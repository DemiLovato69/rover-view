#pragma once
#include <QThread>
#include <QVariantMap>

class Link : public QThread {
    Q_OBJECT
    QVariantMap config;

  public:
    explicit Link(QVariantMap cfg) : config(std::move(cfg)) { setStackSize(4 * 1024 * 1024); }
    static QStringList ports();
    void run() override;
  signals:
    void receiver(QString text);
    void corrections(QString text);
    void epoch(QVariantMap point);
    void boundary();
};
