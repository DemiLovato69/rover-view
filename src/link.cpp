#include "link.h"
#include "solver.h"
#include <QtEndian>
#include <QDateTime>
#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QTimer>
#include <QScopeGuard>
#include <QFile>
#include <cmath>
#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QCoreApplication>
static constexpr auto usb = "org/fieldrover/Usb";
static QJniObject context() { return QNativeInterface::QAndroidApplication::context(); }
QStringList Link::ports() {
    return QJniObject::callStaticObjectMethod(usb, "ports", "(Landroid/content/Context;)Ljava/lang/String;",
                                              context().object())
        .toString()
        .split('\n', Qt::SkipEmptyParts);
}
struct Port {
    QString error;
    bool open(QString path) {
        error = QJniObject::callStaticObjectMethod(
                    usb, "open", "(Landroid/content/Context;Ljava/lang/String;)Ljava/lang/String;",
                    context().object(), QJniObject::fromString(path).object())
                    .toString();
        return error.isEmpty();
    }
    ~Port() { QJniObject::callStaticMethod<void>(usb, "close", "()V"); }
    bool baud(int b) { return QJniObject::callStaticMethod<jboolean>(usb, "baud", "(I)Z", b); }
    bool write(QByteArray a) {
        QJniEnvironment env;
        auto j = env->NewByteArray(a.size());
        if (!j)
            return false;
        env->SetByteArrayRegion(j, 0, a.size(), reinterpret_cast<const jbyte *>(a.constData()));
        auto ok = QJniObject::callStaticMethod<jboolean>(usb, "write", "([B)Z", j);
        env->DeleteLocalRef(j);
        return ok;
    }
    QByteArray read() {
        auto a = QJniObject::callStaticObjectMethod(usb, "read", "()[B");
        if (!a.isValid()) {
            error = "USB detached or read failed";
            return {};
        }
        QJniEnvironment env;
        auto b = a.object<jbyteArray>();
        QByteArray out(env->GetArrayLength(b), Qt::Uninitialized);
        env->GetByteArrayRegion(b, 0, out.size(), reinterpret_cast<jbyte *>(out.data()));
        return out;
    }
};
#else
#include <QSerialPort>
#include <QSerialPortInfo>
QStringList Link::ports() {
    QStringList out;
    for (auto &p : QSerialPortInfo::availablePorts())
        out << p.systemLocation();
    return out;
}
struct Port {
    QSerialPort serial;
    QString error;
    bool open(QString path) {
        serial.setPortName(path);
        serial.setBaudRate(9600);
        if (serial.open(QIODevice::ReadWrite))
            return true;
        error = serial.errorString();
        return false;
    }
    bool baud(int b) { return serial.setBaudRate(b); }
    bool write(const QByteArray &a) { return serial.write(a) == a.size(); }
    QByteArray read() {
        if (serial.error() == QSerialPort::ResourceError)
            error = serial.errorString();
        return serial.readAll();
    }
};
#endif

static QByteArray frame(int type, QByteArray payload) {
    QByteArray b(1, 0);
    b += char(type);
    b += char(payload.size());
    b += payload;
    quint8 sum = 0;
    for (auto c : b)
        sum += quint8(c);
    return '\x02' + b + char(sum) + '\x03';
}
// RT17's C decoder trusts the embedded satellite count and per-satellite lengths.
static bool validObs(const QByteArray &body) {
    if (body.size() < 17)
        return false;
    int n = quint8(body[16]), i = 17;
    if (!n || n > 32)
        return false;
    while (n--) {
        if (i + 6 > body.size() || quint8(body[i]) < 1 || quint8(body[i]) > 32)
            return false;
        int f = quint8(body[i + 1]);
        i += 9 + (f & 64 ? 21 : 0) + (f & 1 ? 13 : 0);
    }
    return i == body.size();
}
void Link::run() {
    if (config["replay"].toBool()) {
        bool walk = config.value("walk").toBool();
        QFile file(walk ? ":/fixtures/grading-walk.bin" : ":/fixtures/practice.bin");
        if (!file.open(QIODevice::ReadOnly)) {
            emit receiver("Practice file unavailable");
            return;
        }
        auto h = file.read(6);
        if (h.size() != 6)
            return;
        Solver solver;
        if (!solver.ready())
            return;
        solver.setReplayTime(qFromBigEndian<quint16>(h.constData()),
                             qFromBigEndian<quint32>(h.constData() + 2));
        emit receiver(walk ? "SIMULATED · grading walk" : "SIMULATED · recorded practice epochs");
        emit corrections("Synthetic paired base");
        while (!isInterruptionRequested() && !file.atEnd()) {
            auto head = file.read(3);
            if (head.size() != 3)
                break;
            int len = qFromBigEndian<quint16>(head.constData() + 1);
            auto data = file.read(len);
            if (data.size() != len)
                break;
            if (head[0] == 'B')
                solver.base(data);
            else {
                auto p = solver.rover(data);
                if (!p.isEmpty()) {
                    p["source"] = "Simulated RT17 replay / RTKLIB";
                    p["base"] = "Synthetic station 42";
                    emit epoch(p);
                    for (int i = 0; i < 20 && !isInterruptionRequested(); i++)
                        msleep(50);
                }
            }
        }
        emit receiver("Practice replay ended");
        return;
    }
    Port port;
    if (!port.open(config["port"].toString())) {
        emit receiver(port.error);
        return;
    }
    Solver solver;
    if (!solver.ready()) {
        emit receiver("Solver initialization failed");
        return;
    }
    QElapsedTimer clock;
    clock.start();
    QNetworkAccessManager network;
    QNetworkReply *stream = nullptr;
    qint64 retry = 0, lastBase = -1, lastByte = 0;
    QByteArray buffer, pages, body;
    int next = 1, total = 0, replyId = 0, prn = 0, acks = 0;
    quint32 mask = 0;
    QString identity;
    bool mock = false;
    enum { Identity, Ack, Flags, Eph, Obs } stage = Identity;
    qint64 deadline = 2000, poll = 0, refresh = 0;
    bool waiting = true;
    auto send = [&](QByteArray a) {
        if (!port.write(a)) {
            emit receiver("Serial write failed");
            quit();
        }
        waiting = true;
        deadline = clock.elapsed() + 1500;
    };
    auto advanceEph = [&]() {
        while (++prn <= 32 && !(mask & (1u << (prn - 1)))) {
        }
        if (prn <= 32) {
            stage = Eph;
            send(frame(0x54, QByteArray(1, 1) + char(prn) + char(0)));
        } else {
            stage = Obs;
            waiting = false;
        }
    };
    auto caster = [&]() {
        QNetworkRequest req(config["url"].toUrl());
        req.setRawHeader("User-Agent", "NTRIP FieldRover/0.1");
        req.setRawHeader("Ntrip-Version", "Ntrip/2.0");
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        auto user = config["user"].toString();
        if (!user.isEmpty())
            req.setRawHeader("Authorization",
                             "Basic " + (user + ":" + config["password"].toString()).toUtf8().toBase64());
        stream = network.get(req);
        stream->setReadBufferSize(65536);
        lastByte = clock.elapsed();
        lastBase = -1;
        emit corrections("Connecting to caster");
        connect(stream, &QNetworkReply::readyRead, &network, [&]() {
            if (stream->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200 ||
                stream->header(QNetworkRequest::ContentTypeHeader)
                    .toString()
                    .contains("sourcetable", Qt::CaseInsensitive)) {
                stream->abort();
                return;
            }
            lastByte = clock.elapsed();
            if (solver.base(stream->readAll())) {
                lastBase = lastByte;
                emit corrections("Base observations live");
            } else if (lastBase < 0)
                emit corrections("Stream open · waiting for base observations");
        });
        connect(stream, &QNetworkReply::finished, &network, [&]() {
            // A server's reason string or URL can echo the rover's credentials into diagnostics.
            const auto status = stream->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            auto reason = status >= 400
                              ? QString("HTTP %1").arg(status)
                              : QString("connection closed (network code %1)").arg(int(stream->error()));
            stream->deleteLater();
            stream = nullptr;
            solver.resetBase();
            emit boundary();
            emit corrections("Caster offline · retrying: " + reason);
            retry = clock.elapsed() + 4000;
        });
    };
    QTimer tick;
    connect(&tick, &QTimer::timeout, &tick, [&]() {
        auto now = clock.elapsed();
        if (isInterruptionRequested()) {
            quit();
            return;
        }
        if (!stream && now >= retry)
            caster();
        if (stream && now - lastByte > 12000)
            stream->abort();
        if (lastBase >= 0 && now - lastBase > 5000) {
            emit corrections("Base observations stale");
            lastBase = -1;
        }
        buffer += port.read();
        if (!port.error.isEmpty()) {
            emit receiver(port.error);
            quit();
            return;
        }
        while (!buffer.isEmpty()) {
            if (stage == Ack && buffer[0] == '\x06') {
                buffer.remove(0, 1);
                stage = Flags;
                send(frame(0x54, QByteArray(3, 0)));
                refresh = now + 300000;
                continue;
            }
            if (buffer[0] != 2) {
                if (buffer[0] == '\x15')
                    deadline = now;
                buffer.remove(0, 1);
                continue;
            }
            if (buffer.size() < 4)
                break;
            int len = quint8(buffer[3]) + 6;
            if (buffer.size() < len)
                break;
            quint8 sum = 0;
            for (int i = 1; i < len - 2; i++)
                sum += quint8(buffer[i]);
            if (buffer[len - 1] != 3 || sum != quint8(buffer[len - 2])) {
                buffer.remove(0, 1);
                continue;
            }
            auto packet = buffer.left(len);
            buffer.remove(0, len);
            int type = quint8(packet[2]);
            auto d = packet.mid(4, len - 6);
            if (stage == Identity && type == 0x6e) {
                QString id = QString::fromLatin1(d);
                auto match = QRegularExpression("(?:^|;)PORT,\\d+,(\\d+),\\1,8,1,N,F;").match(id);
                if (!id.contains("PRODUCT,5700;") || !id.contains("DCOL") || !match.hasMatch() ||
                    !port.baud(match.captured(1).toInt())) {
                    emit receiver("Unsupported receiver or serial settings");
                    quit();
                    return;
                }
                mock = id.contains("SIMULATED;");
                identity = (mock ? "SIMULATED 5700" : "Trimble 5700") + QString(" · ") +
                           id.section("VERSION,", 1).section(',', 0, 0) + " · " + match.captured(1) +
                           " baud";
                emit receiver(identity);
                stage = Ack;
                send("\x05");
                deadline = now + 150;
            } else if (stage == Flags && type == 0x55 && d.size() >= 6 && d[0] == 0) {
                mask = qFromBigEndian<quint32>(d.constData() + 2);
                prn = 0;
                advanceEph();
            } else if (stage == Eph && type == 0x55 && d.size() == 176 && d[0] == 1 &&
                       quint8(d[1]) == prn) {
                int week = (QDateTime::currentSecsSinceEpoch() - 315964800 + 18) / 604800;
                int w = qFromBigEndian<quint16>(d.constData() + 2);
                w += 1024 * qRound((week - w) / 1024.0);
                if (abs(w - week) <= 1) {
                    qToBigEndian<quint16>(w, d.data() + 2);
                    solver.rover(frame(0x55, d));
                } else
                    emit receiver("Inconsistent GPS week · check clock");
                advanceEph();
            } else if (stage == Obs && waiting && type == 0x57 && d.size() >= 4 && d[0] == 0 && d[3] == 3) {
                int num = quint8(d[1]) >> 4, count = quint8(d[1]) & 15;
                if (num != next || num > count ||
                    (num > 1 && (total != count || replyId != quint8(d[2])))) {
                    waiting = false;
                    continue;
                }
                next++;
                total = count;
                replyId = quint8(d[2]);
                pages += packet;
                body += d.mid(4);
                if (num == count) {
                    waiting = false;
                    if (validObs(body)) {
                        auto p = solver.rover(pages);
                        if (!p.isEmpty()) {
                            p["source"] = mock ? "simulated RT17 / RTKLIB" : "Trimble RT17 / RTKLIB";
                            p["base"] = config["url"].toString();
                            emit epoch(p);
                            emit receiver(identity);
                        }
                    }
                }
            }
        }
        if (waiting && now >= deadline) {
            buffer.clear();
            if (stage == Ack && ++acks < 15) {
                send("\x05");
                deadline = now + 150;
            } else if (stage == Identity || stage == Ack) {
                emit receiver("5700 handshake timed out");
                quit();
                return;
            } else if (stage == Eph)
                advanceEph();
            else {
                emit receiver(identity + " · observation request timed out");
                stage = Obs;
                waiting = false;
            }
        }
        if (stage == Obs && !waiting && now >= poll) {
            if (now >= refresh) {
                stage = Flags;
                send(frame(0x54, QByteArray(3, 0)));
                refresh = now + 300000;
            } else {
                pages.clear();
                body.clear();
                next = 1;
                send(frame(0x56, QByteArray::fromHex("000300")));
                poll = now + 1000;
            }
        }
    });
    emit receiver("Identifying 5700 at 9600 baud");
    send("x");
    tick.start(25);
    exec();
    if (stream)
        stream->disconnect(&network);
}
