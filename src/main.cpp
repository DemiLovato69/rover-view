#include "survey.h"
#include "basemap.h"
#include "devicelevel.h"
#include "tinitem.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTimer>
#include <QQmlNetworkAccessManagerFactory>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QStandardPaths>

class MapNetwork : public QNetworkAccessManager {
    Basemap &maps;

  public:
    MapNetwork(Basemap &m, QObject *parent) : QNetworkAccessManager(parent), maps(m) {}
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &request, QIODevice *body) override {
        auto r = request;
        if (r.url().host() == "raster.fieldrover.invalid")
            r.setUrl(maps.resolveImagery(r.url()));
        if (r.url().scheme() == "https") {
            r.setRawHeader("User-Agent", "FieldRover/0.1 (Qt survey collector)");
            r.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
            r.setTransferTimeout(8000);
            if (r.url().hasQuery()) {
                r.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
                r.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
            }
        }
        return QNetworkAccessManager::createRequest(op, r, body);
    }
};
class TileNetworkFactory : public QQmlNetworkAccessManagerFactory {
    Basemap &maps;

  public:
    explicit TileNetworkFactory(Basemap &m) : maps(m) {}
    QNetworkAccessManager *create(QObject *parent) override {
        auto net = new MapNetwork(maps, parent);
        QObject::connect(net, &QNetworkAccessManager::finished, net, [this](QNetworkReply *reply) {
            if (reply->url().scheme() != "https" || reply->error() == QNetworkReply::OperationCanceledError)
                return;
            auto http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
            QString channel = reply->url().host() == "tile.openstreetmap.org" ? "Street" : "Imagery";
            QString msg =
                channel + (http.isValid() ? QString(" · HTTP %1").arg(http.toInt())
                                          : QString(" · network error %1").arg(int(reply->error())));
            if (reply->error() == QNetworkReply::NoError) {
                if (!reply->header(QNetworkRequest::ContentTypeHeader).toString().contains("text/"))
                    return;
                msg = channel + " · server returned text instead of an image";
            }
            QMetaObject::invokeMethod(&maps, [m = &maps, msg] { m->note(msg); }, Qt::QueuedConnection);
        });
        auto cache = new QNetworkDiskCache(net);
        cache->setCacheDirectory(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
                                 "/map-tiles");
        cache->setMaximumCacheSize(64 * 1024 * 1024);
        net->setCache(cache);
        return net;
    }
};

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    app.setOrganizationName("FieldRover");
    app.setApplicationName("FieldRover");
    QQuickStyle::setStyle("Material");
    Survey survey;
    DeviceLevel level;
    Basemap maps;
    maps.selectJob(survey.jobName());
    QObject::connect(&survey, &Survey::jobChanged, &maps, [&] { maps.selectJob(survey.jobName()); });
    qmlRegisterType<TinItem>("Rover.Native", 1, 0, "TinItem");
    TileNetworkFactory tiles(maps);
    QQmlApplicationEngine engine;
    engine.setNetworkAccessManagerFactory(&tiles);
    engine.addImageProvider("offline", new OfflineImages(maps));
    engine.rootContext()->setContextProperty("basemap", &maps);
    engine.rootContext()->setContextProperty("deviceLevel", &level);
    engine.rootContext()->setContextProperty("collector", &survey);
    engine.loadFromModule("FieldRover", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;
    QObject::connect(&app, &QGuiApplication::applicationStateChanged, &survey, [&](auto state) {
        if (state == Qt::ApplicationSuspended)
            survey.stop();
    });
    return app.exec();
}
