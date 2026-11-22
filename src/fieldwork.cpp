#include "fieldwork.h"
#include "../third_party/delaunator/delaunator.hpp"
#include <QDateTime>
#include <QXmlStreamWriter>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <set>

namespace Fieldwork {
static double length(QPointF p) { return std::hypot(p.x(), p.y()); }
static double dot(QPointF a, QPointF b) { return QPointF::dotProduct(a, b); }
QPointF project(PJ *utm, const QVariantMap &geo) {
    if (!utm || !geo.contains("latitudeDeg") || !geo.contains("longitudeDeg"))
        return {NAN, NAN};
    auto p = proj_trans(utm, PJ_FWD,
                        proj_coord(qDegreesToRadians(geo.value("longitudeDeg").toDouble()),
                                   qDegreesToRadians(geo.value("latitudeDeg").toDouble()), 0, 0));
    return {p.xy.x, p.xy.y};
}
QVariantMap geographic(PJ *utm, QPointF en) {
    if (!utm)
        return {};
    auto p = proj_trans(utm, PJ_INV, proj_coord(en.x(), en.y(), 0, 0));
    if (!std::isfinite(p.lp.lam) || !std::isfinite(p.lp.phi))
        return {};
    return {{"longitudeDeg", qRadiansToDegrees(p.lp.lam)}, {"latitudeDeg", qRadiansToDegrees(p.lp.phi)}};
}

// DXF group codes, not the position of a value in the file, identify coordinates.
Drawing dxf(const QByteArray &bytes) {
    Drawing out;
    using Pair = QPair<int, QByteArray>;
    QList<Pair> fields;
    QByteArray type, section;
    Line poly;
    bool building = false, invalid = false, closed = false, ended = false;
    auto finish = [&](Line l, bool bad, bool loop) {
        if (!bad) {
            QPolygonF clean;
            for (auto p : l.en)
                if (clean.isEmpty() || clean.last() != p)
                    clean << p;
            if (loop && !clean.isEmpty() && clean.last() != clean.first())
                clean << clean.first();
            l.en = clean;
        }
        if (bad || l.en.size() < 2)
            out.skipped++;
        else
            out.lines << l;
    };
    auto parse = [&] {
        auto val = [&](int code) {
            for (const auto &f : fields)
                if (f.first == code)
                    return f.second;
            return QByteArray();
        };
        bool bad = false;
        auto number = [&](QByteArray s) {
            bool ok;
            double v = s.toDouble(&ok);
            if (!ok || !std::isfinite(v))
                bad = true;
            return v;
        };
        if (type == "SECTION") {
            section = val(2);
            if (section == "HEADER")
                for (int i = 1; i < fields.size(); i++)
                    if (fields[i - 1] == Pair(9, "$INSUNITS") && fields[i].first == 70) {
                        int u = fields[i].second.toInt();
                        out.units =
                            QMap<int, QString>{{0, "Unitless"},       {1, "Inches"},      {2, "Feet"},
                                               {4, "Millimetres"},    {5, "Centimetres"}, {6, "Metres"},
                                               {21, "US survey feet"}}
                                .value(u, QString("DXF unit code %1").arg(u));
                    }
            return;
        }
        if (type == "EOF") {
            ended = true;
            return;
        }
        if (type == "ENDSEC") {
            if (building) {
                out.skipped++;
                building = false;
            }
            section.clear();
            return;
        }
        if (section != "ENTITIES")
            return;
        if (building && type != "VERTEX" && type != "SEQEND") {
            out.skipped++;
            building = false;
        }
        auto normal = [&](int code, double def) {
            auto v = val(code);
            return v.isEmpty() ? def : number(v);
        };
        bool tilted = normal(210, 0) != 0 || normal(220, 0) != 0 || normal(230, 1) != 1;
        if (type == "VERTEX" && building) {
            poly.en << QPointF(number(val(10)), number(val(20)));
            double bulge = normal(42, 0);
            invalid |= bad || tilted || bulge != 0;
            return;
        }
        if (type == "SEQEND") {
            if (building)
                finish(poly, invalid, closed);
            building = false;
            return;
        }
        auto layer = QString::fromUtf8(val(8));
        if (layer.isEmpty())
            layer = "0";
        Line l{layer + " / " +
                   QString::fromLatin1(
                       val(5).isEmpty() ? QByteArray::number(out.lines.size() + out.skipped + 1) : val(5)),
               layer,
               {}};
        int flags = val(70).toInt();
        bool paper = val(67) == "1" || (!val(410).isEmpty() && val(410).toLower() != "model");
        if (type == "POLYLINE") {
            building = true;
            poly = l;
            invalid = paper || bad || tilted || (flags & (2 | 4 | 16 | 64));
            closed = flags & 1;
            return;
        }
        if (type == "LINE") {
            l.en << QPointF(number(val(10)), number(val(20))) << QPointF(number(val(11)), number(val(21)));
            finish(l, paper || bad || tilted, false);
            return;
        }
        if (type == "LWPOLYLINE") {
            double x = 0;
            bool pending = false;
            for (auto &f : fields) {
                if (f.first == 10) {
                    if (pending)
                        bad = true;
                    x = number(f.second);
                    pending = true;
                } else if (f.first == 20) {
                    if (!pending)
                        bad = true;
                    l.en << QPointF(x, number(f.second));
                    pending = false;
                } else if (f.first == 42 && number(f.second) != 0)
                    bad = true;
            }
            bool ok;
            int declared = val(90).toInt(&ok);
            finish(l, paper || bad || tilted || pending || !ok || declared != l.en.size(), flags & 1);
            return;
        }
        out.skipped++;
    };
    if (bytes.isEmpty() || bytes.contains('\0') || bytes.size() > 16 * 1024 * 1024) {
        out.error = "Use an ASCII DXF up to 16 MiB";
        return out;
    }
    qsizetype pos = 0;
    auto line = [&]() {
        auto end = bytes.indexOf('\n', pos);
        if (end < 0)
            end = bytes.size();
        auto result = bytes.mid(pos, end - pos).trimmed();
        pos = end + 1;
        return result;
    };
    while (pos < bytes.size()) {
        auto key = line();
        if (key.isEmpty() && pos >= bytes.size())
            break;
        bool ok;
        int code = key.toInt(&ok);
        if (!ok || pos >= bytes.size()) {
            out.error = "Malformed DXF group pair";
            break;
        }
        auto v = line();
        if (code == 0) {
            parse();
            fields.clear();
            type = v;
        } else
            fields << Pair(code, v);
    }
    parse();
    if (!ended || !section.isEmpty())
        out.error = "Incomplete DXF";
    if (out.units.isEmpty())
        out.units = "Unspecified";
    return out;
}

QPolygonF parallel(const QPolygonF &p, double offset) {
    if (p.size() < 2 || !std::isfinite(offset))
        return {};
    QVector<QPointF> normals;
    for (int i = 1; i < p.size(); i++) {
        auto v = p[i] - p[i - 1];
        double len = length(v);
        if (!std::isfinite(len) || len < 1e-6)
            return {};
        normals << QPointF(v.y() / len, -v.x() / len);
    }
    if (offset == 0)
        return p;
    bool closed = p.isClosed();
    QPolygonF out;
    for (int i = 0; i < p.size(); i++) {
        auto before = i ? normals[i - 1] : (closed ? normals.last() : normals.first());
        auto after = i < normals.size() ? normals[i] : (closed ? normals.first() : normals.last());
        double denom = 1 + dot(before, after);
        if (denom < .125)
            return {}; // Offset would spike more than four times its specified distance.
        out << p[i] + (before + after) * (offset / denom);
        if (!std::isfinite(length(out.last())))
            return {};
    }
    for (int i = 1; i < p.size(); i++)
        if (dot(out[i] - out[i - 1], p[i] - p[i - 1]) <= 0)
            return {};
    return out;
}
struct Hit {
    int i = -1;
    double signedM = 0, distance = INFINITY, t = 0;
    bool ambiguous = false;
};
static Hit nearest(const QPolygonF &p, QPointF pos, int lock) {
    Hit best;
    auto hit = [&](int i) -> Hit {
        auto v = p[i + 1] - p[i];
        double len = length(v);
        if (len < 1e-6)
            return {};
        double t = dot(pos - p[i], v) / (len * len);
        auto d = pos - (p[i] + std::clamp(t, 0., 1.) * v);
        double m = length(d);
        double sign = v.y() * d.x() - v.x() * d.y();
        return {i, std::copysign(m, sign), m, t, false};
    };
    if (lock >= 0)
        return lock + 1 < p.size() ? hit(lock) : Hit{};
    for (int i = 0; i + 1 < p.size(); i++) {
        auto h = hit(i);
        if (h.distance < best.distance)
            best = h;
    }
    // At a crossing centimetre-scale noise must not silently choose another branch.
    for (int i = 0; i + 1 < p.size(); i++) {
        int separation = std::abs(i - best.i);
        if (separation <= 1 || (p.isClosed() && separation == p.size() - 2))
            continue;
        if (hit(i).distance <= best.distance + .02) {
            best.ambiguous = true;
            break;
        }
    }
    return best;
}
QVariantMap Guide::measure(QPointF rover) const {
    auto &path = target == "Left edge" ? left : target == "Right edge" ? right : center;
    auto h = nearest(path, rover, locked);
    auto base = nearest(source, rover, locked);
    QVariantMap r{{"segment", h.i + 1}};
    if (h.i < 0 || !std::isfinite(length(rover)))
        r["status"] = "Invalid position";
    else if (h.ambiguous || base.ambiguous)
        r["status"] = "Ambiguous crossing · select a segment";
    else if ((locked >= 0 || (!path.isClosed() && h.i == 0)) && h.t < 0)
        r["status"] = "Before start";
    else if ((locked >= 0 || (!path.isClosed() && h.i == path.size() - 2)) && h.t > 1)
        r["status"] = "Past end";
    else {
        r["status"] = "Tracking";
        r["rightM"] = h.signedM;
        r["fromFeatureM"] = base.signedM;
        if (left != right) {
            QPolygonF corridor = left;
            for (auto it = right.crbegin(); it != right.crend(); ++it)
                corridor << *it;
            r["inside"] = corridor.containsPoint(rover, Qt::OddEvenFill) ||
                          nearest(left, rover, locked).distance < 1e-6 ||
                          nearest(right, rover, locked).distance < 1e-6;
        }
    }
    return r;
}

Terrain terrain(const QVariantList &rows, QString job, QString frame, int epsg, QString datum,
                double maxEdge) {
    Terrain t;
    auto pj = Surface::utmProjection(epsg);
    if (!pj || frame.trimmed().isEmpty() || frame == "Unknown" || !std::isfinite(maxEdge) || maxEdge <= 0 ||
        (datum != "CGVD2013" && datum != "Ellipsoid")) {
        t.error = "Confirm a known job frame, supported UTM EPSG, datum and positive maximum edge";
        return t;
    }
    QVector<QPointF> xy;
    QVector<double> heights;
    QVariantList geo;
    std::vector<double> coords;
    std::set<std::pair<double, double>> unique;
    for (auto &v : rows) {
        auto p = v.toMap();
        if (!p.value("surfaceIncluded").toBool())
            continue;
        auto z = p.value(datum == "CGVD2013" ? "groundCgvd2013M" : "groundEllipsoidM");
        auto en = project(pj.get(), p);
        if (p.value("frame") != frame || !z.isValid() || !std::isfinite(z.toDouble()) ||
            !std::isfinite(length(en))) {
            t.error = "Incompatible frame or missing ground elevation at " + p.value("id").toString();
            return t;
        }
        if (!unique.emplace(en.x(), en.y()).second) {
            t.error =
                "Duplicate horizontal position at " + p.value("id").toString() + " · exclude one shot";
            return t;
        }
        xy << en;
        heights << z.toDouble();
        geo << QVariantMap{{"latitudeDeg", p.value("latitudeDeg")},
                           {"longitudeDeg", p.value("longitudeDeg")}};
        coords.push_back(en.x());
        coords.push_back(en.y());
    }
    t.count = xy.size();
    if (t.count < 3) {
        t.error = "Include at least three noncollinear points";
        return t;
    }
    QList<std::array<int, 3>> faces;
    try {
        delaunator::Delaunator d(coords);
        for (size_t i = 0; i < d.triangles.size(); i += 3) {
            int a = d.triangles[i], b = d.triangles[i + 1], c = d.triangles[i + 2];
            const double longest =
                std::max({length(xy[a] - xy[b]), length(xy[b] - xy[c]), length(xy[c] - xy[a])});
            auto ab = xy[b] - xy[a], ac = xy[c] - xy[a];
            if (longest > maxEdge || std::abs(ab.x() * ac.y() - ab.y() * ac.x()) <= longest * 1e-6) {
                t.gaps++;
                continue;
            }
            faces.append({a, b, c});
            t.triangles.append(QVariant(QVariantList{geo[a], geo[b], geo[c]}));
        }
    } catch (const std::exception &) {
        t.error = "Cannot triangulate these positions (collinear or degenerate)";
        return t;
    }
    if (faces.isEmpty()) {
        t.error = "No triangles within the maximum edge length";
        return t;
    }
    QXmlStreamWriter w(&t.xml);
    w.setAutoFormatting(true);
    w.writeStartDocument();
    w.writeStartElement("LandXML");
    w.writeDefaultNamespace("http://www.landxml.org/schema/LandXML-1.2");
    w.writeAttribute("version", "1.2");
    auto now = QDateTime::currentDateTimeUtc();
    w.writeAttribute("date", now.date().toString(Qt::ISODate));
    w.writeAttribute("time", now.time().toString(Qt::ISODate));
    w.writeStartElement("Units");
    w.writeEmptyElement("Metric");
    w.writeAttribute("linearUnit", "meter");
    w.writeAttribute("areaUnit", "squareMeter");
    w.writeAttribute("volumeUnit", "cubicMeter");
    w.writeAttribute("temperatureUnit", "celsius");
    w.writeAttribute("pressureUnit", "milliBars");
    w.writeEndElement();
    w.writeEmptyElement("CoordinateSystem");
    w.writeAttribute("epsgCode", QString::number(epsg));
    w.writeAttribute("horizontalCoordinateSystemName",
                     QString("NAD83(CSRS) / UTM zone %1N").arg(Surface::utmZone(epsg)));
    w.writeAttribute("verticalCoordinateSystemName", datum);
    w.writeStartElement("Surfaces");
    w.writeStartElement("Surface");
    w.writeAttribute("name", job + " - surveyed ground");
    w.writeAttribute("desc", "Measured ground; reference: " + frame);
    w.writeStartElement("Definition");
    w.writeAttribute("surfType", "TIN");
    w.writeStartElement("Pnts");
    QVector<bool> used(t.count, false);
    for (auto f : faces)
        for (int id : f)
            used[id] = true;
    for (int i = 0; i < t.count; i++) {
        if (!used[i])
            continue;
        w.writeStartElement("P");
        w.writeAttribute("id", QString::number(i + 1));
        w.writeCharacters(QString::number(xy[i].y(), 'g', 16) + " " + QString::number(xy[i].x(), 'g', 16) +
                          " " + QString::number(heights[i], 'g', 16));
        w.writeEndElement();
    }
    w.writeEndElement();
    w.writeStartElement("Faces");
    for (auto f : faces)
        w.writeTextElement("F", QString("%1 %2 %3").arg(f[0] + 1).arg(f[1] + 1).arg(f[2] + 1));
    w.writeEndDocument();
    return t;
}
} // namespace Fieldwork
