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
        auto pointer = window->findChild<QQuickItem *>("gradePointer");
        auto track = window->findChild<QQuickItem *>("gradeTrack");
        QVERIFY(pointer && track);
        const auto ground = s.state()["live"].toMap()["groundCgvd2013M"].toDouble();
        QVERIFY(s.setGrade("Manual", ground - .450, "CGVD2013", .02, true, ""));
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
        prefs->setProperty("appearance", "Daylight");
        w->setProperty("page", 2);
        w->resize(1050, 760);
        QVERIFY(s.practice());
        QTRY_VERIFY(s.state()["fresh"].toBool());
        auto coords = w->findChild<QQuickItem *>("positionCoordinates");
        auto height = w->findChild<QQuickItem *>("groundElevation");
        QVERIFY(coords && height);
        QVERIFY(coords->property("text").toString().contains("45.285"));
        QVERIFY(height->property("text").toString().contains("CGVD2013"));
        prefs->setProperty("showGrade", false);
        QTRY_VERIFY(!w->findChild<QQuickItem *>("gradeTrack")->isVisible());
        QVERIFY(coords->isVisible() && height->isVisible());
        QVERIFY(s.store("P1", "grade hidden"));
        QCOMPARE(s.points().first().toMap()["gradeMode"].toString(), "Manual");
        prefs->setProperty("showSurface", true);
        auto preview = w->findChild<QQuickWindow *>("surfaceWindow");
        QVERIFY(preview);
        QTRY_VERIFY(preview->isVisible());
        QTRY_VERIFY(preview->findChild<TinItem *>());
        auto tin = preview->findChild<TinItem *>();
        QTRY_VERIFY(tin->mesh().contains("faces"));
        QVERIFY(tin->rover().contains("up"));
        QTRY_VERIFY(tin->width()>2 && tin->height()>2);
        QImage image(tin->size().toSize(), QImage::Format_RGB32);
        QVERIFY(!image.isNull());
        QPainter p(&image);
        tin->paint(&p);
        p.end();
        QVERIFY(image.pixelColor(image.width() / 2, image.height() / 2) != image.pixelColor(0, 0));
        prefs->setProperty("appearance", "Dark");
        QTRY_VERIFY(!w->property("daylight").toBool());
        prefs->setProperty("showSurface", false);
        QTRY_VERIFY(!preview->isVisible());
        s.stop();
        QVERIFY(w->findChild<QQuickItem *>("positionStatus")
                    ->property("text")
                    .toString()
                    .contains("LAST KNOWN"));
    }
};
QTEST_MAIN(CollectorTest)
#include "collector_test.moc"
