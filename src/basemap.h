#pragma once
#include <QFutureWatcher>
#include <QMutex>
#include <QObject>
#include <QQuickImageProvider>
#include <QUrl>
#include <QVariantMap>

class Basemap : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap offlineInfo READ offlineInfo NOTIFY changed)
    Q_PROPERTY(QVariantMap onlineInfo READ onlineInfo NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QStringList events READ events NOTIFY eventsChanged)
  public:
    Basemap();
    ~Basemap();
    QVariantMap offlineInfo() const { return offline; }
    QVariantMap onlineInfo() const {
        QMutexLocker guard(&lock);
        return online;
    }
    QUrl resolveImagery(const QUrl &alias) const;
    QString message() const { return status; }
    bool busy() const { return importing; }
    QStringList events() const { return history; }
    void selectJob(const QString &job);
    Q_INVOKABLE bool importTiles(QUrl url);
    Q_INVOKABLE bool setOnline(QString url, QString attribution, int maxZoom);
    Q_INVOKABLE void note(QString text);
    QImage tile(const QString &id) const;
  signals:
    void changed();
    void eventsChanged();

  private:
    QString jobKey, status, root;
    bool importing = false;
    QVariantMap offline, online;
    QStringList history;
    QFutureWatcher<QVariantMap> worker;
    mutable QMutex lock;
    QString activePath, revision;
    bool xyz = false;
};

class OfflineImages final : public QQuickImageProvider {
    Basemap &maps;

  public:
    explicit OfflineImages(Basemap &m)
        : QQuickImageProvider(Image, ForceAsynchronousImageLoading), maps(m) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &) override {
        auto im = maps.tile(id);
        if (size)
            *size = im.size();
        return im;
    }
};
