#include "survey.h"
#include <QFile>
#include <QJsonDocument>
#include <QSqlQuery>
#include <QSqlError>
#include <QRegularExpression>
#include <QSet>
#include <cmath>

using namespace Fieldwork;
namespace {
QByteArray json(QVariant value) { return QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact); }
QVariantList coordinates(PJ *pj, const QPolygonF &p) {
    QVariantList out;
    for (auto xy : p) {
        auto g = geographic(pj, xy);
        if (g.isEmpty())
            return {};
        out << g;
    }
    return out;
}
QPolygonF vertices(const QVariantMap &row) {
    QPolygonF out;
    for (auto &v : row.value("en").toList()) {
        auto pair = v.toList();
        if (pair.size() != 2)
            return {};
        out << QPointF(pair[0].toDouble(), pair[1].toDouble());
    }
    return out;
}
QVariantMap feature(QString name, QString source, QString frame, int epsg, PJ *pj, QPolygonF xy) {
    QVariantList en;
    for (auto p : xy)
        en << QVariant(QVariantList{p.x(), p.y()});
    auto geo = coordinates(pj, xy);
    if (geo.size() < 2 || parallel(xy, 0).isEmpty())
        return {};
    return {{"name", name}, {"source", source}, {"frame", frame},
            {"epsg", epsg}, {"en", en},         {"path", geo}};
}
// A line sketched in this job needs local metres, not an operator-assigned map datum.
auto lineProjection(const QVariantMap &f) {
    if (!f.contains("localOrigin"))
        return Surface::utmProjection(f.value("epsg").toInt()); // Existing grid lines and DXF.
    auto o = f.value("localOrigin").toMap();
    auto spec = QString("+proj=aeqd +ellps=GRS80 +lat_0=%1 +lon_0=%2")
                    .arg(o.value("latitudeDeg").toDouble(), 0, 'g', 17)
                    .arg(o.value("longitudeDeg").toDouble(), 0, 'g', 17)
                    .toLatin1();
    return std::unique_ptr<PJ, decltype(&proj_destroy)>(proj_create(nullptr, spec.constData()),
                                                        proj_destroy);
}
} // namespace
void Survey::loadFieldwork() {
    pendingDrawing = {};
    featureRows.clear();
    workPrefs.clear();
    QSqlQuery q(db);
    q.prepare("SELECT prefs FROM field_settings WHERE job=?");
    q.addBindValue(job);
    if (q.exec() && q.next())
        workPrefs = QJsonDocument::fromJson(q.value(0).toByteArray()).toVariant().toMap();
    q.prepare("SELECT geometry FROM feature_lines WHERE job=? ORDER BY name");
    q.addBindValue(job);
    if (q.exec())
        while (q.next())
            featureRows << QJsonDocument::fromJson(q.value(0).toByteArray()).toVariant();
    rebuildGuide();
    emit fieldChanged();
}
bool Survey::saveFieldPrefs(const QVariantMap &prefs) {
    if (job.isEmpty())
        return message("Open a job first");
    QSqlQuery q(db);
    q.prepare("INSERT OR REPLACE INTO field_settings VALUES(?,?)");
    q.addBindValue(job);
    q.addBindValue(json(prefs));
    if (!q.exec())
        return message(q.lastError().text());
    workPrefs = prefs;
    rebuildGuide();
    emit fieldChanged();
    emit changed();
    return true;
}
bool Survey::setCoverage(bool record, bool show, double radius) {
    if (!std::isfinite(radius) || radius <= 0 || radius > 1000)
        return message("Enter a sampling radius above zero and no more than 1000 m");
    auto prefs = workPrefs;
    prefs["recordCoverage"] = record;
    prefs["showCoverage"] = show;
    prefs["radiusM"] = radius;
    return saveFieldPrefs(prefs);
}
bool Survey::includeShot(QString expectedJob, QString id, QString gpst, bool include) {
    if (job != expectedJob || job.isEmpty())
        return message("Job changed · reopen the point");
    QSqlQuery q(db);
    q.prepare("SELECT record FROM points WHERE job=? AND id=?");
    q.addBindValue(job);
    q.addBindValue(id);
    if (!q.exec() || !q.next())
        return message("Point not found");
    auto original = q.value(0);
    auto p = QJsonDocument::fromJson(original.toByteArray()).toVariant().toMap();
    if (p.value("gpst") != gpst)
        return message("Point changed · reopen it");
    p["surfaceIncluded"] = include;
    if (include && !p.contains("sampleRadiusM"))
        p["sampleRadiusM"] = workPrefs.value("radiusM", .5);
    q.prepare("UPDATE points SET record=? WHERE job=? AND id=? AND record=?");
    q.addBindValue(json(p));
    q.addBindValue(job);
    q.addBindValue(id);
    q.addBindValue(original);
    if (!q.exec() || q.numRowsAffected() != 1)
        return message("Point was not updated");
    loadPoints();
    emit changed();
    return true;
}
bool Survey::previewTerrain(int epsg, QString datum, double edge) {
    auto result = terrain(pointRows, job, frame, epsg, datum, edge);
    previewXml = result.xml;
    tinPreview = {{"triangles", result.triangles},
                  {"points", result.count},
                  {"gaps", result.gaps},
                  {"epsg", epsg},
                  {"datum", datum},
                  {"edge", edge},
                  {"error", result.error}};
    emit previewChanged();
    return result.error.isEmpty() || message(result.error);
}
bool Survey::exportTerrain(QUrl dest) {
    if (previewXml.isEmpty())
        return message("Preview a valid measured surface first");
    return writeExport(dest, previewXml, "Surveyed ground exported");
}
bool Survey::insertFeatures(const QVariantList &rows) {
    if (job.isEmpty() || rows.isEmpty())
        return message("No lines selected");
    if (!db.transaction())
        return message(db.lastError().text());
    QSqlQuery q(db);
    q.prepare("INSERT INTO feature_lines VALUES(?,?,?)");
    for (auto v : rows) {
        auto row = v.toMap();
        q.bindValue(0, job);
        q.bindValue(1, row["name"]);
        q.bindValue(2, json(row));
        if (!q.exec()) {
            db.rollback();
            if (q.lastError().nativeErrorCode() == "1555" ||
                q.lastError().text().contains("UNIQUE constraint"))
                return message("A feature named “" + row["name"].toString() +
                               "” already exists in this job");
            return message("Lines not saved: " + q.lastError().text());
        }
    }
    if (!db.commit()) {
        db.rollback();
        return message("Could not commit feature lines");
    }
    loadFieldwork();
    message(QString("Saved %1 feature line(s)").arg(rows.size()));
    return true;
}
bool Survey::createLine(QString name, QVariantList selected) {
    name = name.trimmed();
    if (job.isEmpty() || name.isEmpty() || selected.size() < 2)
        return message("Open a job, choose at least two positions and name the line");
    QHash<QString, QVariantMap> byId;
    for (auto v : pointRows) {
        auto p = v.toMap();
        byId.insert(p.value("id").toString(), p);
    }
    QVariantList path;
    QStringList ids;
    int surveyed = 0;
    QVariantMap referenceBase;
    for (const auto &v : selected) {
        auto p = v.toMap();
        auto id = p.value("id").toString();
        if (!id.isEmpty()) {
            auto shot = byId.value(id);
            if (shot.isEmpty() || shot.value("frame") != frame || shot.value("gpst") != p.value("gpst"))
                return message("Point changed or uses another job reference: " + id);
            p = shot;
            ++surveyed;
            if (frame == "Unknown" && p.value("baseSats").toInt() > 0 &&
                !p.value("base").toString().isEmpty()) {
                QVariantMap source{{"base", p.value("base")}, {"station", p.value("station")}};
                if (!referenceBase.isEmpty() && source != referenceBase)
                    return message("These points were measured from different bases");
                referenceBase = source;
            }
        }
        bool okLat, okLon;
        double lat = p.value("latitudeDeg").toDouble(&okLat),
               lon = p.value("longitudeDeg").toDouble(&okLon);
        if (!okLat || !okLon || !std::isfinite(lat) || !std::isfinite(lon) || std::abs(lat) > 90 ||
            std::abs(lon) > 180)
            return message("Invalid map position");
        ids << id;
        path << QVariantMap{{"latitudeDeg", lat}, {"longitudeDeg", lon}};
    }
    static const QRegularExpression declared(R"(\bEPSG\s*:\s*(\d+)\b)",
                                             QRegularExpression::CaseInsensitiveOption);
    int epsg = declared.match(frame).captured(1).toInt();
    if (!Surface::utmZone(epsg) && gradePrefs.value("surfaceFrame") == frame)
        epsg = gradePrefs.value("epsg").toInt();
    if (!Surface::utmZone(epsg))
        epsg = 0;
    auto origin = path.first().toMap();
    auto pj = epsg ? Surface::utmProjection(epsg) : lineProjection({{"localOrigin", origin}});
    if (!pj)
        return message("Cannot calculate local distances");
    QPolygonF xy;
    for (auto v : path)
        xy << project(pj.get(), v.toMap());
    auto row = feature(name,
                       surveyed == selected.size() ? "Survey points"
                       : surveyed                  ? "Points + map sketch"
                                                   : "Map sketch",
                       frame, epsg, pj.get(), xy);
    if (row.isEmpty())
        return message("Invalid or repeated adjacent line positions");
    row["pointIds"] = ids;
    row["path"] = path; // Keep the original selected positions, not an inverse-projection round trip.
    if (!epsg) {
        row.remove("epsg");
        row["localOrigin"] = origin;
    }
    if (!referenceBase.isEmpty())
        row["referenceBase"] = referenceBase;
    return insertFeatures({row});
}
QVariantMap Survey::inspectDxf(QUrl url) {
    pendingDrawing = {};
    QFile file(url.isEmpty()       ? ":/fixtures/practice-trench.dxf"
               : url.isLocalFile() ? url.toLocalFile()
                                   : url.toString());
    if (!file.open(QIODevice::ReadOnly))
        return {{"error", file.errorString()}};
    pendingDrawing = dxf(file.read(16 * 1024 * 1024 + 1));
    QStringList layers;
    QVariantList shapes;
    for (const auto &l : pendingDrawing.lines) {
        if (!layers.contains(l.layer))
            layers << l.layer;
        QVariantList path;
        for (auto p : l.en)
            path << QVariantMap{{"x", p.x()}, {"y", p.y()}};
        shapes << QVariantMap{{"layer", l.layer}, {"path", path}};
    }
    return {
        {"error", pendingDrawing.error},        {"units", pendingDrawing.units},     {"layers", layers},
        {"count", pendingDrawing.lines.size()}, {"skipped", pendingDrawing.skipped}, {"shapes", shapes}};
}
bool Survey::importDxf(QStringList layers, int epsg, QString units) {
    auto pj = Surface::utmProjection(epsg);
    double scale = QMap<QString, double>{{"Metres", 1},
                                         {"Millimetres", .001},
                                         {"Centimetres", .01},
                                         {"Feet", .3048},
                                         {"US survey feet", 1200. / 3937.},
                                         {"Inches", .0254}}
                       .value(units);
    if (!pendingDrawing.error.isEmpty() || !pj || !scale || frame == "Unknown" || job.isEmpty())
        return message("Confirm a valid drawing, known job frame, supported UTM EPSG and drawing units");
    QVariantList rows;
    for (const auto &l : pendingDrawing.lines) {
        if (!layers.contains(l.layer))
            continue;
        QPolygonF xy;
        for (auto p : l.en)
            xy << p * scale;
        auto row = feature(l.name, "DXF", frame, epsg, pj.get(), xy);
        if (row.isEmpty())
            return message("Invalid coordinates for " + l.name);
        row["layer"] = l.layer;
        row["units"] = units;
        rows << row;
    }
    return insertFeatures(rows);
}
void Survey::rebuildGuide() {
    paths.clear();
    guide = {};
    guideProjection.reset();
    auto name = workPrefs.value("follow").toString();
    if (name.isEmpty())
        return;
    for (auto v : featureRows) {
        auto f = v.toMap();
        if (f.value("name") != name)
            continue;
        auto pj = lineProjection(f);
        if (!pj || f.value("frame") != frame)
            return;
        double offset = workPrefs.value("offsetM").toDouble(),
               half = workPrefs.value("widthM").toDouble() / 2;
        guide.source = vertices(f);
        guide.center = parallel(guide.source, offset);
        guide.left = parallel(guide.source, offset - half);
        guide.right = parallel(guide.source, offset + half);
        if (guide.center.isEmpty() || guide.left.isEmpty() || guide.right.isEmpty())
            return;
        guide.target = workPrefs.value("target").toString();
        guide.locked = workPrefs.value("segment").toInt() - 1;
        paths = {{"source", f["path"]},
                 {"center", coordinates(pj.get(), guide.center)},
                 {"left", coordinates(pj.get(), guide.left)},
                 {"right", coordinates(pj.get(), guide.right)}};
        paths["referenceBase"] = f.value("referenceBase");
        guideProjection = std::move(pj);
        return;
    }
}
QVariantMap Survey::previewLine(QString name, double offset, double width) const {
    if (!std::isfinite(offset) || !std::isfinite(width) || width < 0)
        return {};
    for (auto v : featureRows) {
        auto f = v.toMap();
        if (f.value("name") != name)
            continue;
        auto pj = lineProjection(f);
        auto p = vertices(f);
        return {{"source", f["path"]},
                {"center", coordinates(pj.get(), parallel(p, offset))},
                {"left", coordinates(pj.get(), parallel(p, offset - width / 2))},
                {"right", coordinates(pj.get(), parallel(p, offset + width / 2))}};
    }
    return {};
}
bool Survey::followLine(QString name, double offset, double width, QString target, int segment,
                        bool allowFloat) {
    auto prefs = workPrefs;
    if (!name.isEmpty()) {
        if (!std::isfinite(offset) || !std::isfinite(width) || width < 0 ||
            !QStringList{"Centre", "Left edge", "Right edge"}.contains(target) ||
            (target != "Centre" && width == 0))
            return message("Select a valid centre offset, width and guide target");
        QVariantMap f;
        for (auto v : featureRows)
            if (v.toMap().value("name") == name)
                f = v.toMap();
        auto xy = vertices(f);
        if (f.isEmpty() || f.value("frame") != frame || segment < 0 || segment >= xy.size())
            return message("Invalid line, segment or job frame");
        for (double d : {offset, offset - width / 2, offset + width / 2})
            if (parallel(xy, d).isEmpty())
                return message("Offset crosses or spikes at a corner · reduce width or offset");
        prefs["offsetM"] = offset;
        prefs["widthM"] = width;
        prefs["target"] = target;
        prefs["segment"] = segment;
        prefs["lineAllowFloat"] = allowFloat;
    }
    prefs["follow"] = name;
    return saveFieldPrefs(prefs);
}
QVariantMap Survey::lineReading(bool current) const {
    auto name = workPrefs.value("follow").toString();
    if (name.isEmpty())
        return {};
    QVariantMap r{{"name", name}, {"target", guide.target}};
    if (!guideProjection)
        r["status"] = "Feature frame or geometry unavailable";
    else if (!current)
        r["status"] = "Waiting for fresh position";
    else if (auto ref = paths.value("referenceBase").toMap();
             !ref.isEmpty() &&
             (ref.value("base") != live.value("base") || ref.value("station") != live.value("station")))
        r["status"] = "Rover base differs from saved points";
    else if (live.value("quality") != "fixed" &&
             !(live.value("quality") == "float" && workPrefs.value("lineAllowFloat").toBool()))
        r["status"] = "Waiting for qualifying RTK";
    else
        r.insert(guide.measure(project(guideProjection.get(), live)));
    return r;
}
