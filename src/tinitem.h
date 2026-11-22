#pragma once
#include <QQuickPaintedItem>
#include <QImage>
#include <QVariantMap>

class TinItem : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QVariantMap mesh READ mesh WRITE setMesh NOTIFY meshChanged)
    Q_PROPERTY(QVariantMap rover READ rover WRITE setRover NOTIFY roverChanged)
    Q_PROPERTY(QVariantMap view READ view WRITE setView NOTIFY viewChanged)
    QVariantMap geometry, marker, camera;
    QImage cached;

  public:
    using QQuickPaintedItem::QQuickPaintedItem;
    QVariantMap mesh() const { return geometry; }
    QVariantMap rover() const { return marker; }
    QVariantMap view() const { return camera; }
    void setMesh(const QVariantMap &m) {
        geometry = m;
        cached = {};
        update();
        emit meshChanged();
    }
    void setRover(const QVariantMap &r) {
        marker = r;
        update();
        emit roverChanged();
    }
    void setView(const QVariantMap &v) {
        camera = v;
        cached = {};
        update();
        emit viewChanged();
    }
    void paint(QPainter *p) override;
  signals:
    void meshChanged();
    void roverChanged();
    void viewChanged();
};
