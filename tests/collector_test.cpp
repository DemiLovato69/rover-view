#include "survey.h"
#include "basemap.h"
#include "devicelevel.h"
#include "tinitem.h"
#include "solver.h"
#include <QtTest>
#include <QProcess>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlAbstractUrlInterceptor>
#include <QQuickWindow>
#include <QQuickItem>
#include <QQuickStyle>
#include <QtEndian>
#include <QSqlQuery>
#include <QJsonDocument>
#include <QBuffer>
#include <QPainter>
#include <QQmlComponent>
#ifdef Q_OS_UNIX
#include <signal.h>
#endif

class CollectorTest : public QObject {
    Q_OBJECT
    QTemporaryDir temp;
  private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName("FieldRoverTest");
        QCoreApplication::setApplicationName(QUuid::createUuid().toString(QUuid::WithoutBraces));
        QStandardPaths::setTestModeEnabled(true);
        QQuickStyle::setStyle("Material");
        qmlRegisterType<TinItem>("Rover.Native", 1, 0, "TinItem");
    }
    void cleanupTestCase() {
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).removeRecursively();
    }
    void coordinates() {
        QVariantMap p{{"latitudeDeg", 45.28558}, {"longitudeDeg", -75.93252}, {"ellipsoidM", 81.2}};
        addHeights(p, 2);
        QVERIFY(std::abs(p["groundCgvd2013M"].toDouble() - 112.07499) < .001);
        QVariantMap a{{"x", 6378137.}, {"y", -1.}, {"z", 2.}, {"quality", "float"}}, b = a;
        b["y"] = 1.;
        b["z"] = -2.;
        b["quality"] = "fixed";
        auto mean = average({a, b});
        QCOMPARE(mean["quality"].toString(), "mixed float/fixed");
        QVERIFY(std::abs(mean["horizontalRmsM"].toDouble() - std::sqrt(5.)) < 1e-8);
        QCOMPARE(mean["verticalRmsM"].toDouble(), 0.);
    }
    void trenchSidesAndEnds() {
        Fieldwork::Guide g;
        g.source = {{0, 0}, {0, 10}, {5, 15}};
        g.center = Fieldwork::parallel(g.source, .5);
        g.left = Fieldwork::parallel(g.source, -.1);
        g.right = Fieldwork::parallel(g.source, 1.1);
        QCOMPARE(g.center[0], QPointF(.5, 0));
        auto r = g.measure({.8, 5});
        QCOMPARE(r["segment"].toInt(), 1);
        QVERIFY(std::abs(r["rightM"].toDouble() - .3) < 1e-10);
        QVERIFY(std::abs(r["fromFeatureM"].toDouble() - .8) < 1e-10);
        QVERIFY(r["inside"].toBool());
        g.target = "Right edge";
        QVERIFY(std::abs(g.measure({.8, 5})["rightM"].toDouble() + .3) < 1e-10);
        QVERIFY(!g.measure({1.5, 5})["inside"].toBool());
        QCOMPARE(g.measure({.8, -2})["status"].toString(), "Before start");
        QCOMPARE(g.measure({10, 20})["status"].toString(), "Past end");
        g.source = {{0, 0}, {2, 2}, {0, 2}, {2, 0}};
        g.center = g.left = g.right = g.source;
        QVERIFY(g.measure({1, 1})["status"].toString().startsWith("Ambiguous"));
        g.locked = 2;
        QCOMPARE(g.measure({1, 1})["segment"].toInt(), 3);
        QCOMPARE(g.measure({1, 1})["rightM"].toDouble(), 0.);
        QVERIFY(Fieldwork::parallel({{0, 0}, {1, 0}, {0, 0}}, 1).isEmpty());
    }
    void dxfEntities() {
        QFile fixture(":/fixtures/practice-trench.dxf");
        QVERIFY(fixture.open(QIODevice::ReadOnly));
        auto bytes = fixture.readAll();
        auto d = Fieldwork::dxf(bytes);
        QVERIFY(d.error.isEmpty());
        QCOMPARE(d.units, "Metres");
        QCOMPARE(d.lines.size(), 2);
        QCOMPARE(d.lines[0].en.size(), 3);
        bytes.replace("90\n3\n", "42\n0.4\n90\n3\n");
        bytes.replace("0\nLINE\n", "0\nARC\n8\nCURVES\n0\nPOLYLINE\n8\nString\n70\n8\n0\nVERTEX\n10\n1\n20"
                                   "\n2\n30\n50\n0\nVERTEX\n10\n4\n20\n7\n30\n58\n0\nSEQEND\n0\nLINE\n");
        d = Fieldwork::dxf(bytes);
        QVERIFY(d.error.isEmpty());
        QCOMPARE(d.skipped, 2);
        QCOMPARE(d.lines.size(), 2);
        QCOMPARE(d.lines.first().en.last(), QPointF(4, 7));
        QVERIFY(!Fieldwork::dxf(bytes.left(bytes.size() - 6)).error.isEmpty());
    }
    void measuredGroundRoundTrip() {
        QVariantList rows;
        auto pj = Surface::utmProjection(2959);
        for (auto xy : QPolygonF{{0, 0}, {0, 2}, {2, 0}, {2, 2}, {35, 35}}) {
            auto p = Fieldwork::geographic(pj.get(), QPointF(426869.205, 5015098.440) + xy);
            p["id"] = QString::number(rows.size());
            p["frame"] = "Practice";
            p["groundCgvd2013M"] = 112. + xy.x() * .02;
            p["surfaceIncluded"] = true;
            rows << p;
        }
        auto terrain = Fieldwork::terrain(rows, "Pad & grade", "Practice", 2959, "CGVD2013", 3);
        QVERIFY2(terrain.error.isEmpty(), qPrintable(terrain.error));
        QCOMPARE(terrain.count, 5);
        QCOMPARE(terrain.triangles.size(), 2);
        QVERIFY(terrain.gaps > 0);
        Surface imported;
        QVERIFY(imported.read(terrain.xml, "Pad & grade - surveyed ground").isEmpty());
        auto at = [&](QPointF offset) {
            auto p = Fieldwork::geographic(pj.get(), QPointF(426869.205, 5015098.440) + offset);
            return imported.target(p["latitudeDeg"].toDouble(), p["longitudeDeg"].toDouble());
        };
        auto middle = at({1, 1});
        QVERIFY(middle);
        QVERIFY(std::abs(*middle - 112.02) < 1e-6);
        QVERIFY(!at({12, 12}));
        QVERIFY(!Fieldwork::terrain(rows, "Pad", "Other", 2959, "CGVD2013", 3).error.isEmpty());
        QVERIFY(!Fieldwork::terrain(rows, "Pad", "Practice", 2959, "Ellipsoid", 3).error.isEmpty());
        rows << rows.first();
        QVERIFY(!Fieldwork::terrain(rows, "Pad", "Practice", 2959, "CGVD2013", 3).error.isEmpty());
    }
    void fieldworkJob() {
        QString line;
        {
            Survey s;
            QVERIFY(s.createJob("Trench", "ARP", 2, "Practice CSRS"));
            QVERIFY(s.setCoverage(true, true, .5));
            auto d = s.inspectDxf({});
            QCOMPARE(d["count"].toInt(), 2);
            QVERIFY(s.importDxf({"Drain A"}, 2959, "Metres"));
            QCOMPARE(s.features().size(), 1);
            line = s.features().first().toMap()["name"].toString();
            QVERIFY(s.followLine(line, .5, 1.2, "Right edge", 0, false));
            QVERIFY(s.gradingWalk());
            QTRY_VERIFY(s.state()["canStore"].toBool());
            auto r = s.state()["line"].toMap();
            QCOMPARE(r["status"].toString(), "Tracking");
            QVERIFY(r["rightM"].toDouble() < 0);
            QVERIFY(s.store("A", "edge"));
            QVERIFY(!s.store("duplicate", "edge"));
            QCOMPARE(s.points().size(), 1);
            auto a = s.points()[0].toMap();
            QCOMPARE(a["sampleRadiusM"].toDouble(), .5);
            QVERIFY(s.setCoverage(false, true, 1.5));
            QTRY_VERIFY(s.state()["canStore"].toBool());
            QVERIFY(s.store("B", "edge"));
            auto b = s.points()[0].toMap();
            QVERIFY(!b.contains("surfaceIncluded"));
            QVERIFY(s.includeShot("Trench", "B", b["gpst"].toString(), true));
            QCOMPARE(s.points()[0].toMap()["sampleRadiusM"].toDouble(), 1.5);
            QCOMPARE(s.points()[1].toMap()["sampleRadiusM"].toDouble(), .5);
            QVERIFY(s.createLine("Return", {b, a}));
            QVERIFY(!s.createLine("Return", {a, b}));
            QVERIFY(!s.previewTerrain(2959, "CGVD2013", 5));
            s.stop();
            QVERIFY(!s.state()["line"].toMap().contains("rightM"));
            // Third off-line survey shot in the same job; the two actual captures above came from RTKLIB.
            auto c = a;
            c["id"] = "C";
            c["latitudeDeg"] = a["latitudeDeg"].toDouble() + .00002;
            c["gpst"] = "fixture";
            QSqlQuery q;
            q.prepare("INSERT INTO points VALUES('Trench','C','fixture',?,'[]')");
            q.addBindValue(QJsonDocument::fromVariant(c).toJson());
            QVERIFY(q.exec());
            s.openJob("Trench");
            QVERIFY(s.previewTerrain(2959, "CGVD2013", 5));
            auto dest = QUrl::fromLocalFile(temp.filePath("ground.landxml"));
            QVERIFY(s.exportTerrain(dest));
            QFile f(dest.toLocalFile());
            QVERIFY(f.open(QIODevice::ReadOnly));
            Surface back;
            QVERIFY(back.read(f.readAll(), "Trench - surveyed ground").isEmpty());
            QCOMPARE(back.faces.size(), 1);
            QVERIFY(s.includeShot("Trench", "C", "fixture", false));
            QVERIFY(!s.exportTerrain(dest));
            QVERIFY(s.deletePoint("Trench", "A", a["gpst"].toString()));
            QCOMPARE(s.features().size(), 2);
        }
        Survey reopened;
        QCOMPARE(reopened.fieldPrefs()["follow"].toString(), line);
        QCOMPARE(reopened.guidePaths()["source"].toList().size(), 3);
        QVERIFY(reopened.createJob("Empty feature job", "ARP", 2, "Practice CSRS"));
        QVERIFY(reopened.features().isEmpty());
        QVERIFY(reopened.fieldPrefs().isEmpty());
    }
    void constructionLines() {
        QString name = "Fence";
        QVariantMap a{{"latitudeDeg", -33.0000}, {"longitudeDeg", 151.0}}, b = a;
        b["latitudeDeg"] = -32.9999;
        {
            Survey s;
            QVERIFY(s.createJob("Unreferenced site", "ARP", 2, ""));
            QVERIFY(!s.createLine(name, {a, a}));
            QVERIFY(!s.createLine(name, {QVariantMap{}, b}));
            QVERIFY(s.createLine(name, {a, b}));
        }
        Survey s;
        auto f = s.features().first().toMap();
        QVERIFY(!f.contains("epsg"));
        QCOMPARE(f["frame"].toString(), "Unknown");
        QCOMPARE(f["localOrigin"].toMap(), a);
        QVERIFY(s.followLine(name, 1, 2, "Right edge", 0, false));
        auto right = s.guidePaths()["right"].toList().first().toMap();
        // Two metres to the right of a northbound line is east, also in the southern hemisphere.
        double east = (right["longitudeDeg"].toDouble() - 151) * 111320 * std::cos(qDegreesToRadians(33.));
        QVERIFY(std::abs(east - 2) < .01);
    }
    void levelCalibration() {
        QCOMPARE(DeviceLevel::angles({0, 1, 0}, {0, 1, 0}), QPointF());
        const float a = qDegreesToRadians(3.0f);
        auto tilt = DeviceLevel::angles({std::sin(a), std::cos(a), 0}, {0, 1, 0});
        QVERIFY(std::abs(tilt.x() - 3) < .001 && std::abs(tilt.y()) < .001);
        tilt = DeviceLevel::angles({0, -std::sin(a), std::cos(a)}, {0, 0, 1});
        QVERIFY(std::abs(tilt.x()) < .001 && std::abs(tilt.y() + 3) < .001);
        DeviceLevel d;
        QVERIFY(!d.zero());
    }
    void offlinePackages() {
        auto path = temp.filePath("quadrants.mbtiles");
        QColor colors[] = {Qt::red, Qt::green, Qt::blue, Qt::yellow};
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", "fixture");
            db.setDatabaseName(path);
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec("CREATE TABLE metadata(name TEXT,value TEXT)"));
            QVERIFY(q.exec("INSERT INTO metadata VALUES ('format','png'),('name','Four "
                           "quadrants'),('bounds','-180,-85,180,85')"));
            QVERIFY(q.exec("CREATE TABLE tiles(zoom_level INTEGER,tile_column INTEGER,tile_row "
                           "INTEGER,tile_data BLOB,PRIMARY KEY(zoom_level,tile_column,tile_row))"));
            for (int y = 0; y < 2; y++)
                for (int x = 0; x < 2; x++) {
                    QImage im(256, 256, QImage::Format_RGB32);
                    im.fill(colors[y * 2 + x]);
                    QBuffer png;
                    png.open(QIODevice::WriteOnly);
                    QVERIFY(im.save(&png, "PNG"));
                    q.prepare("INSERT INTO tiles VALUES(1,?,?,?)");
                    q.addBindValue(x);
                    q.addBindValue(1 - y);
                    q.addBindValue(png.data());
                    QVERIFY(q.exec());
                }
        }
        QSqlDatabase::removeDatabase("fixture");
        Basemap b;
        b.selectJob("Offline job");
        QVERIFY(b.importTiles(QUrl::fromLocalFile(path)));
        QTRY_VERIFY(!b.busy());
        QCOMPARE(b.offlineInfo()["name"].toString(), "Four quadrants");
        auto rev = b.offlineInfo()["revision"].toString();
        QVERIFY(QFile::remove(path));
        for (int y = 0; y < 2; y++)
            for (int x = 0; x < 2; x++)
                QCOMPARE(b.tile(rev + QString("/1/%1/%2").arg(x).arg(y)).pixelColor(64, 64),
                         colors[y * 2 + x]);
        QVERIFY(b.importTiles(QUrl::fromLocalFile(path)));
        QTRY_VERIFY(!b.busy());
        QCOMPARE(b.offlineInfo()["revision"].toString(), rev);
        b.selectJob("Other job");
        QVERIFY(b.offlineInfo().isEmpty());
        QVERIFY(b.tile(rev + "/1/1/1").isNull());
        b.selectJob("Offline job");
        QCOMPARE(b.offlineInfo()["revision"].toString(), rev);
        QVERIFY(b.importTiles(QUrl("practice:ortho")));
        QTRY_VERIFY(!b.busy());
        auto m = b.offlineInfo();
        QCOMPARE(m["maxZoom"].toInt(), 19);
        QCOMPARE(b.tile(m["revision"].toString() + "/17/37889/47002").size(), QSize(256, 256));
        QVERIFY(!b.setOnline("http://example.test/{z}/{x}/{y}", "Example", 19));
        QVERIFY(b.setOnline("https://example.test/{z}/{x}/{y}", "Example", 20));
        b.note("Failed https://example.test?token=secret");
        QVERIFY(!b.events().join('\n').contains("secret"));
        QVERIFY(b.setOnline("", "", 19));
    }
    void streetPlacement() {
        // Render a deterministic 2x2 tile pattern through the actual Street layer, not the network.
        struct Streets : QQmlAbstractUrlInterceptor {
            QString dir;
            QUrl intercept(const QUrl &u, DataType) override {
                if (u.host() != "tile.openstreetmap.org")
                    return u;
                auto parts = u.path().split('/');
                int x = parts.value(2).toInt(), y = parts.value(3).section('.', 0, 0).toInt();
                return QUrl::fromLocalFile(dir + QString("/%1.png").arg((x % 2) + 2 * (y % 2)));
            }
        } streets;
        QColor colors[] = {Qt::red, Qt::green, Qt::blue, Qt::yellow};
        streets.dir = temp.path();
        for (int i = 0; i < 4; i++) {
            QImage im(256, 256, QImage::Format_RGB32);
            im.fill(colors[i]);
            QVERIFY(im.save(temp.filePath(QString::number(i) + ".png")));
        }
        Survey s;
        Basemap b;
        QQmlEngine e;
        e.addUrlInterceptor(&streets);
        e.rootContext()->setContextProperty("collector", &s);
        e.rootContext()->setContextProperty("basemap", &b);
        QQmlComponent component(&e, QUrl::fromLocalFile(QDir::current().filePath("FieldPlot.qml")));
        std::unique_ptr<QObject> obj(component.create());
        QVERIFY2(obj, qPrintable(component.errorString()));
        QQuickWindow w;
        w.resize(512, 512);
        auto item = qobject_cast<QQuickItem *>(obj.get());
        QVERIFY(item);
        item->setParentItem(w.contentItem());
        item->setSize({512, 512});
        QVERIFY(s.createJob("Map placement", "ARP", 2, "Synthetic"));
        QVERIFY(s.practice());
        QTRY_VERIFY(item->property("located").toBool());
        s.stop();
        const int n = 1 << 17;
        double ox = item->property("ox").toDouble() * n, oy = item->property("oy").toDouble() * n;
        int ix = qRound(ox), iy = qRound(oy);
        double lat = s.state()["live"].toMap()["latitudeDeg"].toDouble();
        item->setProperty("scaleM", 40075016.686 * std::cos(qDegreesToRadians(lat)) * 64 / (256. * n));
        double offset = (ox - ix) * 256;
        item->setProperty("east", offset);
        item->setProperty("north", (oy - iy) * 256);
        auto layer = item->findChild<QObject *>("mapLayer");
        QVERIFY(layer);
        layer->setProperty("currentIndex", 1);
        w.show();
        auto pixel = [&](int x, int y) {
            auto img = w.grabWindow();
            if (img.isNull())
                return QColor{};
            return img.pixelColor(x * img.width() / 512, y * img.height() / 512);
        };
        auto color = [&](int x, int y) { return colors[(x % 2) + 2 * (y % 2)]; };
        QTRY_COMPARE(pixel(119, 101), color(ix - 1, iy - 1));
        QCOMPARE(pixel(361, 101), color(ix, iy - 1));
        QCOMPARE(pixel(119, 359), color(ix - 1, iy));
        QCOMPARE(pixel(361, 359), color(ix, iy));
        item->setProperty("east", offset + 256.);
        QTRY_COMPARE(pixel(119, 101), color(ix - 2, iy - 1));
        item->setParentItem(nullptr);
    }
    void workflow() {
        Survey s;
        struct OfflineTiles : QQmlAbstractUrlInterceptor {
            QUrl intercept(const QUrl &url, DataType) override {
                return url.scheme() == "https" ? QUrl("qrc:/unavailable-tile") : url;
            }
        } offline;
        DeviceLevel level;
        Basemap maps;
        QQmlApplicationEngine ui;
        ui.addImageProvider("offline", new OfflineImages(maps));
        ui.rootContext()->setContextProperty("deviceLevel", &level);
        ui.rootContext()->setContextProperty("basemap", &maps);
        ui.addUrlInterceptor(&offline);
        ui.rootContext()->setContextProperty("collector", &s);
        ui.load(QUrl::fromLocalFile(QDir::current().filePath("Main.qml")));
        QVERIFY(!ui.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow *>(ui.rootObjects().first());
        QVERIFY(window);
        QCOMPARE(window->property("page").toInt(), 0);
        window->setProperty("page", 2);
        window->resize(480, 800);
        QTest::qWait(100);
        QVERIFY(s.createJob("Simulated control", "ARP", 2, "Synthetic"));
#ifdef Q_OS_UNIX
        QProcess mock;
        mock.setProcessChannelMode(QProcess::ForwardedErrorChannel);
        mock.start("python3", {"tools/mock5700.py", "--port", "0"});
        QVERIFY(mock.waitForReadyRead(5000));
        auto path = mock.readLine().trimmed();
        if (!mock.canReadLine())
            mock.waitForReadyRead(1000);
        auto caster = mock.readLine().trimmed();
        QVERIFY(s.start(path, caster, "", ""));
        QTRY_VERIFY_WITH_TIMEOUT(s.state()["canStore"].toBool(), 10000);
        const auto ground = s.state()["live"].toMap()["groundCgvd2013M"].toDouble();
        QVERIFY(s.setGrade("Manual", ground - .450, "CGVD2013", .02, true, ""));
        auto pointer = window->findChild<QQuickItem *>("gradePointer");
        auto track = window->findChild<QQuickItem *>("gradeTrack");
        QVERIFY(pointer && track);
        QTRY_VERIFY(pointer->isVisible() && track->height() > 100);
        QTRY_VERIFY(qAbs(pointer->y() + pointer->height() / 2 - track->y()) < 1);
        QVERIFY(s.state()["grade"].toMap()["cutMinusFillM"].toDouble() > .25);
        QVERIFY(s.setGrade("Manual", ground, "CGVD2013", .02, true, ""));
        QTRY_VERIFY(qAbs(pointer->y() + pointer->height() / 2 - track->y() - track->height() / 2) < 2);
        QVERIFY(s.setGrade("Manual", 112.258, "CGVD2013", .02, true, ""));
        QTRY_COMPARE(s.state()["grade"].toMap()["gradeStatus"].toString(), "FILL");
        QTRY_VERIFY(pointer->y() > track->y() + track->height() / 2);
        auto layer = window->findChild<QObject *>("mapLayer");
        QVERIFY(layer);
        layer->setProperty("currentIndex", 2);
        QTRY_VERIFY(window->findChild<QQuickItem *>("offlineTiles")->isVisible());
        auto button = window->findChild<QQuickItem *>("storePoint");
        QVERIFY(button);
        QVERIFY(button->isEnabled());
        auto tapAt = button->mapToScene(button->boundingRect().center()).toPoint();
        QVERIFY(window->contentItem()->boundingRect().contains(tapAt));
        auto touch = QTest::createTouchDevice();
        QTest::touchEvent(window, touch).press(0, tapAt, window);
        QTest::touchEvent(window, touch).release(0, tapAt, window);
        QTRY_COMPARE(s.points().size(), 1);
        QCOMPARE(s.points().first().toMap()["gradeTargetM"].toDouble(), 112.258);
        QVERIFY(!s.store("P2", "duplicate"));
        QVERIFY(s.occupy("B1", "averaged", 2, true));
        window->resize(940, 580);
        QTRY_VERIFY_WITH_TIMEOUT(s.state()["occupation"].toMap()["review"].toBool(), 15000);
        QVERIFY(s.finish());
        QCOMPARE(s.points().size(), 2);
        auto b = s.points().first().toMap();
        // The live serial mock is polled, not replayed at a guaranteed 1 Hz.
        QVERIFY(b["epochs"].toInt() >= 2);
        QVERIFY(b["elapsedS"].toDouble() >= 2.0);
        QVERIFY(b["source"].toString().contains("simulated"));
        QCOMPARE(s.epochsFor("B1").size(), b["epochs"].toInt());
        QVERIFY(std::abs(b["latitudeDeg"].toDouble() - 45.28558) < .00005);
        QVERIFY(s.exportCsv(QUrl::fromLocalFile(temp.filePath("job.csv"))));
        kill(mock.processId(), SIGUSR1);
        QTRY_COMPARE_WITH_TIMEOUT(s.state()["live"].toMap()["quality"].toString(), "standalone", 10000);
        QVERIFY(s.state()["fresh"].toBool());
        QVERIFY(!s.state()["grade"].toMap().contains("cutMinusFillM"));
        QTRY_VERIFY(!pointer->isVisible());
        kill(mock.processId(), SIGUSR2);
        QTRY_VERIFY_WITH_TIMEOUT(!s.state()["fresh"].toBool(), 8000);
        QVERIFY(!s.store("P3", "stale"));
        s.stop();
        mock.terminate();
        mock.waitForFinished();
        s.openJob("Simulated control");
        QCOMPARE(s.points().first().toMap(), b);
        window->setProperty("page", 3);
        auto detail = window->findChild<QObject *>("pointDetail");
        auto confirm = window->findChild<QObject *>("deleteDialog");
        auto list = window->findChild<QQuickItem *>("pointList");
        QVERIFY(detail && confirm && list);
        auto tap = [&](QPoint pos) {
            QTest::touchEvent(window, touch).press(0, pos, window);
            QTest::touchEvent(window, touch).release(0, pos, window);
        };
        auto tapButton = [&](const char *name) {
            auto item = window->findChild<QQuickItem *>(name);
            QVERIFY(item && item->isVisible() && item->height() >= 56);
            tap(item->mapToScene(item->boundingRect().center()).toPoint());
        };
        QTRY_VERIFY(list->isVisible() && list->property("count").toInt() == 2);
        tap(list->mapToScene(QPointF(40, 25)).toPoint());
        QTRY_VERIFY(detail->property("opened").toBool());
        tapButton("requestDelete");
        QTRY_VERIFY(confirm->property("opened").toBool());
        tapButton("cancelDelete");
        QTRY_VERIFY(!confirm->property("visible").toBool());
        QCOMPARE(s.points().size(), 2);
        tapButton("requestDelete");
        QTRY_VERIFY(confirm->property("opened").toBool());
        tapButton("confirmDelete");
        QTRY_COMPARE(s.points().size(), 1);
        QVERIFY(s.epochsFor("B1").isEmpty());
#endif
    }
    void jobScopedDeletion() {
        QVariantMap kept;
        {
            Survey s;
            QVERIFY(s.createJob("North", "ARP", 2, "Synthetic"));
            QVERIFY(s.practice());
            QTRY_VERIFY(s.state()["canStore"].toBool());
            QVERIFY(s.store("P1", "north"));
            auto time = s.points().first().toMap().value("gpst").toString();
            QVERIFY(s.createJob("South", "ARP", 2, "Synthetic"));
            QVERIFY(s.store("P1", "south"));
            kept = s.points().first().toMap();
            QVERIFY(!s.deletePoint("North", "P1", time));
            s.openJob("North");
            QVERIFY(!s.deletePoint("North", "P1", "wrong epoch"));
            QCOMPARE(s.points().size(), 1);
            QVERIFY(s.deletePoint("North", "P1", time));
            QVERIFY(s.points().isEmpty());
            s.openJob("North");
            QVERIFY(!s.store("P2", "same epoch"));
            QTRY_VERIFY(s.state()["canStore"].toBool());
            QVERIFY(s.store("P2", "new epoch"));
            auto path = temp.filePath("after-delete.csv");
            QVERIFY(s.exportCsv(QUrl::fromLocalFile(path)));
            QFile csv(path);
            QVERIFY(csv.open(QIODevice::ReadOnly));
            auto data = csv.readAll();
            QVERIFY(data.contains("\"P2\"") && !data.contains("\"P1\""));
        }
        Survey reopened;
        QCOMPARE(reopened.points().size(), 1);
        QCOMPARE(reopened.points().first().toMap()["id"].toString(), "P2");
        reopened.openJob("South");
        QCOMPARE(reopened.points().first().toMap(), kept);
    }
    void referenceParity() {
        QFile data(":/fixtures/practice.bin"), golden("fixtures/reference.csv");
        QVERIFY(data.open(QIODevice::ReadOnly));
        QVERIFY(golden.open(QIODevice::ReadOnly));
        golden.readLine();
        auto h = data.read(6);
        QCOMPARE(h.size(), 6);
        Solver solver;
        solver.setReplayTime(qFromBigEndian<quint16>(h.constData()),
                             qFromBigEndian<quint32>(h.constData() + 2));
        int rows = 0;
        while (!data.atEnd()) {
            auto head = data.read(3);
            QCOMPARE(head.size(), 3);
            auto bytes = data.read(qFromBigEndian<quint16>(head.constData() + 1));
            if (head[0] == 'B') {
                solver.base(bytes);
                continue;
            }
            auto p = solver.rover(bytes);
            if (head[0] == 'E')
                continue;
            auto ref = golden.readLine().trimmed().split(',');
            QCOMPARE(ref.size(), 7);
            QVERIFY(p["gpst"].toString().startsWith(QString::fromLatin1(ref[0])));
            QCOMPARE("rtk-" + p["quality"].toString(), QString::fromLatin1(ref[1]));
            QVERIFY(std::abs(p["latitudeDeg"].toDouble() - ref[2].toDouble()) < 1e-8);
            QVERIFY(std::abs(p["longitudeDeg"].toDouble() - ref[3].toDouble()) < 1e-8);
            QVERIFY(std::abs(p["ellipsoidM"].toDouble() - ref[4].toDouble()) < .001);
            QCOMPARE(p["used"].toInt(), ref[5].toInt());
            addHeights(p, 0);
            QVERIFY(std::abs(p["antennaCgvd2013M"].toDouble() - ref[6].toDouble()) < .001);
            rows++;
        }
        QCOMPARE(rows, 90);
        QVERIFY(golden.atEnd());
    }
    void portableReplay() {
        QVariantMap stored;
        {
            Survey s;
            QVERIFY(s.createJob("Replay", "Synthetic ARP", 1.8, "Synthetic geometry"));
            QVERIFY(s.practice());
            QTRY_VERIFY_WITH_TIMEOUT(s.state()["fresh"].toBool(), 5000);
            QVERIFY(s.occupy("B2", "check", 2, true));
            QTRY_VERIFY_WITH_TIMEOUT(s.state()["occupation"].toMap()["review"].toBool(), 8000);
            QVERIFY(s.finish());
            stored = s.points().first().toMap();
            QVERIFY(s.createJob("Replay", "Second ARP", 2.5, "New frame", true));
            QVERIFY(stored["source"].toString().contains("replay"));
            QVERIFY(std::abs(stored["longitudeDeg"].toDouble() + 75.93252) < .00005);
        }
        Survey reopened;
        QCOMPARE(reopened.state()["job"].toString(), "Replay");
        QCOMPARE(reopened.points().first().toMap(), stored);
        QVERIFY(reopened.epochsFor("B2").size() >= 3);
    }
    void gradingWalkAndHoles() {
        QFile file(":/fixtures/practice-pad.landxml");
        QVERIFY(file.open(QIODevice::ReadOnly));
        auto xml = file.readAll();
        Surface tin;
        QVERIFY(tin.read(xml).isEmpty());
        auto name = tin.names.first();
        QVERIFY(tin.read(xml, name).isEmpty());
        QCOMPARE(tin.outline.size(), 8);
        QFile data(":/fixtures/grading-walk.bin");
        QVERIFY(data.open(QIODevice::ReadOnly));
        auto h = data.read(6);
        Solver solver;
        solver.setReplayTime(qFromBigEndian<quint16>(h.constData()),
                             qFromBigEndian<quint32>(h.constData() + 2));
        int step = 0;
        while (!data.atEnd()) {
            auto head = data.read(3);
            auto bytes = data.read(qFromBigEndian<quint16>(head.constData() + 1));
            if (head[0] == 'B') {
                solver.base(bytes);
                continue;
            }
            auto p = solver.rover(bytes);
            if (head[0] == 'E')
                continue;
            QCOMPARE(p.value("quality").toString(), "fixed");
            addHeights(p, 2);
            auto z = tin.target(p["latitudeDeg"].toDouble(), p["longitudeDeg"].toDouble());
            if (step == 0 || step == 10 || step == 20) {
                QVERIFY(z);
                QVERIFY(std::abs(p.value("groundCgvd2013M").toDouble() - *z - (10 - step) * .02) < .005);
            }
            if (step >= 31)
                QVERIFY(!z);
            step++;
        }
        QCOMPARE(step, 36);
        auto cut = xml;
        cut.replace("<F>1 2 3", "<F i=\"1\">1 2 3");
        QVERIFY(tin.read(cut, name).isEmpty());
        QVERIFY(!tin.target(45.28550, -75.93245));
        QVERIFY(tin.target(45.28566, -75.93259));
        QVERIFY(!tin.read(xml.replace("<F>1 2 3", "<F>1 2 99"), name).isEmpty());
    }
    void savedDesign() {
        QString hash;
        {
            Survey s;
            QVERIFY(s.createJob("Grade pad", "Practice", 2, "NAD83(CSRS)"));
            auto meta = s.inspectSurface({});
            QCOMPARE(meta["epsg"].toInt(), 2959);
            auto name = meta["names"].toStringList().value(0);
            QVERIFY(s.setGrade("Surface", 0, "", .02, false, name));
            QVERIFY(s.practice());
            QTRY_VERIFY(s.state()["fresh"].toBool());
            QCOMPARE(s.state()["grade"].toMap().value("gradeStatus").toString(), "ON GRADE");
            QVERIFY(s.store("G1", "pad"));
            QVERIFY(!s.surfaceMesh().isEmpty());
            QVERIFY(s.state()["surfacePosition"].toMap().contains("up"));
            QTRY_VERIFY(s.controlCheck("G1").contains("horizontalM"));
            QVERIFY(s.controlCheck("G1")["horizontalM"].toDouble() < .1);
            hash = s.points().first().toMap().value("gradeSurfaceSha256").toString();
            QCOMPARE(hash.size(), 64);
            s.stop();
            QVERIFY(!s.state()["grade"].toMap().contains("cutMinusFillM"));
        }
        Survey s;
        QCOMPARE(s.state()["gradePrefs"].toMap()["sha256"].toString(), hash);
        QCOMPARE(s.surfaceOutline().size(), 8);
        QVERIFY(s.setGrade("Manual", 300, "Ellipsoid", .01, false, ""));
        QCOMPARE(s.surfaceOutline().size(), 8);
        QVERIFY(!s.surfaceMesh().isEmpty());
        QCOMPARE(s.points().first().toMap()["gradeMode"].toString(), "Surface");
    }
    void workViewInstruments() {
        Survey s;
        Basemap maps;
        DeviceLevel level;
        QQmlApplicationEngine ui;
        ui.rootContext()->setContextProperty("collector", &s);
        ui.rootContext()->setContextProperty("basemap", &maps);
        ui.rootContext()->setContextProperty("deviceLevel", &level);
        QVERIFY(s.createJob("Instrument panel", "ARP", 2, "Synthetic"));
        QVERIFY(s.setGrade("Manual", 112, "CGVD2013", .02, false, ""));
        auto info = s.inspectSurface({});
        auto name = info["names"].toStringList().first();
        QVERIFY(s.setGrade("Preview", 0, "", 0, false, name));
        QCOMPARE(s.state()["gradePrefs"].toMap()["mode"].toString(), "Manual");
        ui.load(QUrl::fromLocalFile(QDir::current().filePath("Main.qml")));
        QVERIFY(!ui.rootObjects().isEmpty());
        auto w = qobject_cast<QQuickWindow *>(ui.rootObjects().first());
        auto prefs = w->findChild<QObject *>("displayPreferences");
        QVERIFY(prefs);
        auto tools = w->findChild<QObject *>("fieldTools");
        QVERIFY(tools);
        auto showTool = [&](QString t, QString n) {
            QVERIFY(QMetaObject::invokeMethod(tools, "start", Q_ARG(QVariant, t), Q_ARG(QVariant, n)));
            QTRY_VERIFY(tools->property("opened").toBool());
        };
        showTool("Coverage", "");
        const auto shortHeight = tools->property("height").toReal();
        QVERIFY(shortHeight < w->height() * .7);
        QMetaObject::invokeMethod(tools, "close");
        QTRY_VERIFY(!tools->property("visible").toBool());
        showTool("DXF", "");
        QVERIFY(QMetaObject::invokeMethod(tools, "inspect", Q_ARG(QVariant, QString(""))));
        auto confirm = tools->findChild<QObject *>("confirmDxfReference");
        auto commit = tools->findChild<QObject *>("commitDxf");
        QVERIFY(confirm && commit);
        confirm->setProperty("checked", true);
        QTRY_VERIFY(commit->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(commit, "clicked"));
        QTRY_COMPARE(s.features().size(), 2);
        QTRY_VERIFY(!tools->property("visible").toBool());
        w->setWidth(390);
        w->setHeight(640);
        showTool("Follow", s.features().first().toMap()["name"].toString());
        QVERIFY(tools->property("height").toReal() > shortHeight);
        auto chooser = tools->findChild<QQuickItem *>("featureChooser");
        QVERIFY(chooser);
        auto popup = chooser->property("popup").value<QObject *>();
        QVERIFY(popup);
        QMetaObject::invokeMethod(popup, "open");
        QTRY_VERIFY(popup->property("opened").toBool());
        auto view = popup->property("contentItem").value<QQuickItem *>();
        QVERIFY(view);
        auto listContent = view->property("contentItem").value<QQuickItem *>();
        QVERIFY(listContent);
        auto namedOptions = [&] {
            QStringList names;
            for (auto child : listContent->childItems())
                if (!child->property("text").toString().isEmpty())
                    names << child->property("text").toString();
            return names;
        };
        QTRY_VERIFY(namedOptions().contains(s.features().last().toMap()["name"].toString()));
        QMetaObject::invokeMethod(popup, "close");
        QTRY_VERIFY(!popup->property("visible").toBool());
        auto follow = tools->findChild<QQuickItem *>("commitFollowing");
        QVERIFY(follow && follow->property("enabled").toBool());
        auto side = tools->findChild<QQuickItem *>("offsetSide");
        auto offset = tools->findChild<QObject *>("offsetDistance");
        auto scroll = tools->property("contentItem").value<QQuickItem *>();
        QVERIFY(side && offset && scroll);
        QTRY_VERIFY(scroll->property("contentHeight").toReal() <= scroll->height() + 1);
        auto chooseSide = [&](QString text) {
            for (auto b : side->childItems())
                if (b->property("text").toString() == text) {
                    QVERIFY(b->width() >= 56 && b->height() >= 56);
                    QTest::mouseClick(w, Qt::LeftButton, {},
                                      b->mapToScene(b->boundingRect().center()).toPoint());
                    QTRY_COMPARE(side->property("currentText").toString(), text);
                    return;
                }
            QFAIL("Visible side choice missing");
        };
        chooseSide("Left");
        offset->setProperty("text", "0.50");
        // In a short landscape window only the body scrolls; the commit button remains tappable.
        w->setHeight(365);
        QTRY_VERIFY(scroll->property("contentHeight").toReal() > scroll->height());
        auto action = follow->mapToScene(follow->boundingRect().center()).toPoint();
        QVERIFY(w->contentItem()->boundingRect().contains(action));
        QTest::mouseClick(w, Qt::LeftButton, {}, action);
        QTRY_COMPARE(s.fieldPrefs()["offsetM"].toDouble(), -.5);
        QTRY_VERIFY(!tools->property("visible").toBool());
        w->setHeight(640);
        showTool("Follow", QString());
        QCOMPARE(side->property("currentText").toString(), "Left");
        chooseSide("Right");
        QVERIFY(QMetaObject::invokeMethod(follow, "clicked"));
        QCOMPARE(s.fieldPrefs()["offsetM"].toDouble(), .5);
        QTRY_COMPARE(w->property("page").toInt(), 2);
        QTRY_VERIFY(!tools->property("visible").toBool());
        prefs->setProperty("appearance", "Daylight");
        w->setProperty("page", 2);
        w->resize(1050, 760);
        QVERIFY(s.practice());
        QTRY_VERIFY(s.state()["fresh"].toBool());
        auto puck = w->findChild<QQuickItem *>("crossTrackRover");
        auto chart = w->findChild<QQuickItem *>("crossTrackChart");
        QVERIFY(puck && chart);
        QTRY_VERIFY(puck->isVisible() && puck->x() + puck->width() / 2 < chart->width() / 2);
        QVERIFY(s.followLine(s.fieldPrefs()["follow"].toString(), -.5, 0, "Centre", 0, false));
        QTRY_VERIFY(puck->x() + puck->width() / 2 > chart->width() / 2);
        auto coords = w->findChild<QQuickItem *>("positionCoordinates");
        auto height = w->findChild<QQuickItem *>("groundElevation");
        QVERIFY(coords && height);
        QVERIFY(coords->property("text").toString().contains("45.285"));
        QVERIFY(height->property("text").toString().contains("CGVD2013"));
        prefs->setProperty("showGrade", false);
        QTRY_VERIFY(!w->findChild<QQuickItem *>("gradeTrack") ||
                    !w->findChild<QQuickItem *>("gradeTrack")->isVisible());
        QVERIFY(coords->isVisible() && height->isVisible());
        QVERIFY(s.store("P1", "grade hidden"));
        QCOMPARE(s.points().first().toMap()["gradeMode"].toString(), "Manual");
        auto click = [&](const char *name) {
            for (auto button : w->findChildren<QQuickItem *>(name))
                if (button->isVisible()) {
                    QVERIFY(button->isEnabled());
                    QVERIFY(QMetaObject::invokeMethod(button, "clicked"));
                    return;
                }
            QFAIL(name);
        };
        // Build one feature across both entry points. No EPSG is present in this job's frame.
        click("mapNewLine");
        auto draft = w->findChild<QObject *>("featureDraft");
        QVERIFY(draft);
        auto map = w->findChild<QQuickItem *>("workMap");
        QVERIFY(map);
        QTRY_VERIFY(draft->property("active").toBool() && map->width() > 900);
        // The only saved point is the map origin. A tap there must keep its survey identity.
        auto tapAt = [&](QPointF p) {
            QTest::mouseClick(w, Qt::LeftButton, {}, map->mapToScene(p).toPoint());
        };
        tapAt({map->width() / 2, map->height() / 2});
        QTRY_COMPARE(draft->property("vertices").value<QJSValue>().toVariant().toList().size(), 1);
        QCOMPARE(draft->property("vertices")
                     .value<QJSValue>()
                     .toVariant()
                     .toList()
                     .first()
                     .toMap()["id"]
                     .toString(),
                 "P1");
        QMetaObject::invokeMethod(draft, "remove", Q_ARG(QVariant, 0));
        QTest::mouseClick(w, Qt::LeftButton, {},
                          map->mapToScene(QPointF(map->width() * .65, map->height() * .45)).toPoint());
        QTRY_COMPARE(draft->property("vertices").value<QJSValue>().toVariant().toList().size(), 1);
        w->setProperty("page", 3);
        auto list = w->findChild<QQuickItem *>("pointList");
        QTRY_VERIFY(list->isVisible());
        QTest::mouseClick(w, Qt::LeftButton, {}, list->mapToScene(QPointF(36, 25)).toPoint());
        QTRY_COMPARE(draft->property("vertices").value<QJSValue>().toVariant().toList().size(), 2);
        click("reviewLine");
        QTRY_VERIFY(w->findChild<QObject *>("featureDraft")->property("active").toBool());
        QTest::qWait(180);
        click("saveDraftLine");
        QTRY_VERIFY(!draft->property("active").toBool());
        auto mixed = s.features().last().toMap();
        for (auto v : s.features())
            if (v.toMap()["source"] == "Points + map sketch")
                mixed = v.toMap();
        QCOMPARE(mixed["source"].toString(), "Points + map sketch");
        QCOMPARE(mixed["pointIds"].toStringList().last(), "P1");
        click("listNewLine");
        QTRY_VERIFY(draft->property("active").toBool());
        QMetaObject::invokeMethod(draft, "cancel");
        w->setProperty("page", 2);
        prefs->setProperty("showGrade", true);
        auto gradeWindow = w->findChild<QQuickWindow *>("gradeWindow");
        QVERIFY(gradeWindow);
        click("gradeWindowPlacement");
        QTRY_VERIFY(gradeWindow->isVisible());
        QTRY_COMPARE(w->findChild<QQuickItem *>("gradeTrack")->window(), gradeWindow);
        click("gradeWindowPlacement");
        QTRY_VERIFY(!gradeWindow->isVisible());
        QCOMPARE(w->findChild<QQuickItem *>("gradeTrack")->window(), w);
        prefs->setProperty("showSurface", true);
        auto preview = w->findChild<QQuickWindow *>("surfaceWindow");
        QVERIFY(preview);
        QTRY_VERIFY(!preview->isVisible());
        QTRY_VERIFY(w->findChild<TinItem *>() && w->findChild<TinItem *>()->height() >= 80);
        click("surfaceWindowPlacement");
        QTRY_VERIFY(preview->isVisible());
        QTRY_VERIFY(w->findChild<TinItem *>());
        auto tin = w->findChild<TinItem *>();
        QTRY_VERIFY(tin->mesh().contains("faces"));
        QVERIFY(tin->rover().contains("up"));
        QTRY_VERIFY(tin->width() > 2 && tin->height() > 2);
        QImage image(tin->size().toSize(), QImage::Format_RGB32);
        QVERIFY(!image.isNull());
        QPainter p(&image);
        tin->paint(&p);
        p.end();
        QVERIFY(image.pixelColor(image.width() / 2, image.height() / 2) != image.pixelColor(0, 0));
        click("surfaceWindowPlacement");
        QTRY_VERIFY(!preview->isVisible());
        QCOMPARE(tin->window(), w);
        auto surfaceDock = w->findChild<QQuickItem *>("surfaceDock");
        QVERIFY(surfaceDock);
        surfaceDock->setProperty("mobile", true);
        click("surfaceWindowPlacement");
        QTRY_VERIFY(prefs->property("surfaceFloating").toBool());
        QVERIFY(!preview->isVisible());
        QVERIFY(tin->isVisible());
        QCOMPARE(tin->window(), w);
        click("surfaceWindowPlacement");
        QTRY_VERIFY(!prefs->property("surfaceFloating").toBool());
        prefs->setProperty("appearance", "Dark");
        QTRY_VERIFY(!w->property("daylight").toBool());
        prefs->setProperty("showSurface", false);
        QTRY_VERIFY(!preview->isVisible());
        s.stop();
        QTRY_VERIFY(!puck->isVisible());
        QVERIFY(w->findChild<QQuickItem *>("positionStatus")
                    ->property("text")
                    .toString()
                    .contains("LAST KNOWN"));
    }
};
QTEST_MAIN(CollectorTest)
#include "collector_test.moc"
