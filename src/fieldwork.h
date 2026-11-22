#pragma once
#include "surface.h"
#include <QPolygonF>

namespace Fieldwork {
QPointF project(PJ *utm, const QVariantMap &geo);
QVariantMap geographic(PJ *utm, QPointF en);
struct Line {
    QString name, layer;
    QPolygonF en;
};
struct Drawing {
    QVector<Line> lines;
    QString units, error;
    int skipped = 0;
};
Drawing dxf(const QByteArray &bytes);
QPolygonF parallel(const QPolygonF &source, double right);
struct Guide {
    QPolygonF source, center, left, right;
    QString target;
    int locked = -1;
    QVariantMap measure(QPointF rover) const;
};
struct Terrain {
    QByteArray xml;
    QVariantList triangles;
    QString error;
    int count = 0, gaps = 0;
};
Terrain terrain(const QVariantList &rows, QString job, QString frame, int epsg, QString datum,
                double maxEdge);
} // namespace Fieldwork
