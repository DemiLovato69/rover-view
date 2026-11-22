#include "surface.h"
#include <QXmlStreamReader>
#include <QHash>
#include <QtMath>
#include <algorithm>
#include <cmath>

static constexpr int zones[] = {3154, 3155, 3156, 3157, 2955, 2956, 2957, 3158, 3159,
                                3160, 2958, 2959, 2960, 2961, 2962, 3761, 9709, 9713};

QString Surface::read(const QByteArray &xml, const QString &selection) {
    *this = Surface();
    QXmlStreamReader x(xml);
    QHash<int, int> index;
    QStringList path;
    bool selected = false, tin = false, meters = false;
    while (!x.atEnd()) {
        auto token = x.readNext();
        if (token == QXmlStreamReader::DTD)
            return "LandXML DTDs are not supported";
        if (token == QXmlStreamReader::EndElement) {
            path.removeLast();
            continue;
        }
        if (!x.isStartElement())
            continue;
        auto tag = x.name();
        auto attrs = x.attributes();
        path << tag.toString();
        if (path.size() > 40)
            return "Invalid LandXML nesting";
        if (path.size() == 1 && tag != u"LandXML")
            return "Expected a LandXML document";
        if (path.size() == 3 && path[1] == "Units")
            meters = tag == u"Metric" && attrs.value("linearUnit") == u"meter";
        if (path.size() == 2 && tag == u"CoordinateSystem") {
            epsg = attrs.value("epsgCode").toInt();
            horizontal = attrs.value("horizontalCoordinateSystemName").toString();
            datum = attrs.value("verticalCoordinateSystemName").toString().trimmed();
            if (datum.compare("Ellipsoidal", Qt::CaseInsensitive) == 0)
                datum = "Ellipsoid";
        }
        if (path.size() == 3 && path[1] == "Surfaces" && tag == u"Surface") {
            auto label = attrs.value("name").toString();
            if (label.trimmed().isEmpty() || names.contains(label))
                return "Surfaces need distinct, non-empty names";
            names << label;
            selected = !selection.isEmpty() && selection == label;
            tin = false;
            if (selected)
                name = label;
        }
        if (!selected || path.size() < 4 || path[2] != "Surface")
            continue;
        if (tag == u"Boundaries")
            return "Explicit clipping boundaries are unsupported; export a clipped TIN";
        if (path.size() == 4 && tag == u"Definition") {
            if (tin || attrs.value("surfType") != u"TIN")
                return "Expected one TIN definition";
            tin = true;
        }
        if (!tin || path.size() != 6 || path[3] != "Definition")
            continue;
        if (path[4] == "Pnts" && tag == u"P") {
            bool ok;
            int id = attrs.value("id").toInt(&ok);
            if (!ok || index.contains(id))
                return "Invalid or duplicate TIN point ID";
            auto coords = x.readElementText().simplified().split(' ');
            path.removeLast();
            if (coords.size() != 3)
                return "Expected northing, easting and elevation";
            double xyz[3];
            for (int i = 0; i < 3; i++) {
                xyz[i] = coords[i].toDouble(&ok);
                if (!ok || !std::isfinite(xyz[i]))
                    return "Invalid TIN coordinate";
            }
            index[id] = vertices.size();
            vertices.append({xyz[1], xyz[0], xyz[2]});
        } else if (path[4] == "Faces" && tag == u"F") {
            bool hidden = attrs.value("i") == u"1" || attrs.value("i") == u"true";
            auto ids = x.readElementText().simplified().split(' ');
            path.removeLast();
            if (hidden)
                continue;
            if (ids.size() != 3)
                return "Expected triangular faces";
            std::array<int, 3> f;
            for (int i = 0; i < 3; i++) {
                bool ok;
                int id = ids[i].toInt(&ok);
                if (!ok || !index.contains(id))
                    return "Face references a missing vertex";
                f[i] = index[id];
            }
            auto a = vertices[f[0]], b = vertices[f[1]], c = vertices[f[2]];
            if (std::abs((b.e - a.e) * (c.n - a.n) - (b.n - a.n) * (c.e - a.e)) < 1e-12)
                return "Degenerate triangle";
            faces.append(f);
        }
    }
    if (x.hasError())
        return x.errorString();
    if (!meters)
        return "Expected Metric linearUnit=meter";
    if (names.isEmpty())
        return "No named surfaces in file";
    auto zone = std::find(std::begin(zones), std::end(zones), epsg);
    if (zone == std::end(zones))
        return "Expected a NAD83(CSRS) UTM EPSG code (zones 7–24N)";
    if (datum != "CGVD2013" && datum != "Ellipsoid")
        return "Expected verticalCoordinateSystemName CGVD2013 or Ellipsoid";
    if (selection.isEmpty())
        return {};
    if (faces.isEmpty())
        return "Selected surface has no visible triangles";
    // Use the EPSG's projection directly: offline Android builds need no PROJ database.
    auto spec = QString("+proj=utm +zone=%1 +ellps=GRS80").arg(7 + (zone - std::begin(zones))).toLatin1();
    projection.reset(proj_create(nullptr, spec.constData()));
    if (!projection)
        return "Cannot initialize UTM projection";
    QHash<QPair<int, int>, int> edges;
    for (auto f : faces)
        for (int i = 0; i < 3; i++) {
            int a = f[i], b = f[(i + 1) % 3];
            if (++edges[{std::min(a, b), std::max(a, b)}] > 2)
                return "Non-manifold TIN edge";
        }
    for (auto it = edges.cbegin(); it != edges.cend(); ++it)
        if (it.value() == 1) {
            for (int idx : {it.key().first, it.key().second}) {
                auto v = vertices[idx];
                auto p = proj_trans(projection.get(), PJ_INV, proj_coord(v.e, v.n, 0, 0));
                if (!std::isfinite(p.lp.lam) || !std::isfinite(p.lp.phi))
                    return "Invalid projected coordinates";
                outline << QVariantMap{{"latitudeDeg", qRadiansToDegrees(p.lp.phi)},
                                       {"longitudeDeg", qRadiansToDegrees(p.lp.lam)}};
            }
        }
    // Center before converting to floats: full UTM northings lose centimetres in a float.
    auto lo = vertices[faces.first()[0]], hi = lo;
    for (const auto &f : faces)
        for (int id : f) {
            auto v = vertices[id];
            lo.e = std::min(lo.e, v.e);
            lo.n = std::min(lo.n, v.n);
            lo.z = std::min(lo.z, v.z);
            hi.e = std::max(hi.e, v.e);
            hi.n = std::max(hi.n, v.n);
            hi.z = std::max(hi.z, v.z);
        }
    Vertex center{(lo.e + hi.e) / 2, (lo.n + hi.n) / 2, (lo.z + hi.z) / 2};
    QVector<QVector3D> local;
    local.reserve(vertices.size());
    for (auto v : vertices)
        local.append(QVector3D(v.e - center.e, v.n - center.n, v.z - center.z));
    QVector<int> indices;
    indices.reserve(faces.size() * 3);
    for (auto f : faces)
        for (int i : f)
            indices << i;
    mesh = {{"vertices", QVariant::fromValue(local)},
            {"faces", QVariant::fromValue(indices)},
            {"centerE", center.e},
            {"centerN", center.n},
            {"centerZ", center.z},
            {"extent", std::max({hi.e - lo.e, hi.n - lo.n, hi.z - lo.z, .01})}};
    return {};
}

QVariantMap Surface::localPosition(double lat, double lon, double height) const {
    if (!projection || mesh.isEmpty() || !std::isfinite(height))
        return {};
    auto p = proj_trans(projection.get(), PJ_FWD,
                        proj_coord(qDegreesToRadians(lon), qDegreesToRadians(lat), 0, 0));
    if (!std::isfinite(p.xy.x) || !std::isfinite(p.xy.y))
        return {};
    return {{"east", p.xy.x - mesh.value("centerE").toDouble()},
            {"north", p.xy.y - mesh.value("centerN").toDouble()},
            {"up", height - mesh.value("centerZ").toDouble()}};
}

std::optional<double> Surface::target(double lat, double lon) const {
    if (!projection)
        return {};
    auto p = proj_trans(projection.get(), PJ_FWD,
                        proj_coord(qDegreesToRadians(lon), qDegreesToRadians(lat), 0, 0));
    for (auto f : faces) {
        auto a = vertices[f[0]], b = vertices[f[1]], c = vertices[f[2]];
        double dx = p.xy.x - a.e, dy = p.xy.y - a.n, be = b.e - a.e, bn = b.n - a.n, ce = c.e - a.e,
               cn = c.n - a.n;
        double det = be * cn - bn * ce, u = (dx * cn - dy * ce) / det, v = (dy * be - dx * bn) / det;
        if (u >= -1e-10 && v >= -1e-10 && u + v <= 1 + 1e-10)
            return a.z + u * (b.z - a.z) + v * (c.z - a.z);
    }
    return {};
}
