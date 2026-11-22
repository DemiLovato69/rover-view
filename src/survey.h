#pragma once
#include "link.h"
#include "surface.h"
#include "fieldwork.h"
#include <QElapsedTimer>
#include <QSqlDatabase>
#include <QUrl>

class Survey : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap state READ state NOTIFY changed)
    Q_PROPERTY(QStringList ports READ ports NOTIFY portsChanged)
    Q_PROPERTY(QVariantList jobs READ jobs NOTIFY jobsChanged)
    Q_PROPERTY(QVariantList points READ points NOTIFY pointsChanged)
    Q_PROPERTY(QVariantList surfaceOutline READ surfaceOutline NOTIFY gradeSetupChanged)
    Q_PROPERTY(QVariantMap surfaceMesh READ surfaceMesh NOTIFY gradeSetupChanged)
    Q_PROPERTY(QStringList events READ events NOTIFY eventsChanged)
    Q_PROPERTY(QVariantMap fieldPrefs READ fieldPrefs NOTIFY fieldChanged)
    Q_PROPERTY(QVariantList features READ features NOTIFY fieldChanged)
    Q_PROPERTY(QVariantMap guidePaths READ guidePaths NOTIFY fieldChanged)
    Q_PROPERTY(QVariantMap terrainPreview READ terrainPreview NOTIFY previewChanged)
    QVariantMap workPrefs, paths, tinPreview;
    QVariantList featureRows;
    QByteArray previewXml;
    Fieldwork::Drawing pendingDrawing;
    Fieldwork::Guide guide;
    std::unique_ptr<PJ, decltype(&proj_destroy)> guideProjection{nullptr, proj_destroy};
    void loadFieldwork();
    void rebuildGuide();
    bool saveFieldPrefs(const QVariantMap &prefs);
    bool insertFeatures(const QVariantList &rows);
    QVariantMap lineReading(bool current) const;
    bool writeExport(QUrl dest, const QByteArray &data, QString success);
    Surface surface;
    QVariantMap gradePrefs;
    QByteArray pendingXml, savedXml;
    void loadGrade();
    bool persistGrade(QVariantMap prefs, const QByteArray &xml);
    QVariantMap guidance(const QVariantMap &point, bool fresh) const;
    QSqlDatabase db;
    Link *link = nullptr;
    QString job, antenna, frame, rx = "Disconnected", base = "Offline", notice;
    double pole = 0;
    QVariantMap live, occupation;
    QVariantList samples, jobRows, pointRows;
    QStringList devices;
    QElapsedTimer fresh;
    qint64 displayedAge = -1;
    QStringList logLines;
    QString lastQuality;
    void log(QString channel, QString text);
    bool valid = false;
    qint64 lastAccepted = 0, elapsedMs = 0;
    QString lastPointEpoch;
    QHash<QString, QString> deletedLiveEpoch;
    void loadJobs();
    void loadPoints();
    void receive(QVariantMap p);
    void interruptOccupation(QString reason);
    bool save(QString id, QString code, QVariantMap p, bool benchmark);
    bool message(QString s);
    bool startLink(QVariantMap config);

  public:
    Survey();
    ~Survey();
    QVariantMap state() const;
    QStringList ports() const { return devices; }
    QVariantList jobs() const { return jobRows; }
    QVariantList points() const { return pointRows; }
    Q_INVOKABLE void scan();
    Q_INVOKABLE bool createJob(QString name, QString model, double height, QString reference,
                               bool updateSetup = false);
    Q_INVOKABLE void openJob(QString name);
    Q_INVOKABLE bool start(QString port, QString url, QString user, QString password);
    Q_INVOKABLE bool practice();
    Q_INVOKABLE bool gradingWalk();
    Q_INVOKABLE QVariantMap inspectSurface(QUrl url);
    Q_INVOKABLE bool setGrade(QString mode, double elevation, QString datum, double tolerance,
                              bool allowFloat, QString selection);
    QVariantList surfaceOutline() const { return surface.outline; }
    QVariantMap surfaceMesh() const { return surface.mesh; }
    QStringList events() const { return logLines; }
    Q_INVOKABLE QVariantMap controlCheck(QString id) const;
    QString jobName() const { return job; }
    Q_INVOKABLE void stop();
    Q_INVOKABLE bool store(QString id, QString code);
    Q_INVOKABLE bool deletePoint(QString expectedJob, QString id, QString gpst);
    Q_INVOKABLE bool occupy(QString id, QString code, int seconds, bool allowFloat);
    Q_INVOKABLE bool finish();
    Q_INVOKABLE void discard();
    Q_INVOKABLE bool exportCsv(QUrl destination);
    QVariantMap fieldPrefs() const { return workPrefs; }
    QVariantList features() const { return featureRows; }
    QVariantMap guidePaths() const { return paths; }
    QVariantMap terrainPreview() const { return tinPreview; }
    Q_INVOKABLE bool setCoverage(bool record, bool show, double radius);
    Q_INVOKABLE bool includeShot(QString expectedJob, QString id, QString gpst, bool include);
    Q_INVOKABLE bool previewTerrain(int epsg, QString datum, double maxEdge);
    Q_INVOKABLE bool exportTerrain(QUrl destination);
    Q_INVOKABLE bool createLine(QString name, QVariantList vertices);
    Q_INVOKABLE QVariantMap inspectDxf(QUrl file);
    Q_INVOKABLE bool importDxf(QStringList layers, int epsg, QString units);
    Q_INVOKABLE bool followLine(QString name, double offset, double width, QString target, int segment,
                                bool allowFloat);
    Q_INVOKABLE QVariantMap previewLine(QString name, double offset, double width) const;
    Q_INVOKABLE QVariantList epochsFor(QString id) const;
  signals:
    void changed();
    void jobsChanged();
    void pointsChanged();
    void portsChanged();
    void gradeSetupChanged();
    void jobChanged();
    void eventsChanged();
    void fieldChanged();
    void previewChanged();
};
