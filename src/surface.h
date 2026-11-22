#pragma once
#include <QVariantMap>
#include <QVector>
#include <QVector3D>
#include <array>
#include <memory>
#include <optional>
#include <proj.h>

struct Surface {
    static int utmZone(int epsg);
    static std::unique_ptr<PJ, decltype(&proj_destroy)> utmProjection(int epsg);
    struct Vertex {
        double e, n, z;
    };
    QStringList names;
    QString name, datum, horizontal;
    int epsg = 0;
    QVector<Vertex> vertices;
    QVector<std::array<int, 3>> faces;
    QVariantList outline; // Geographic endpoint pairs, including hole edges.
    QVariantMap mesh;
    std::unique_ptr<PJ, decltype(&proj_destroy)> projection{nullptr, proj_destroy};
    // With no selection, inspect metadata and names without loading geometry.
    QString read(const QByteArray &xml, const QString &selection = {});
    std::optional<double> target(double lat, double lon) const;
    QVariantMap localPosition(double lat, double lon, double height) const;
};
