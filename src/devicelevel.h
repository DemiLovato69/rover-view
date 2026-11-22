#pragma once
#include <QAccelerometer>
#include <QElapsedTimer>
#include <QPointF>
#include <QVector3D>

class DeviceLevel : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool supported READ supported CONSTANT)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY changed)
    Q_PROPERTY(bool fresh READ fresh NOTIFY changed)
    Q_PROPERTY(bool calibrated READ calibrated NOTIFY changed)
    Q_PROPERTY(double roll READ roll NOTIFY changed)
    Q_PROPERTY(double pitch READ pitch NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    QAccelerometer sensor;
    QElapsedTimer last, steady;
    QVector3D gravity, anchor, reference;
    QPointF tilt;
    QString calibrationKey;
    bool present = false, requested = false;
    void sample();
    void loadReference();
    void invalidate();

  public:
    DeviceLevel();
    bool supported() const { return present; }
    bool active() const { return requested; }
    bool fresh() const { return requested && last.isValid() && last.elapsed() < 1200; }
    bool calibrated() const { return !reference.isNull(); }
    double roll() const { return tilt.x(); }
    double pitch() const { return tilt.y(); }
    QString status() const;
    void setActive(bool on);
    Q_INVOKABLE bool zero();
    Q_INVOKABLE void reset();
    static QPointF angles(QVector3D gravity, QVector3D reference);
  signals:
    void changed();
};
