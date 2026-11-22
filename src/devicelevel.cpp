#include "devicelevel.h"
#include <QGuiApplication>
#include <QSettings>
#include <QTimer>
#include <QtMath>
#include <algorithm>
#include <cmath>

DeviceLevel::DeviceLevel() {
    present = !QSensor::sensorsForType(QAccelerometer::sensorType).isEmpty() && sensor.connectToBackend();
    if (!present)
        return;
    if (sensor.isFeatureSupported(QSensor::AccelerationMode))
        sensor.setAccelerationMode(QAccelerometer::Gravity);
    if (sensor.isFeatureSupported(QSensor::AxesOrientation))
        sensor.setAxesOrientationMode(QSensor::AutomaticOrientation);
    sensor.setDataRate(20);
    sensor.setSkipDuplicates(false);
    loadReference();
    connect(&sensor, &QSensor::currentOrientationChanged, this, [this] {
        invalidate();
        loadReference();
        emit changed();
    });
    connect(&sensor, &QSensor::readingChanged, this, &DeviceLevel::sample);
    connect(&sensor, &QSensor::sensorError, this, [this] {
        invalidate();
        emit changed();
    });
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state == Qt::ApplicationSuspended || state == Qt::ApplicationHidden) {
            sensor.stop();
            invalidate();
            emit changed();
        } else if (state == Qt::ApplicationActive && requested)
            sensor.start();
    });
    auto timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this] {
        if (last.isValid() && !fresh()) {
            invalidate();
            emit changed();
        }
    });
    timer->start(300);
}
void DeviceLevel::invalidate() {
    last.invalidate();
    steady.invalidate();
    gravity = {};
    anchor = {};
}
void DeviceLevel::setActive(bool on) {
    if (on == requested)
        return;
    requested = on;
    invalidate();
    if (present) {
        if (on)
            sensor.start();
        else
            sensor.stop();
    }
    emit changed();
}
void DeviceLevel::loadReference() {
    calibrationKey = "level/" + QString::fromLatin1(sensor.identifier().toHex()) + "/" +
                     QString::number(sensor.currentOrientation());
    reference = QSettings().value(calibrationKey).value<QVector3D>();
    if (!std::isfinite(reference.length()) || reference.length() < .9 || reference.length() > 1.1)
        reference = {};
}
QPointF DeviceLevel::angles(QVector3D g, QVector3D ref) {
    // Project gravity onto the tangent plane at the plumb calibration, including upright mounts.
    g.normalize();
    ref.normalize();
    auto right = QVector3D(1, 0, 0) - ref * ref.x();
    if (right.length() < .1)
        right = QVector3D(0, 0, 1) - ref * ref.z();
    right.normalize();
    auto up = QVector3D::crossProduct(ref, right);
    double x = QVector3D::dotProduct(g, right), y = QVector3D::dotProduct(g, up), len = std::hypot(x, y);
    double angle = qRadiansToDegrees(std::atan2(len, double(QVector3D::dotProduct(g, ref))));
    return len < 1e-7 ? QPointF(0, angle) : QPointF(x * angle / len, y * angle / len);
}
void DeviceLevel::sample() {
    if (!requested || !sensor.reading())
        return;
    auto *r = sensor.reading();
    QVector3D g(r->x(), r->y(), r->z());
    if (!std::isfinite(g.length()) || std::abs(g.length() - 9.80665) > 1.2) {
        invalidate();
        emit changed();
        return;
    }
    g.normalize();
    if (!steady.isValid() || (g - anchor).length() > .008) {
        anchor = g;
        steady.restart();
    }
    gravity = !fresh() ? g : (gravity * .75f + g * .25f).normalized();
    last.restart();
    tilt = angles(gravity, calibrated() ? reference : QVector3D(0, 0, 1));
    emit changed();
}
bool DeviceLevel::zero() {
    if (!fresh() || !steady.isValid() || steady.elapsed() < 900)
        return false;
    reference = gravity;
    QSettings().setValue(calibrationKey, reference);
    tilt = {};
    emit changed();
    return true;
}
void DeviceLevel::reset() {
    if (!calibrationKey.isEmpty())
        QSettings().remove(calibrationKey);
    reference = {};
    if (fresh())
        tilt = angles(gravity, QVector3D(0, 0, 1));
    emit changed();
}
QString DeviceLevel::status() const {
    if (!supported())
        return "No tilt sensor";
    if (!requested)
        return "Device level off";
    if (!fresh())
        return "Waiting for stable gravity reading";
    if (!steady.isValid() || steady.elapsed() < 900)
        return "Hold stationary to calibrate";
    return calibrated() ? "Relative to plumb calibration" : "Device level · not calibrated to pole";
}
