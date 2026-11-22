#include "survey.h"
#include "solver.h"
#include <QDir>
#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QSaveFile>
#include <QFile>
#include <QTimer>
#include <QSettings>
#include <QCryptographicHash>
#include <QDateTime>
#include <QtMath>
#include <geodesic.h>
#include <cmath>

Survey::Survey() {
    auto dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    db = QSqlDatabase::addDatabase("QSQLITE");
    db.setDatabaseName(dir + "/field.sqlite");
    if (!db.open()) {
        notice = db.lastError().text();
        return;
    }
    QSqlQuery q(db);
    for (auto sql :
         {"PRAGMA foreign_keys=ON",
          "CREATE TABLE IF NOT EXISTS jobs(name TEXT PRIMARY KEY,antenna TEXT NOT NULL,pole REAL NOT "
          "NULL,frame TEXT NOT NULL)",
          "CREATE TABLE IF NOT EXISTS grading(job TEXT PRIMARY KEY REFERENCES jobs(name),settings TEXT NOT "
          "NULL,xml BLOB)",
          "CREATE TABLE IF NOT EXISTS points(job TEXT REFERENCES jobs(name),id TEXT NOT NULL,epoch "
          "TEXT,record TEXT NOT NULL,samples TEXT NOT NULL,PRIMARY KEY(job,id),UNIQUE(job,epoch))"})
        if (!q.exec(sql)) {
            notice = q.lastError().text();
            return;
        }
    loadJobs();
    if (!jobRows.isEmpty())
        openJob(QSettings().value("lastJob", jobRows.first()).toString());
    scan();
    auto timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this] {
        bool updated = false;
        if (!live.isEmpty() && fresh.isValid() && displayedAge != fresh.elapsed() / 1000) {
            displayedAge = fresh.elapsed() / 1000;
            updated = true;
        }
        if (valid && fresh.elapsed() > 4000) {
            valid = false;
            lastAccepted = 0;
            if (!occupation.isEmpty() && !occupation.value("review").toBool() &&
                !occupation.value("interrupted").toBool())
                occupation["status"] = "Paused · rover observations stale";
            log("Solution", "No fresh position");
            updated = true;
        }
        if (updated)
            emit changed();
    });
    timer->start(250);
}
Survey::~Survey() {
    stop();
    auto name = db.connectionName();
    db.close();
    db = {};
    QSqlDatabase::removeDatabase(name);
}
bool Survey::message(QString s) {
    notice = s;
    log("Job", s);
    emit changed();
    return false;
}
void Survey::log(QString channel, QString text) {
    logLines.prepend(QDateTime::currentDateTimeUtc().toString("HH:mm:ss 'UTC'") + "  " + channel + "  ·  " +
                     text);
    if (logLines.size() > 200)
        logLines.removeLast();
    emit eventsChanged();
}
void Survey::scan() {
    devices = Link::ports();
    emit portsChanged();
}
void Survey::loadJobs() {
    jobRows.clear();
    QSqlQuery q("SELECT name FROM jobs ORDER BY rowid DESC", db);
    while (q.next())
        jobRows << q.value(0);
    emit jobsChanged();
}
void Survey::loadPoints() {
    pointRows.clear();
    lastPointEpoch.clear();
    QSqlQuery q(db);
    q.prepare("SELECT record,epoch FROM points WHERE job=? ORDER BY rowid DESC");
    q.addBindValue(job);
    if (q.exec())
        while (q.next()) {
            pointRows << QJsonDocument::fromJson(q.value(0).toByteArray()).toVariant();
            if (lastPointEpoch.isEmpty())
                lastPointEpoch = q.value(1).toString();
        }
    emit pointsChanged();
}
bool Survey::createJob(QString name, QString model, double height, QString reference, bool updateSetup) {
    name = name.trimmed();
    if (!occupation.isEmpty() || name.isEmpty() || model.trimmed().isEmpty() || !std::isfinite(height) ||
        height < 0 || height > 20)
        return message("Enter a job name, antenna model and vertical height (0–20 m)");
    QSqlQuery q(db);
    if (updateSetup && name != job)
        return message("Select the active job to change its setup");
    q.prepare(updateSetup ? "UPDATE jobs SET antenna=:model,pole=:pole,frame=:frame WHERE name=:name"
                          : "INSERT INTO jobs VALUES(:name,:model,:pole,:frame)");
    q.bindValue(":name", name);
    q.bindValue(":model", model.trimmed());
    q.bindValue(":pole", height);
    q.bindValue(":frame", reference.trimmed().isEmpty() ? QString("Unknown") : reference.trimmed());
    if (!q.exec())
        return message(q.lastError().text());
    loadJobs();
    openJob(name);
    return true;
}
void Survey::openJob(QString name) {
    if (!occupation.isEmpty()) {
        message("Finish or discard this occupation first");
        return;
    }
    QSqlQuery q(db);
    q.prepare("SELECT antenna,pole,frame FROM jobs WHERE name=?");
    q.addBindValue(name);
    if (!q.exec() || !q.next())
        return;
    job = name;
    antenna = q.value(0).toString();
    pole = q.value(1).toDouble();
    frame = q.value(2).toString();
    if (live.contains("ellipsoidM"))
        addHeights(live, pole);
    QSettings().setValue("lastJob", job);
    loadPoints();
    loadGrade();
    emit jobChanged();
    message("Opened " + job);
}
QVariantMap Survey::state() const {
    bool current = valid && fresh.isValid() && fresh.elapsed() <= 4000;
    auto z = live.value(surface.datum == "CGVD2013" ? "groundCgvd2013M" : "groundEllipsoidM");
    return {{"job", job},
            {"antenna", antenna},
            {"pole", pole},
            {"frame", frame},
            {"receiver", rx},
            {"base", base},
            {"connected", bool(link)},
            {"ageSeconds", fresh.isValid() ? fresh.elapsed() / 1000 : -1},
            {"hasSurface", !surface.mesh.isEmpty()},
            {"surfaceName", surface.name},
            {"surfacePosition",
             current && z.isValid() && gradePrefs.value("surfaceFrame", gradePrefs.value("frame")) == frame
                 ? surface.localPosition(live.value("latitudeDeg").toDouble(),
                                         live.value("longitudeDeg").toDouble(), z.toDouble())
                 : QVariantMap{}},
            {"live", live},
            {"grade", guidance(live, current)},
            {"gradePrefs", gradePrefs},
            {"fresh", current},
            {"canStore", current && !job.isEmpty() && occupation.isEmpty() &&
                             live.value("gpst").toString() != lastPointEpoch &&
                             live.value("gpst").toString() != deletedLiveEpoch.value(job)},
            {"occupation", occupation},
            {"notice", notice}};
}
bool Survey::start(QString port, QString address, QString user, QString password) {
    if (link)
        return message("Disconnect the current receiver first");
    QUrl url(address);
    if (port.trimmed().isEmpty() || !url.isValid() || url.host().isEmpty() ||
        (url.scheme() != "http" && url.scheme() != "https") || url.path().size() < 2 ||
        !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment())
        return message("Enter a serial device and an HTTP(S) caster URL including its mountpoint");
    return startLink({{"port", port.trimmed()}, {"url", url}, {"user", user}, {"password", password}});
}
bool Survey::practice() { return startLink({{"replay", true}}); }
bool Survey::gradingWalk() { return startLink({{"replay", true}, {"walk", true}}); }

void Survey::loadGrade() {
    gradePrefs.clear();
    surface = Surface();
    pendingXml.clear();
    savedXml.clear();
    QSqlQuery q(db);
    q.prepare("SELECT settings,xml FROM grading WHERE job=?");
    q.addBindValue(job);
    if (q.exec() && q.next()) {
        gradePrefs = QJsonDocument::fromJson(q.value(0).toByteArray()).toVariant().toMap();
        savedXml = q.value(1).toByteArray();
        if (!savedXml.isEmpty() && !gradePrefs.value("surface").toString().isEmpty()) {
            auto err = surface.read(savedXml, gradePrefs.value("surface").toString());
            if (!err.isEmpty()) {
                surface = Surface();
                if (gradePrefs.value("mode") == "Surface")
                    gradePrefs["error"] = err;
                log("Design", err);
            }
        }
    }
    emit gradeSetupChanged();
}
QVariantMap Survey::inspectSurface(QUrl url) {
    pendingXml.clear();
    QString path = url.isEmpty()       ? ":/fixtures/practice-pad.landxml"
                   : url.isLocalFile() ? url.toLocalFile()
                                       : url.toString();
    constexpr int limit = 16 * 1024 * 1024;
    QByteArray xml;
    if (url.toString() == "job:surface")
        xml = savedXml;
    else {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            return {{"error", f.errorString()}};
        xml = f.read(limit + 1);
    }
    if (xml.size() > limit)
        return {{"error", "Surface exceeds 16 MiB import limit"}};
    Surface info;
    auto error = info.read(xml);
    if (!error.isEmpty())
        return {{"error", error}};
    pendingXml = xml;
    return {
        {"names", info.names}, {"datum", info.datum}, {"epsg", info.epsg}, {"horizontal", info.horizontal}};
}
bool Survey::persistGrade(QVariantMap prefs, const QByteArray &xml) {
    QSqlQuery q(db);
    q.prepare("INSERT OR REPLACE INTO grading(job,settings,xml) VALUES(?,?,?)");
    q.addBindValue(job);
    q.addBindValue(QJsonDocument::fromVariant(prefs).toJson(QJsonDocument::Compact));
    q.addBindValue(xml);
    if (!q.exec())
        return message(q.lastError().text());
    gradePrefs = std::move(prefs);
    savedXml = xml;
    return true;
}
bool Survey::setGrade(QString mode, double elevation, QString datum, double tolerance, bool allowFloat,
                      QString selection) {
    if (job.isEmpty())
        return message("Open a job before configuring grade");
    const bool previewOnly = mode == "Preview";
    if (mode != "Off" && mode != "Manual" && mode != "Surface" && !previewOnly)
        return message("Unknown grade mode");
    if (!std::isfinite(tolerance) || tolerance < 0 || !std::isfinite(elevation))
        return message("Enter finite elevations and a nonnegative tolerance");
    QVariantMap prefs = gradePrefs;
    prefs.remove("error");
    if (!previewOnly) {
        prefs["mode"] = mode;
        prefs["frame"] = frame;
        prefs["toleranceM"] = tolerance;
        prefs["allowFloat"] = allowFloat;
    } else if (prefs.value("mode") != "Manual")
        prefs["mode"] = "Off";
    Surface candidate;
    QByteArray xml = savedXml;
    if (mode == "Manual") {
        if (datum != "CGVD2013" && datum != "Ellipsoid")
            return message("Select a height reference");
        prefs["elevationM"] = elevation;
        prefs["datum"] = datum;
    } else if (mode == "Surface" || previewOnly) {
        if (!pendingXml.isEmpty())
            xml = pendingXml;
        if (selection.isEmpty())
            return message("Select a named surface");
        auto err = candidate.read(xml, selection);
        if (!err.isEmpty())
            return message(err);
        prefs["surface"] = candidate.name;
        prefs["surfaceFrame"] = frame;
        prefs["epsg"] = candidate.epsg;
        if (!previewOnly)
            prefs["datum"] = candidate.datum;
        prefs["sha256"] =
            QString::fromLatin1(QCryptographicHash::hash(xml, QCryptographicHash::Sha256).toHex());
    }
    if (!persistGrade(prefs, xml))
        return false;
    if (mode == "Surface" || previewOnly)
        surface = std::move(candidate);
    pendingXml.clear();
    emit gradeSetupChanged();
    message(previewOnly ? "Loaded design for preview: " + selection : "Grade: " + mode);
    return true;
}
QVariantMap Survey::guidance(const QVariantMap &point, bool current) const {
    auto mode = gradePrefs.value("mode").toString();
    if (mode.isEmpty() || mode == "Off")
        return {};
    QVariantMap g{{"gradeMode", mode},
                  {"gradeDatum", gradePrefs.value("datum")},
                  {"gradeToleranceM", gradePrefs.value("toleranceM")}};
    auto paused = [&](QString text) {
        g["gradeStatus"] = text;
        return g;
    };
    if (mode == "Surface") {
        g["gradeSurface"] = surface.name;
        g["gradeSurfaceSha256"] = gradePrefs.value("sha256");
        g["gradeEpsg"] = surface.epsg;
    } else
        g["gradeManualTargetM"] = gradePrefs.value("elevationM");
    if (frame != gradePrefs.value("frame").toString())
        return paused("Job frame changed · confirm grade setup");
    if (gradePrefs.contains("error"))
        return paused(gradePrefs.value("error").toString());
    if (!current)
        return paused("Waiting for fresh rover position");
    auto q = point.value("quality").toString();
    if (q != "fixed" && !(gradePrefs.value("allowFloat").toBool() && q == "float"))
        return paused("Waiting for qualifying RTK");
    auto h = point.value(gradePrefs.value("datum") == "CGVD2013" ? "groundCgvd2013M" : "groundEllipsoidM");
    if (!h.isValid() || !std::isfinite(h.toDouble()))
        return paused("Ground height unavailable");
    std::optional<double> target =
        mode == "Manual"
            ? std::optional<double>(gradePrefs.value("elevationM").toDouble())
            : surface.target(point.value("latitudeDeg").toDouble(), point.value("longitudeDeg").toDouble());
    if (!target)
        return paused("Outside design surface");
    double diff = h.toDouble() - *target;
    g["gradeGroundM"] = h;
    g["gradeTargetM"] = *target;
    g["cutMinusFillM"] = diff;
    g["gradeStatus"] = std::abs(diff) <= gradePrefs.value("toleranceM").toDouble() + 1e-9 ? "ON GRADE"
                       : diff > 0                                                         ? "CUT"
                                                                                          : "FILL";
    return g;
}
bool Survey::startLink(QVariantMap config) {
    if (link)
        return message("Disconnect the current receiver first");
    auto l = new Link(std::move(config));
    l->setParent(this);
    link = l;
    valid = false;
    live.clear();
    lastQuality.clear();
    fresh.invalidate();
    displayedAge = -1;
    notice.clear();
    connect(l, &Link::receiver, this, [=](QString s) {
        if (link == l) {
            if (rx == s)
                return;
            log("Receiver", s);
            rx = s;
            emit changed();
        }
    });
    connect(l, &Link::corrections, this, [=](QString s) {
        if (link == l) {
            if (base == s)
                return;
            log("Corrections", s);
            base = s;
            emit changed();
        }
    });
    connect(l, &Link::epoch, this, [=](QVariantMap p) {
        if (link == l)
            receive(p);
    });
    connect(l, &Link::boundary, this, [=] {
        if (link == l) {
            valid = false;
            lastAccepted = 0;
            emit changed();
        }
    });
    connect(l, &QThread::finished, this, [=] {
        if (link == l) {
            link = nullptr;
            valid = false;
            base = "Offline";
            interruptOccupation("Receiver session ended");
            emit changed();
        }
        l->deleteLater();
    });
    l->start();
    emit changed();
    return true;
}
void Survey::stop() {
    if (link) {
        log("Receiver", "Session disconnected");
        auto l = link;
        link = nullptr;
        l->requestInterruption();
        l->quit();
        l->wait();
    }
    valid = false;
    rx = "Disconnected";
    base = "Offline";
    interruptOccupation("Receiver disconnected");
    emit changed();
}
void Survey::interruptOccupation(QString reason) {
    if (!occupation.isEmpty() && !occupation.value("review").toBool()) {
        occupation["interrupted"] = true;
        occupation["status"] = reason + " · restart occupation";
    }
    lastAccepted = 0;
}
void Survey::receive(QVariantMap p) {
    bool positioned = p.value("quality") != "none";
    valid = positioned;
    const QString quality = p.value("quality").toString();
    if (lastQuality != quality) {
        lastQuality = quality;
        log("Solution", quality);
    }
    if (positioned) {
        fresh.restart();
        displayedAge = 0;
        live = p;
        addHeights(live, pole);
    } else
        live["quality"] = "none";
    if (occupation.isEmpty() || occupation.value("review").toBool() ||
        occupation.value("interrupted").toBool()) {
        emit changed();
        return;
    }
    const auto t = p.value("epochMs").toLongLong();
    if (quality != "fixed" && !(quality == "float" && occupation.value("allowFloat").toBool())) {
        lastAccepted = 0;
        occupation["excluded"] = occupation.value("excluded").toInt() + 1;
        occupation["status"] = "Paused · waiting for qualifying RTK";
    } else if (!samples.isEmpty() && [&] {
                   auto first = samples.first().toMap();
                   for (auto k : {"station", "baseX", "baseY", "baseZ"})
                       if (first.value(k) != p.value(k))
                           return true;
                   return false;
               }()) {
        interruptOccupation("Base station changed");
    } else if (samples.isEmpty() || t > samples.last().toMap().value("epochMs").toLongLong()) {
        if (lastAccepted && t - lastAccepted <= 2500)
            elapsedMs += t - lastAccepted;
        lastAccepted = t;
        samples << p;
        occupation.insert(average(samples));
        occupation["elapsedS"] = elapsedMs / 1000.;
        occupation["review"] = elapsedMs >= occupation.value("targetS").toInt() * 1000;
        occupation["status"] = occupation.value("review").toBool() ? "Complete · review before saving"
                                                                   : "Occupying · keep pole stationary";
    }
    emit changed();
}
QVariantMap Survey::controlCheck(QString id) const {
    if (!valid || !fresh.isValid() || fresh.elapsed() > 4000)
        return {{"status", "Waiting for a fresh position"}};
    if (live.value("quality") != "fixed")
        return {{"status", "Waiting for fixed RTK"}};
    for (const auto &row : pointRows) {
        auto ref = row.toMap();
        if (ref.value("id") != id)
            continue;
        if (ref.value("gpst") == live.value("gpst"))
            return {{"status", "Wait for a later observation to check this point"}};
        if (ref.value("frame") != frame || frame == "Unknown")
            return {{"status", "Verify matching known reference frames first"}};
        if (ref.value("base") != live.value("base") || ref.value("station") != live.value("station"))
            return {{"status", "Different base station"}};
        if (ref.value("quality") != "fixed")
            return {{"status", "Reference was not stored as fixed RTK"}};
        geod_geodesic ellipsoid;
        geod_init(&ellipsoid, 6378137.0, 1.0 / 298.257222101);
        double horizontal = 0;
        geod_inverse(&ellipsoid, ref.value("latitudeDeg").toDouble(), ref.value("longitudeDeg").toDouble(),
                     live.value("latitudeDeg").toDouble(), live.value("longitudeDeg").toDouble(),
                     &horizontal, nullptr, nullptr);
        const bool ortho = ref.contains("groundCgvd2013M");
        auto key = ortho ? "groundCgvd2013M" : "groundEllipsoidM";
        if (!live.contains(key) || !ref.contains(key))
            return {{"status", "Matching ground elevation unavailable"}};
        return {{"status", "Current minus stored point"},
                {"horizontalM", horizontal},
                {"verticalM", live.value(key).toDouble() - ref.value(key).toDouble()},
                {"datum", ortho ? "CGVD2013" : "Ellipsoid"}};
    }
    return {{"status", "Choose a point from this job"}};
}
bool Survey::store(QString id, QString code) {
    if (!state().value("canStore").toBool())
        return message("A new, fresh solution and an open job are required");
    return save(id, code, live, false);
}
bool Survey::occupy(QString id, QString code, int seconds, bool allowFloat) {
    id = id.trimmed();
    if (!link || job.isEmpty() || id.isEmpty() || seconds < 1 || seconds > 3600 || !occupation.isEmpty())
        return message("Connect, open a job, and select a benchmark ID and duration (1–3600 s)");
    for (const auto &row : pointRows)
        if (row.toMap().value("id") == id)
            return message("Point ID already exists");
    occupation = {{"id", id},
                  {"code", code},
                  {"targetS", seconds},
                  {"allowFloat", allowFloat},
                  {"status", "Waiting for qualifying RTK"},
                  {"epochs", 0},
                  {"elapsedS", 0},
                  {"excluded", 0}};
    samples.clear();
    lastAccepted = elapsedMs = 0;
    emit changed();
    return true;
}
void Survey::discard() {
    occupation.clear();
    samples.clear();
    lastAccepted = elapsedMs = 0;
    emit changed();
}
bool Survey::finish() {
    if (!occupation.value("review").toBool() || samples.isEmpty())
        return message("Occupation incomplete");
    auto p = occupation;
    p["startGpst"] = samples.first().toMap().value("gpst");
    auto end = samples.last().toMap();
    for (auto k : {"gpst", "source", "base", "station"})
        p[k] = end[k];
    return save(p["id"].toString(), p["code"].toString(), p, true);
}
bool Survey::save(QString id, QString code, QVariantMap p, bool benchmark) {
    id = id.trimmed();
    if (id.isEmpty())
        return message("Enter a point ID");
    p["id"] = id;
    p["code"] = code;
    p["kind"] = benchmark ? "Benchmark" : "Point";
    p["antenna"] = antenna;
    p["frame"] = frame;
    p["job"] = job;
    addHeights(p, pole);
    if (!benchmark)
        p.insert(guidance(p, true));
    // One atomic INSERT holds the snapshot and its contributing epochs together.
    QSqlQuery q(db);
    q.prepare("INSERT INTO points(job,id,epoch,record,samples) VALUES(?,?,?,?,?)");
    q.addBindValue(job);
    q.addBindValue(id);
    q.addBindValue(benchmark ? QVariant() : p["gpst"]);
    q.addBindValue(QJsonDocument::fromVariant(p).toJson(QJsonDocument::Compact));
    q.addBindValue(
        QJsonDocument::fromVariant(benchmark ? samples : QVariantList{}).toJson(QJsonDocument::Compact));
    if (!q.exec())
        return message("Not saved: " + q.lastError().text());
    if (benchmark)
        discard();
    loadPoints();
    message("Saved " + id);
    return true;
}
QVariantList Survey::epochsFor(QString id) const {
    QSqlQuery q(db);
    q.prepare("SELECT samples FROM points WHERE job=? AND id=?");
    q.addBindValue(job);
    q.addBindValue(id);
    if (q.exec() && q.next())
        return QJsonDocument::fromJson(q.value(0).toByteArray()).toVariant().toList();
    return {};
}
bool Survey::deletePoint(QString expectedJob, QString id, QString gpst) {
    if (job.isEmpty() || job != expectedJob)
        return message("Not deleted: the active job has changed");
    QSqlQuery q(db);
    q.prepare("DELETE FROM points WHERE job=? AND id=? AND json_extract(CAST(record AS TEXT),'$.gpst')=?");
    q.addBindValue(job);
    q.addBindValue(id);
    q.addBindValue(gpst);
    if (!q.exec())
        return message("Not deleted: " + q.lastError().text());
    if (q.numRowsAffected() != 1)
        return message("Not deleted: record no longer matches the selection");
    // Keep the double-tap guard even after deleting the row that held its epoch.
    if (live.value("gpst").toString() == gpst && lastPointEpoch == gpst)
        deletedLiveEpoch.insert(job, gpst);
    loadPoints();
    message("Deleted " + id + " from " + job);
    return true;
}
bool Survey::exportCsv(QUrl dest) {
    if (pointRows.isEmpty())
        return message("No points to export");
    QStringList keys;
    for (auto &v : pointRows)
        for (auto &key : v.toMap().keys())
            if (!keys.contains(key))
                keys << key;
    QByteArray out = keys.join(',').toUtf8() + "\r\n";
    for (auto &v : pointRows) {
        QStringList row;
        auto p = v.toMap();
        for (auto &k : keys) {
            auto val = p.value(k);
            auto text = val.typeId() == QMetaType::Double ? QString::number(val.toDouble(), 'g', 16)
                                                          : val.toString();
            row << ('"' + text.replace('"', "\"\"") + '"');
        }
        out += row.join(',').toUtf8() + "\r\n";
    }
    auto path = dest.isLocalFile() ? dest.toLocalFile() : dest.toString();
#ifdef Q_OS_ANDROID
    if (dest.scheme() == "content") {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(out) != out.size() || !f.flush())
            return message(f.errorString());
        message("CSV exported");
        return true;
    }
#endif
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(out) != out.size() || !f.commit())
        return message(f.errorString());
    message("CSV exported");
    return true;
}
