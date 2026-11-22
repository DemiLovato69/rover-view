#include "basemap.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QSettings>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QUuid>
#include <QtConcurrentRun>
#include <cmath>

namespace {
// Connections belong to the calling thread (import worker or QML image loader).
struct ReadOnlyDb {
    QString name = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", name);
    explicit ReadOnlyDb(const QString &path) {
        db.setDatabaseName(path);
        db.setConnectOptions("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=1000");
        if (db.open()) {
            QSqlQuery q(db);
            q.exec("PRAGMA trusted_schema=OFF");
            q.exec("PRAGMA query_only=ON");
        }
    }
    ~ReadOnlyDb() {
        db.close();
        db = {};
        QSqlDatabase::removeDatabase(name);
    }
};
QImage decode(QByteArray bytes) {
    QBuffer buf(&bytes);
    buf.open(QIODevice::ReadOnly);
    QImageReader reader(&buf);
    const auto format = reader.format().toLower();
    if (format != "png" && format != "jpeg" && format != "webp")
        return {};
    const auto s = reader.size();
    if (!s.isValid() || s.width() != s.height() || s.width() > 1024)
        return {};
    return reader.read();
}
QVariantMap inspect(QString path) {
    auto fail = [](QString why) { return QVariantMap{{"error", why}}; };
    ReadOnlyDb file(path);
    if (!file.db.isOpen())
        return fail("Cannot open the MBTiles database");
    QSqlQuery q(file.db);
    if (!q.exec("PRAGMA quick_check(1)") || !q.next() || q.value(0) != "ok")
        return fail("Package integrity check failed");
    if (!q.exec("SELECT name,substr(value,1,4096) FROM metadata LIMIT 512"))
        return fail("Invalid MBTiles metadata");
    QVariantMap m;
    while (q.next())
        m.insert(q.value(0).toString(), q.value(1));
    auto format = m.value("format").toString().toLower();
    if (!QStringList{"jpg", "jpeg", "png", "webp"}.contains(format))
        return fail("Use a raster PNG, JPEG or WebP MBTiles package");
    auto scheme = m.value("scheme", "tms").toString().toLower();
    if (scheme != "xyz" && scheme != "tms")
        return fail("Unknown tile row scheme");
    if (!q.exec("SELECT min(zoom_level),max(zoom_level) FROM tiles") || !q.next() || q.value(0).isNull())
        return fail("Empty or unreadable tile pyramid");
    int lo = q.value(0).toInt(), hi = q.value(1).toInt();
    if (lo < 0 || hi > 24 || lo > hi)
        return fail("Unsupported zoom levels");
    if (!q.exec("SELECT tile_data FROM tiles WHERE length(tile_data) BETWEEN 1 AND 8388608 LIMIT 1") ||
        !q.next() || decode(q.value(0).toByteArray()).isNull())
        return fail("No decodable raster tile");
    QVariantList bounds;
    for (auto text : m.value("bounds").toString().split(',')) {
        bool ok;
        double n = text.toDouble(&ok);
        if (ok && std::isfinite(n))
            bounds << n;
    }
    if (bounds.size() != 4 || bounds[0].toDouble() < -180 || bounds[2].toDouble() > 180 ||
        bounds[1].toDouble() < -90 || bounds[3].toDouble() > 90 ||
        bounds[0].toDouble() >= bounds[2].toDouble() || bounds[1].toDouble() >= bounds[3].toDouble())
        bounds.clear();
    return {{"name", m.value("name")}, {"minZoom", lo},    {"maxZoom", hi},
            {"bounds", bounds},        {"scheme", scheme}, {"attribution", m.value("attribution")},
            {"date", m.value("date")}};
}
QString sourcePath(QUrl u) { return u.isLocalFile() ? u.toLocalFile() : u.toString(); }
} // namespace

Basemap::Basemap() {
    root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/maps/";
    online = QSettings().value("map/online").toMap();
    connect(&worker, &QFutureWatcher<QVariantMap>::finished, this, [this] {
        auto result = worker.result();
        importing = false;
        if (result.contains("error")) {
            status = result.value("error").toString();
            note(status);
            emit changed();
            return;
        }
        auto key = result.take("key").toString();
        QSettings settings;
        auto previous = settings.value(key).toMap();
        settings.setValue(key, result);
        settings.sync();
        if (settings.status() != QSettings::NoError) {
            status = "Could not save job map selection";
            QFile::remove(root + result.value("revision").toString() + ".mbtiles");
        } else {
            if (jobKey == key) {
                offline = result;
                QMutexLocker guard(&lock);
                revision = result.value("revision").toString();
                activePath = root + revision + ".mbtiles";
                xyz = result.value("scheme") == "xyz";
            }
            if (!previous.value("revision").toString().isEmpty())
                QFile::remove(root + previous.value("revision").toString() + ".mbtiles");
            status = "Verified and saved offline map: " + result.value("name").toString();
        }
        note(status);
        emit changed();
    });
}
Basemap::~Basemap() {
    if (importing) {
        worker.waitForFinished();
        auto rev = worker.result().value("revision").toString();
        if (!rev.isEmpty())
            QFile::remove(root + rev + ".mbtiles");
    }
}
void Basemap::selectJob(const QString &job) {
    auto key =
        job.isEmpty()
            ? QString()
            : "map/jobs/" + QString::fromLatin1(
                                QCryptographicHash::hash(job.toUtf8(), QCryptographicHash::Sha256).toHex());
    if (key == jobKey)
        return;
    jobKey = key;
    offline = key.isEmpty() ? QVariantMap{} : QSettings().value(key).toMap();
    {
        QMutexLocker guard(&lock);
        revision = offline.value("revision").toString();
        activePath = root + revision + ".mbtiles";
        xyz = offline.value("scheme") == "xyz";
        if (revision.isEmpty() || !QFileInfo::exists(activePath)) {
            offline.clear();
            activePath.clear();
            revision.clear();
        }
    }
    status.clear();
    emit changed();
}
bool Basemap::importTiles(QUrl url) {
    if (importing)
        return false;
    bool practice = url.toString() == "practice:ortho";
    if (jobKey.isEmpty() || (!practice && !url.isLocalFile() && url.scheme() != "content")) {
        status = "Open a job and choose a local MBTiles file";
        emit changed();
        return false;
    }
    auto key = jobKey, dir = root,
         path = practice ? QString(":/fixtures/practice-aerial.mbtiles") : sourcePath(url);
    importing = true;
    status = "Copying and checking map…";
    emit changed();
    worker.setFuture(QtConcurrent::run([key, dir, path] {
        auto fail = [](QString e) { return QVariantMap{{"error", e}}; };
        QFile source(path);
        if (!source.open(QIODevice::ReadOnly))
            return fail("Cannot read selected map file");
        QDir().mkpath(dir);
        QTemporaryFile tmp(dir + "import-XXXXXX");
        if (!tmp.open())
            return fail(tmp.errorString());
        QByteArray block(1024 * 1024, Qt::Uninitialized);
        qint64 n;
        while ((n = source.read(block.data(), block.size())) > 0)
            if (tmp.write(block.constData(), n) != n)
                return fail("Map copy failed: " + tmp.errorString());
        if (n < 0 || !tmp.flush())
            return fail("Incomplete map copy");
        tmp.close();
        auto info = inspect(tmp.fileName());
        if (info.contains("error"))
            return info;
        auto rev = QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (!tmp.rename(dir + rev + ".mbtiles"))
            return fail(tmp.errorString());
        tmp.setAutoRemove(false);
        info["key"] = key;
        info["revision"] = rev;
        info["sizeBytes"] = source.size();
        if (info.value("name").toString().trimmed().isEmpty())
            info["name"] = "Local imagery";
        return info;
    }));
    return true;
}
bool Basemap::setOnline(QString url, QString attribution, int maxZoom) {
    url = url.trimmed();
    auto u = QUrl(url);
    if (!url.isEmpty() && (u.scheme() != "https" || u.host().isEmpty() || !u.userInfo().isEmpty() ||
                           !url.contains("{z}") || !url.contains("{x}") || !url.contains("{y}") ||
                           attribution.trimmed().isEmpty() || maxZoom < 0 || maxZoom > 24)) {
        status = "Use an attributed HTTPS XYZ template with {z}, {x}, {y} and native zoom 0–24";
        emit changed();
        return false;
    }
    {
        QMutexLocker guard(&lock);
        online = url.isEmpty()
                     ? QVariantMap{}
                     : QVariantMap{{"url", url},
                                   {"attribution", attribution.trimmed()},
                                   {"maxZoom", maxZoom},
                                   {"revision", QUuid::createUuid().toString(QUuid::WithoutBraces)}};
    }
    QSettings().setValue("map/online", u.hasQuery() ? QVariantMap{} : onlineInfo());
    status = u.hasQuery() ? "Imagery URL with query parameters saved for this session only"
                          : "Imagery source updated";
    note(status);
    emit changed();
    return true;
}
QUrl Basemap::resolveImagery(const QUrl &alias) const {
    const auto parts = alias.path().split('/', Qt::SkipEmptyParts);
    auto config = onlineInfo();
    if (parts.size() != 4 || parts[0] != config.value("revision").toString())
        return {};
    auto url = config.value("url").toString();
    url.replace("{z}", parts[1]).replace("{x}", parts[2]).replace("{y}", parts[3]);
    return QUrl(url);
}
void Basemap::note(QString text) {
    // Only status summaries belong in logs; tile URLs can contain provider credentials.
    if (text.contains("://"))
        text = "Tile request failed";
    auto entry = "Map  ·  " + text.left(250);
    if (!history.isEmpty() && history.first().endsWith(entry))
        return;
    history.prepend(QDateTime::currentDateTimeUtc().toString("HH:mm:ss 'UTC'  ") + entry);
    if (history.size() > 100)
        history.removeLast();
    emit eventsChanged();
}
QImage Basemap::tile(const QString &id) const {
    auto parts = id.split('/');
    if (parts.size() != 4)
        return {};
    QString path;
    bool useXyz;
    {
        QMutexLocker guard(&lock);
        if (revision.isEmpty() || parts[0] != revision)
            return {};
        path = activePath;
        useXyz = xyz;
    }
    bool ok;
    int z = parts[1].toInt(&ok);
    if (!ok || z < 0 || z > 24)
        return {};
    int count = 1 << z;
    int x = parts[2].toInt(&ok);
    if (!ok || x < 0 || x >= count)
        return {};
    int y = parts[3].toInt(&ok);
    if (!ok || y < 0 || y >= count)
        return {};
    ReadOnlyDb f(path);
    if (!f.db.isOpen())
        return {};
    QSqlQuery q(f.db);
    q.prepare("SELECT tile_data FROM tiles WHERE zoom_level=? AND tile_column=? AND tile_row=? AND "
              "length(tile_data)<=8388608 LIMIT 1");
    q.addBindValue(z);
    q.addBindValue(x);
    q.addBindValue(useXyz ? y : count - 1 - y);
    return q.exec() && q.next() ? decode(q.value(0).toByteArray()) : QImage{};
}
