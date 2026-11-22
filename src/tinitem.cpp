#include "tinitem.h"
#include <QPainter>
#include <QtMath>
#include <QVector3D>
#include <algorithm>
#include <numeric>

void TinItem::paint(QPainter *p) {
    if (width() < 2 || height() < 2 || geometry.isEmpty())
        return;
    auto yaw = qDegreesToRadians(camera.value("yaw").toDouble()),
         pitch = qDegreesToRadians(camera.value("pitch", 40).toDouble());
    double extent = std::max(.01, geometry.value("extent").toDouble());
    double scale = .8 * std::min(width(), height()) * camera.value("zoom", 1).toDouble() / extent;
    QVector3D right(std::cos(yaw), -std::sin(yaw), 0),
        toward(std::sin(yaw) * std::cos(pitch), std::cos(yaw) * std::cos(pitch), std::sin(pitch));
    auto up = QVector3D::crossProduct(right, toward);
    auto project = [&](QVector3D v) {
        return QPointF(width() / 2 + scale * QVector3D::dotProduct(v, right),
                       height() / 2 - scale * QVector3D::dotProduct(v, up));
    };
    QSize size(qCeil(width()), qCeil(height()));
    bool day = camera.value("daylight").toBool();
    if (cached.size() != size) {
        cached = QImage(size, QImage::Format_ARGB32_Premultiplied);
        cached.fill(day ? QColor("#e3e9eb") : QColor("#182126"));
        QPainter g(&cached);
        auto vertices = geometry.value("vertices").value<QVector<QVector3D>>();
        auto indices = geometry.value("faces").value<QVector<int>>();
        QVector<QPointF> xy;
        QVector<float> depth;
        xy.reserve(vertices.size());
        depth.reserve(vertices.size());
        for (auto v : vertices) {
            xy << project(v);
            depth << QVector3D::dotProduct(v, toward);
        }
        QVector<int> order(indices.size() / 3);
        std::iota(order.begin(), order.end(), 0);
        auto z = [&](int i) {
            return depth[indices[3 * i]] + depth[indices[3 * i + 1]] + depth[indices[3 * i + 2]];
        };
        std::sort(order.begin(), order.end(), [&](int a, int b) { return z(a) < z(b); });
        g.setPen(QPen(day ? QColor("#435a57") : QColor("#3f5d5f"), .6));
        for (int face : order) {
            int a = indices[3 * face], b = indices[3 * face + 1], c = indices[3 * face + 2];
            QPointF tri[] = {xy[a], xy[b], xy[c]};
            auto normal = QVector3D::normal(vertices[b] - vertices[a], vertices[c] - vertices[a]);
            if (normal.z() < 0)
                normal = -normal;
            double light = .52 + .34 * QVector3D::dotProduct(normal, QVector3D(-.36, .48, .8));
            g.setBrush(QColor::fromRgbF(light * .68, light * .92, light * .84));
            g.drawPolygon(tri, 3);
        }
    }
    p->drawImage(QPointF(0, 0), cached);
    if (marker.contains("up")) {
        auto pos = project(QVector3D(marker.value("east").toDouble(), marker.value("north").toDouble(),
                                     marker.value("up").toDouble()));
        p->setRenderHint(QPainter::Antialiasing);
        p->setPen(QPen(Qt::black, 2));
        p->setBrush(QColor("#ffcc53"));
        p->drawEllipse(pos, 6, 6);
    }
}
