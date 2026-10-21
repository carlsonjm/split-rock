// SPDX-License-Identifier: GPL-2.0-or-later
//
// The Helpers' window, shown off screen with every program a Helper would run
// replaced by the test's own.
#include "app/ChangeRunner.h"
#include "app/Helpers.h"
#include "app/QmlContext.h"
#include "settings/SettingsHistory.h"

#include <QDir>
#include <QFile>
#include <QClipboard>
#include <QGuiApplication>
#include <QPalette>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include "AdminStandIn.h"

#include <algorithm>
#include <functional>
#include <memory>

using namespace splitrock;

class TestSheet : public QObject
{
    Q_OBJECT

    // Searches what is on screen, including what Repeaters and Loaders made.
    static QQuickItem *find(QQuickItem *item, const QString &name)
    {
        if (item->objectName() == name)
            return item;
        for (QQuickItem *child : item->childItems()) {
            if (QQuickItem *found = find(child, name))
                return found;
        }
        return nullptr;
    }

    static QQuickItem *find(QQuickWindow *window, const QString &name)
    {
        return find(window->contentItem(), name);
    }

    // With SPLIT_ROCK_SCREENSHOTS set to a folder, each screen is saved there,
    // for comparing against the mock-up by eye.
    static void shoot(QQuickWindow *window, const QString &name)
    {
        const QString dir = qEnvironmentVariable("SPLIT_ROCK_SCREENSHOTS");
        if (dir.isEmpty())
            return;
        QTest::qWait(150);
        QDir().mkpath(dir);
        window->grabWindow().save(dir + QLatin1Char('/') + name + QStringLiteral(".png"));
    }

    // The Helpers' window, made as Split Rock makes it, not yet shown.
    static QQuickWindow *open(QQmlApplicationEngine &engine, Helpers &helpers)
    {
        engine.rootContext()->setContextProperty(QStringLiteral("helpers"), &helpers);
        exposeProduct(engine.rootContext());
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qml/ActionWindow.qml")));
        auto *window = qobject_cast<QQuickWindow *>(component.create(engine.rootContext()));
        if (!window)
            qWarning().noquote() << component.errorString();
        return window;
    }

    // Every piece of text drawn in the window that runs past either side of
    // it, by its name or its first words.
    static QStringList overflowing(QQuickWindow *window)
    {
        QStringList found;
        const std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
            if (!item->isVisible() || item->opacity() <= 0.01)
                return;
            if (item->inherits("QQuickText") || item->inherits("QQuickTextInput")) {
                const QString text = item->property("text").toString();
                const qreal drawn = item->inherits("QQuickText") ? item->property("contentWidth").toReal()
                                                                  : item->width();
                const QRectF scene = item->mapRectToScene(QRectF(0, 0, std::min(drawn, item->width() > 0 ? std::max(drawn, item->width()) : drawn), item->height()));
                if (!text.isEmpty() && (scene.left() < -0.5 || scene.right() > window->width() + 0.5))
                    found.append(QStringLiteral("%1 [%2..%3]").arg(text.left(40)).arg(scene.left()).arg(scene.right()));
            }
            for (QQuickItem *child : item->childItems())
                walk(child);
        };
        walk(window->contentItem());
        return found;
    }

    // Waits for the card's motion to settle.
    static void settle() { QTest::qWait(400); }

private Q_SLOTS:
    void initTestCase()
    {
        if (qEnvironmentVariable("SPLIT_ROCK_SCREENSHOTS_THEME") == QLatin1String("dark")) {
            QPalette dark;
            dark.setColor(QPalette::Window, QColor(0x14, 0x14, 0x14));
            dark.setColor(QPalette::WindowText, QColor(0xF8, 0xF8, 0xFF));
            dark.setColor(QPalette::Base, QColor(0x1C, 0x1C, 0x1C));
            dark.setColor(QPalette::Text, QColor(0xF8, 0xF8, 0xFF));
            dark.setColor(QPalette::Button, QColor(0x24, 0x24, 0x24));
            dark.setColor(QPalette::ButtonText, QColor(0xF8, 0xF8, 0xFF));
            dark.setColor(QPalette::Highlight, QColor(0x3D, 0xAE, 0xE9));
            QGuiApplication::setPalette(dark);
        }
        // Any warning from the sheet is a fault in it.
        QTest::failOnWarning(QRegularExpression(QStringLiteral("qrc:/")));
    }

    void theUpdateCardShowsEachStep()
    {
        QTemporaryDir store;
        Helpers h(store.path());
        SystemUpdate *update = h.systemUpdate();
        update->setElevate({});
        update->setLookScript(QStringLiteral("cat '" FIXTURES_DIR "/update/look.txt'"));
        // Slowed so the card can be seen partway.
        update->setAdmin(adminStandIn(QStringLiteral("echo split-rock-pre=41; head -n 22 '" FIXTURES_DIR "/update/update.out'; "
                                               "sleep 1.2; tail -n +23 '" FIXTURES_DIR "/update/update.out'; "
                                               "echo split-rock-post=42")));
        update->setAppsScript(QStringLiteral("echo Updating apps"));
        update->setAfterScript(QStringLiteral("printf '@@ kernel\\nrestart\\n@@ settings\\n/etc/pacman.conf.pacnew\\n@@ failed\\n@@ end\\n'"));
        QQmlApplicationEngine engine;
        std::unique_ptr<QQuickWindow> window(open(engine, h));
        QVERIFY(window);
        QVERIFY(h.openChore(QStringLiteral("update")));
        window->show();
        window->setGeometry(0, 0, 560, 800);
        QVERIFY(QTest::qWaitForWindowExposed(window.get()));
        QQuickWindow *full = window.get();
        QTRY_COMPARE(update->stage(), QStringLiteral("ready"));
        QTRY_VERIFY(find(full, QStringLiteral("updateCard")));
        settle();
        QVERIFY(overflowing(full).isEmpty());
        shoot(full, QStringLiteral("21-update-ready"));
        QQuickItem *button = find(full, QStringLiteral("updateButton"));
        QVERIFY(button && button->isVisible() && button->isEnabled());

        // Every package is on the card before anything installs.
        QVERIFY(find(full, QStringLiteral("updateList"))->isVisible());

        QMetaObject::invokeMethod(button, "clicked");
        // Plasma moves a step under Shuffle: the card asks once more.
        QCOMPARE(update->stage(), QStringLiteral("confirming"));
        settle();
        QVERIFY(overflowing(full).isEmpty());
        shoot(full, QStringLiteral("21b-update-confirming"));
        QQuickItem *anyway = find(full, QStringLiteral("updateAnyway"));
        QVERIFY(anyway && anyway->isVisible());
        QMetaObject::invokeMethod(anyway, "clicked");
        QTRY_COMPARE(update->stage(), QStringLiteral("updating"));
        QTRY_VERIFY(update->done() >= 2);
        settle();
        QVERIFY(find(full, QStringLiteral("updateProgress"))->isVisible());
        shoot(full, QStringLiteral("22-update-running"));

        QTRY_COMPARE_WITH_TIMEOUT(update->stage(), QStringLiteral("done"), 10000);
        settle();
        QVERIFY(find(full, QStringLiteral("restartButton"))->isVisible());
        QVERIFY(overflowing(full).isEmpty());
        shoot(full, QStringLiteral("23-update-done"));
        update->close();
        QTRY_VERIFY(!full->isVisible());
    }
    // Core opens Free up space on its own: an ordinary window with the
    // Helper in it.
    void aHelperOpensOnItsOwn()
    {
        QTemporaryDir store;
        Helpers c(store.path());
        const QString chores = QStringLiteral(FIXTURES_DIR "/chores/");
        SpaceCleanup *space = c.spaceCleanup();
        space->setElevate({});
        space->setLookScript(QStringLiteral("cat '") + chores + QStringLiteral("space-look.txt'"));
        space->setYoursScript(QStringLiteral("cat '") + chores + QStringLiteral("yours-look.txt'"));
        space->setReport({});

        QQmlApplicationEngine engine;
        std::unique_ptr<QQuickWindow> window(open(engine, c));
        QVERIFY(window);

        QVERIFY(c.openChore(QStringLiteral("space")));
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window.get()));
        QCOMPARE(window->title(), QStringLiteral("Free up space"));
        QTRY_COMPARE(space->stage(), QStringLiteral("ready"));
        QTRY_VERIFY(find(window.get(), QStringLiteral("spaceCard")) && find(window.get(), QStringLiteral("spaceCard"))->isVisible());
        settle();
        QVERIFY(overflowing(window.get()).isEmpty());
        shoot(window.get(), QStringLiteral("60-space-on-its-own"));

        // Not now closes the card, and the window with it.
        QMetaObject::invokeMethod(find(window.get(), QStringLiteral("spaceClose")), "clicked");
        QTRY_VERIFY(!window->isVisible());
        QVERIFY(c.chore().isEmpty());

        // Every other card opens the same way, as Search opens it.
        const auto printing = [&chores](const QString &name) { return QStringLiteral("cat '") + chores + name + QLatin1Char('\''); };
        SystemUpdate *update = c.systemUpdate();
        update->setElevate({});
        update->setLookScript(QStringLiteral("cat '" FIXTURES_DIR "/update/look.txt'"));
        update->setReport({});
        AppInstall *install = c.appInstall();
        install->setElevate({});
        install->setSearchScript(printing(QStringLiteral("install-search.txt")));
        install->setReport({});
        c.healthCheck()->setLookScript(printing(QStringLiteral("health-look.txt")));
        c.healthCheck()->setReport({});
        c.restorePoints()->setLookScript(printing(QStringLiteral("restore-look.txt")));
        c.restorePoints()->setReport({});
        c.communityApps()->setElevate({});
        c.communityApps()->setLookScript(printing(QStringLiteral("community-look.txt")));
        c.communityApps()->setReport({});
        const struct { const char *name; const char *query; const char *card; const char *stage; const char *title; } each[] = {
            {"update", "", "updateCard", "ready", "Update"},
            {"install", "pdf editor", "installCard", "results", "Install a new app"},
            {"health", "", "healthCard", "done", "Health check"},
            {"restore", "", "restoreCard", "ready", "Restore points"},
            {"community", "", "communityCard", "ready", "Community apps"},
        };
        for (const auto &card : each) {
            const QString name = QString::fromLatin1(card.name);
            QVERIFY2(c.openChore(name, QString::fromLatin1(card.query)), card.name);
            window->show();
            QCOMPARE(window->title(), QString::fromLatin1(card.title));
            QTRY_VERIFY2(find(window.get(), QString::fromLatin1(card.card)) && find(window.get(), QString::fromLatin1(card.card))->isVisible(), card.name);
            QObject *job = c.property(card.name).value<QObject *>();
            QTRY_COMPARE(job->property("stage").toString(), QString::fromLatin1(card.stage));
            settle();
            QVERIFY2(overflowing(window.get()).isEmpty(), card.name);
            shoot(window.get(), QStringLiteral("61-") + name + QStringLiteral("-on-its-own"));
            QMetaObject::invokeMethod(job, "close");
            QTRY_VERIFY2(!window->isVisible(), card.name);
        }

        // A card that hands over to another keeps the window: Health check's
        // Free up space opens in the same window rather than closing it.
        QVERIFY(c.openChore(QStringLiteral("health")));
        window->show();
        QTRY_COMPARE(c.healthCheck()->property("stage").toString(), QStringLiteral("done"));
        QVERIFY(c.openChore(QStringLiteral("space")));
        QTRY_COMPARE(space->stage(), QStringLiteral("ready"));
        settle();
        QVERIFY(window->isVisible());
        QCOMPARE(window->title(), QStringLiteral("Free up space"));
        QVERIFY(find(window.get(), QStringLiteral("spaceCard")) && find(window.get(), QStringLiteral("spaceCard"))->isVisible());
    }

    void theHelpersShowEachStep()
    {
        QTemporaryDir store;
        Helpers c(store.path());
        const QString chores = QStringLiteral(FIXTURES_DIR "/chores/");
        const auto printing = [&chores](const QString &name) { return QStringLiteral("cat '") + chores + name + QLatin1Char('\''); };
        // Everything the cards would run is the test's own.
        SpaceCleanup *space = c.spaceCleanup();
        space->setElevate({});
        space->setLookScript(printing(QStringLiteral("space-look.txt")));
        space->setYoursScript(printing(QStringLiteral("yours-look.txt")));
        space->setAdmin(adminStandIn(QStringLiteral("echo '@@ step pkgcache'; sleep 0.3; echo '@@ step journal'")));
        space->setPersonScript(QStringLiteral("echo '@@ step cache'"));
        space->setFreeScript(QStringLiteral("echo 52500000"));
        AppInstall *install = c.appInstall();
        install->setElevate({});
        install->setSearchScript(printing(QStringLiteral("install-search.txt")));
        install->setAdmin(adminStandIn(QStringLiteral("echo 'Packages (1) pdfarranger-1.11-1'; echo 'installing pdfarranger...'")));
        install->setOpenScript(QStringLiteral("true"));
        install->setYoursScript(printing(QStringLiteral("yours-look.txt")));
        RestorePoints *restore = c.restorePoints();
        restore->setLookScript(printing(QStringLiteral("restore-look.txt")));
        HealthCheck *health = c.healthCheck();
        health->setLookScript(printing(QStringLiteral("health-look.txt")));
        CommunityApps *community = c.communityApps();
        community->setElevate({});
        community->setLookScript(printing(QStringLiteral("community-look.txt")));
        community->setReviewScript(printing(QStringLiteral("community-review.txt")));
        community->setAdmin(adminStandIn(QStringLiteral("echo '@@ step build'; echo '==> Making package: visual-studio-code-bin'")));
        QQmlApplicationEngine engine;
        std::unique_ptr<QQuickWindow> window(open(engine, c));
        QVERIFY(window);
        QQuickWindow *full = window.get();
        const auto opens = [&](const QString &name, const QString &query = QString()) {
            if (!c.openChore(name, query))
                return false;
            full->show();
            return QTest::qWaitForWindowExposed(full);
        };
        full->setGeometry(0, 0, 560, 800);

        const auto shows = [&](const QString &card, const QString &shot) {
            QTRY_VERIFY2(find(full, card) && find(full, card)->isVisible(), qPrintable(shot + QLatin1Char(' ') + c.chore()));
            settle();
            const QStringList past = overflowing(full);
            if (!past.isEmpty())
                qWarning().noquote() << shot << past;
            QVERIFY(past.isEmpty());
            shoot(full, shot);
        };

        QVERIFY(opens(QStringLiteral("space")));
        QTRY_COMPARE(space->stage(), QStringLiteral("ready"));
        shows(QStringLiteral("spaceCard"), QStringLiteral("31-space-ready"));
        QMetaObject::invokeMethod(find(full, QStringLiteral("spaceButton")), "clicked");
        QTRY_COMPARE(space->stage(), QStringLiteral("done"));
        shows(QStringLiteral("spaceCard"), QStringLiteral("32-space-done"));

        QVERIFY(opens(QStringLiteral("install"), QStringLiteral("pdf editor")));
        QTRY_COMPARE(install->stage(), QStringLiteral("results"));
        shows(QStringLiteral("installCard"), QStringLiteral("33-install-results"));
        install->install(QStringLiteral("shelf:pdfarranger"));
        QTRY_COMPARE(install->stage(), QStringLiteral("done"));
        shows(QStringLiteral("installCard"), QStringLiteral("34-install-done"));
        // Removing one: the installed apps the words fit, then a yes.
        install->setSearchScript(printing(QStringLiteral("remove-search.txt")));
        QVERIFY(opens(QStringLiteral("install"), QStringLiteral("uninstall MaiN Keyboard")));
        QTRY_COMPARE(install->stage(), QStringLiteral("results"));
        QVERIFY(install->removing());
        shows(QStringLiteral("installCard"), QStringLiteral("46-remove-results"));
        install->remove(QStringLiteral("shelf:main-keyboard-git"));
        QTRY_COMPARE(install->stage(), QStringLiteral("confirm"));
        shows(QStringLiteral("installCard"), QStringLiteral("47-remove-confirm"));
        install->confirmRemove();
        QTRY_COMPARE(install->stage(), QStringLiteral("removed"));
        shows(QStringLiteral("installCard"), QStringLiteral("48-remove-done"));
        install->setSearchScript(printing(QStringLiteral("install-search.txt")));
        // Your apps: what the person added, biggest first, from Free up
        // space's row; then a yes before one goes.
        QVERIFY(opens(QStringLiteral("space")));
        QTRY_COMPARE(space->stage(), QStringLiteral("ready"));
        QTRY_VERIFY(find(full, QStringLiteral("spaceYours")) && find(full, QStringLiteral("spaceYours"))->isVisible());
        full->setGeometry(0, 0, 560, 1500);
        shows(QStringLiteral("spaceCard"), QStringLiteral("49-space-your-apps-row"));
        full->setGeometry(0, 0, 560, 800);
        QMetaObject::invokeMethod(find(full, QStringLiteral("spaceYours")), "clicked");
        QTRY_COMPARE(install->stage(), QStringLiteral("results"));
        QVERIFY(install->yours());
        QCOMPARE(c.chore(), QStringLiteral("install"));
        QCOMPARE(full->title(), QStringLiteral("Your apps"));
        shows(QStringLiteral("installCard"), QStringLiteral("50-your-apps"));
        install->remove(QStringLiteral("flathub:com.valvesoftware.Steam"));
        QTRY_COMPARE(install->stage(), QStringLiteral("confirm"));
        QTRY_VERIFY(find(full, QStringLiteral("yoursPicked")) && find(full, QStringLiteral("yoursPicked"))->isVisible());
        shows(QStringLiteral("installCard"), QStringLiteral("51-your-apps-confirm"));
        install->back();
        QCOMPARE(install->stage(), QStringLiteral("results"));
        install->close();

        // Tidy up Downloads, from Free up space's last row: the groups, one
        // opened to its files, then what went to the Trash.
        QTemporaryDir tidyHome(QDir::homePath() + QStringLiteral("/sheet-tidy-XXXXXX"));
        const QString downloads = tidyHome.filePath(QStringLiteral("Downloads"));
        const auto put = [](const QString &path, const QByteArray &bytes, int daysAgo = 0) {
            QDir().mkpath(QFileInfo(path).absolutePath());
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly))
                return false;
            file.write(bytes);
            if (daysAgo > 0) {
                const QDateTime then = QDateTime::currentDateTime().addDays(-daysAgo);
                file.setFileTime(then, QFileDevice::FileModificationTime);
                file.setFileTime(then, QFileDevice::FileAccessTime);
            }
            return true;
        };
        QVERIFY(put(downloads + QStringLiteral("/ZoomInstaller.exe"), QByteArray(180000, 'z')));
        QVERIFY(put(downloads + QStringLiteral("/report.pdf"), QByteArray(820000, 'r')));
        QVERIFY(put(downloads + QStringLiteral("/report (1).pdf"), QByteArray(820000, 'r')));
        QVERIFY(put(downloads + QStringLiteral("/budget (2).xlsx"), QByteArray(40000, 'b')));
        QVERIFY(put(downloads + QStringLiteral("/budget.xlsx"), QByteArray(40000, 'b')));
        QVERIFY(put(downloads + QStringLiteral("/Obsidian-1.7.AppImage"), QByteArray(98000, 'o')));
        QVERIFY(put(downloads + QStringLiteral("/holiday.mov"), QByteArray(3900000, 'h'), 60));
        QVERIFY(put(downloads + QStringLiteral("/Screenshot_20261006_101500.png"), QByteArray(54000, 's')));
        QVERIFY(put(downloads + QStringLiteral("/Games temp/data.bin"), QByteArray(2400000, 'g')));
        TidyUp *tidy = c.tidyUp();
        tidy->setFolders({downloads});
        tidy->setTrashOpener({});
        QVERIFY(opens(QStringLiteral("space")));
        QTRY_COMPARE(space->stage(), QStringLiteral("ready"));
        QTRY_VERIFY(find(full, QStringLiteral("spaceTidy")) && find(full, QStringLiteral("spaceTidy"))->isVisible());
        QMetaObject::invokeMethod(find(full, QStringLiteral("spaceTidy")), "clicked");
        QTRY_COMPARE(tidy->stage(), QStringLiteral("ready"));
        QCOMPARE(c.chore(), QStringLiteral("tidy"));
        QCOMPARE(full->title(), QStringLiteral("Tidy up Downloads"));
        shows(QStringLiteral("tidyCard"), QStringLiteral("52-tidy-ready"));
        QQuickItem *installers = find(full, QStringLiteral("tidy-installers"));
        QVERIFY(installers);
        // Opening a group: a tap anywhere but its box.
        QTest::mouseClick(full, Qt::LeftButton, {}, installers->mapToScene(QPointF(installers->width() / 2, installers->height() / 2)).toPoint());
        QTRY_VERIFY(find(full, QStringLiteral("tidyFile-ZoomInstaller.exe")) && find(full, QStringLiteral("tidyFile-ZoomInstaller.exe"))->isVisible());
        shows(QStringLiteral("tidyCard"), QStringLiteral("53-tidy-group-open"));
        // Its box unticks the whole group, and a file's own row ticks it back.
        QQuickItem *box = find(full, QStringLiteral("tidyTick-installers"));
        QVERIFY(box);
        QTest::mouseClick(full, Qt::LeftButton, {}, box->mapToScene(QPointF(box->width() / 2, box->height() / 2)).toPoint());
        QTRY_COMPARE(tidy->chosenCount(), 2);
        QMetaObject::invokeMethod(find(full, QStringLiteral("tidyFile-ZoomInstaller.exe")), "clicked");
        QTRY_COMPARE(tidy->chosenCount(), 3);
        QMetaObject::invokeMethod(find(full, QStringLiteral("tidyButton")), "clicked");
        QTRY_COMPARE(tidy->stage(), QStringLiteral("done"));
        QCOMPARE(tidy->moved(), 3);
        shows(QStringLiteral("tidyCard"), QStringLiteral("54-tidy-done"));
        QVERIFY(QFile::exists(downloads + QStringLiteral("/report.pdf")));
        QVERIFY(!QFile::exists(downloads + QStringLiteral("/report (1).pdf")));
        QMetaObject::invokeMethod(find(full, QStringLiteral("tidyClose")), "clicked");
        QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/Trash")).removeRecursively();
        // A narrow window, as a Bento column.
        full->setGeometry(0, 0, 420, 800);
        QVERIFY(opens(QStringLiteral("tidy")));
        QTRY_COMPARE(tidy->stage(), QStringLiteral("ready"));
        shows(QStringLiteral("tidyCard"), QStringLiteral("55-tidy-narrow"));
        tidy->close();
        full->setGeometry(0, 0, 560, 800);

        QVERIFY(opens(QStringLiteral("restore")));
        QTRY_COMPARE(restore->stage(), QStringLiteral("ready"));
        shows(QStringLiteral("restoreCard"), QStringLiteral("35-restore-ready"));
        restore->choose(313);
        shows(QStringLiteral("restoreCard"), QStringLiteral("36-restore-chosen"));

        QVERIFY(opens(QStringLiteral("health")));
        QTRY_COMPARE(health->stage(), QStringLiteral("done"));
        shows(QStringLiteral("healthCard"), QStringLiteral("37-health"));
        // A problem no card fixes is copied, to ask someone about.
        QQuickItem *services = find(full, QStringLiteral("health-services"));
        QVERIFY(services);
        QCOMPARE(services->property("trailing").toString(), QStringLiteral("Copy for help"));
        QMetaObject::invokeMethod(services, "clicked");
        QTRY_COMPARE(services->property("trailing").toString(), QStringLiteral("Copied"));
        QVERIFY(QGuiApplication::clipboard()->text().contains(QStringLiteral("bluetooth-autoconnect.service")));
        QVERIFY(QGuiApplication::clipboard()->text().contains(QStringLiteral("About my computer:")));
        shows(QStringLiteral("healthCard"), QStringLiteral("37b-health-copied"));

        QVERIFY(opens(QStringLiteral("community")));
        QTRY_COMPARE(community->stage(), QStringLiteral("ready"));
        shows(QStringLiteral("communityCard"), QStringLiteral("38-community-ready"));
        community->look(QStringLiteral("visual-studio-code-bin"));
        QTRY_COMPARE(community->stage(), QStringLiteral("reviewed"));
        shows(QStringLiteral("communityCard"), QStringLiteral("39-community-review"));

        // What changed, from a look written for today, so the days read as
        // they would on the computer.
        QTemporaryDir changesDir;
        {
            const QDateTime now = QDateTime::currentDateTime();
            const auto stamp = [&now](int daysAgo, const QString &time) {
                return QStringLiteral("[%1T%2+0000]").arg(now.date().addDays(-daysAgo).toString(Qt::ISODate), time);
            };
            const auto secs = [&now](int daysAgo) { return QString::number(now.addDays(-daysAgo).toSecsSinceEpoch()); };
            QFile look(changesDir.filePath(QStringLiteral("look.txt")));
            QVERIFY(look.open(QIODevice::WriteOnly));
            look.write(QStringLiteral(
                "@@ pacman\n"
                "%1 [ALPM] upgraded plasma-workspace (6.7.3-1 -> 6.7.4-1)\n"
                "%1 [ALPM] upgraded linux-cachyos (6.17.1-2 -> 6.17.2-1)\n"
                "%1 [ALPM] upgraded mesa (1:25.2.3-1 -> 1:25.2.4-1)\n"
                "%1 [ALPM] upgraded firefox (131.0-1 -> 132.0-1)\n"
                "%1 [ALPM] upgraded libfoo (1-1 -> 2-1)\n"
                "%2 [ALPM] installed vlc (3.0.21-1)\n"
                "%2 [ALPM] installed libvlc-dep (1-1)\n"
                "@@ explicit\nfirefox\nvlc\n"
                "@@ settings\n%3 kwinrc\n%3 kdeglobals\n%4 kcminputrc\n"
                "@@ failed\nsystem|bluetooth.service|%3|Starting Bluetooth service...\037Failed to set mode: Blocked through "
                "rfkill (0x12)|Bluetooth service\n"
                "@@ kernel\n6.17.1-2-cachyos\nmissing\n"
                "@@ snapper\nyes\n@@ end\n")
                           .arg(stamp(1, QStringLiteral("09:10:00")), stamp(3, QStringLiteral("08:00:00")), secs(0), secs(3))
                           .toUtf8());
        }
        // Core's copies of the settings, taken an hour ago; since then the
        // screen's scale, a shortcut and a widget changed.
        QTemporaryDir configDir;
        QTemporaryDir historyDir;
        const auto setting = [&configDir](const QString &file, const QByteArray &text) {
            QFile out(configDir.filePath(file));
            QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
            out.write(text);
        };
        const QByteArray outputs = R"([{"name":"outputs","data":[{"connectorName":"eDP-1","scale":1.75,"transform":"Normal"}]}])";
        const QString applets = QStringLiteral("plasma-org.kde.plasma.desktop-appletsrc");
        setting(QStringLiteral("kwinoutputconfig.json"), outputs);
        setting(QStringLiteral("kglobalshortcutsrc"), "[kwin]\nTile windows=Meta+T,Meta+T,Tile windows\n");
        setting(applets, "[Containments][1]\nplugin=org.kde.panel\n");
        const QDateTime changedAt = QDateTime::currentDateTime().addSecs(-1800);
        {
            SettingsHistory history(historyDir.path(), configDir.path());
            history.look(QDateTime::currentDateTime().addSecs(-3600));
            QByteArray scaled = outputs;
            scaled.replace("1.75", "1.5");
            setting(QStringLiteral("kwinoutputconfig.json"), scaled);
            setting(QStringLiteral("kglobalshortcutsrc"), "[kwin]\nTile windows=none,Meta+T,Tile windows\n");
            setting(applets, "[Containments][1]\nplugin=org.kde.panel\n\n[Containments][1][Applets][7]\n"
                             "plugin=org.kde.plasma.digitalclock\n");
            QCOMPARE(history.look(changedAt).size(), 3);
        }
        RecentChanges *recent = c.recentChanges();
        recent->setSettingsHistory(historyDir.path(), configDir.path());
        recent->setProgram(QStringLiteral("/usr/bin/split-rock"));
        recent->setLookScript(QStringLiteral("cat '") + changesDir.filePath(QStringLiteral("look.txt")) + QLatin1Char('\''));
        recent->setRecords([] {
            ChangeRunner::Record pointer;
            pointer.id = QStringLiteral("c1");
            pointer.made = QDateTime::currentDateTime().addSecs(-3600);
            pointer.proposal.title = QStringLiteral("Use the Bibata pointer");
            pointer.proposal.undo = QStringLiteral("plasma-apply-cursortheme breeze_cursors");
            pointer.status = QStringLiteral("done");
            return QList<ChangeRunner::Record>{pointer};
        });
        QVERIFY(opens(QStringLiteral("changes")));
        QTRY_COMPARE(recent->stage(), QStringLiteral("ready"));
        shows(QStringLiteral("changesCard"), QStringLiteral("42-changes-ready"));
        QQuickItem *changes = find(full, QStringLiteral("changesCard"));
        QVERIFY(changes);
        changes->setProperty("all", true);

        // Each row says what changed, with its way back on it.
        const QString secs = QString::number(changedAt.toSecsSinceEpoch());
        QQuickItem *scale = find(full, QStringLiteral("change-setting-kwinoutputconfig.json-%1-eDP-1").arg(secs));
        QVERIFY(scale);
        QCOMPARE(scale->property("title").toString(), QStringLiteral("Built-in screen: scale 175% to 150%"));
        QCOMPARE(scale->property("button").toString(), QStringLiteral("Put back"));
        QQuickItem *keys = find(full, QStringLiteral("change-setting-kglobalshortcutsrc-%1").arg(secs));
        QVERIFY(keys);
        QCOMPARE(keys->property("title").toString(), QStringLiteral("Shortcut for Tile windows removed"));
        QCOMPARE(keys->property("button").toString(), QStringLiteral("Put back"));
        // Put back asks first, as a change from a card.
        QMetaObject::invokeMethod(scale, "clicked");
        QTRY_VERIFY(changes->property("asking").toBool());
        QQuickItem *heading = find(full, QStringLiteral("changesHeading"));
        QVERIFY(heading);
        QCOMPARE(heading->property("text").toString(), QStringLiteral("Put it back as it was?"));
        QVERIFY(find(full, QStringLiteral("changesShowMe"))->isVisible());
        shows(QStringLiteral("changesCard"), QStringLiteral("42a-changes-put-back"));
        QMetaObject::invokeMethod(find(full, QStringLiteral("changesNotNow")), "clicked");
        QTRY_VERIFY(!changes->property("asking").toBool());

        // A service that stopped working starts again, with what it said.
        QQuickItem *stopped = find(full, QStringLiteral("change-service-bluetooth.service"));
        QVERIFY(stopped);
        QCOMPARE(stopped->property("button").toString(), QStringLiteral("Start it again"));
        QVERIFY(stopped->property("detail").toString().startsWith(QStringLiteral("Said \"Failed to set mode")));
        QMetaObject::invokeMethod(stopped, "clicked");
        QTRY_VERIFY(changes->property("asking").toBool());
        QCOMPARE(heading->property("text").toString(), QStringLiteral("Start Bluetooth service again?"));
        shows(QStringLiteral("changesCard"), QStringLiteral("42b-changes-start-again"));
        QMetaObject::invokeMethod(find(full, QStringLiteral("changesNotNow")), "clicked");
        QTRY_VERIFY(!changes->property("asking").toBool());

        // A widget has no safe way back yet: copied for help, exactly.
        QQuickItem *widget = find(full, QStringLiteral("change-setting-%1-%2").arg(applets, secs));
        QVERIFY(widget);
        QCOMPARE(widget->property("title").toString(), QStringLiteral("Added the Digital clock widget"));
        QCOMPARE(widget->property("button").toString(), QStringLiteral("Copy for help"));
        QMetaObject::invokeMethod(widget, "clicked");
        QTRY_COMPARE(widget->property("button").toString(), QStringLiteral("Copied"));
        const QString copied = QGuiApplication::clipboard()->text();
        QVERIFY2(copied.contains(QStringLiteral("Exactly, in ~/.config:")), qPrintable(copied));
        QVERIFY(copied.contains(QStringLiteral("plugin: not set to org.kde.plasma.digitalclock")));
        QVERIFY(copied.contains(QStringLiteral("About my computer:")));
        shows(QStringLiteral("changesCard"), QStringLiteral("42c-changes-copied"));
        // The whole card at once, for the screens.
        full->setGeometry(0, 0, 560, 1500);
        shows(QStringLiteral("changesCard"), QStringLiteral("45-changes-whole"));
        full->setGeometry(0, 0, 560, 800);
        recent->setRecords({});
        QVERIFY(opens(QStringLiteral("changes")));
        QTRY_COMPARE(recent->stage(), QStringLiteral("ready"));
        shows(QStringLiteral("changesCard"), QStringLiteral("43-changes-nothing-to-undo"));

        // A narrow window, as a Bento column: nothing runs past its edge.
        full->setGeometry(0, 0, 420, 800);
        QVERIFY(opens(QStringLiteral("changes")));
        QTRY_COMPARE(recent->stage(), QStringLiteral("ready"));
        shows(QStringLiteral("changesCard"), QStringLiteral("44-changes-narrow"));
        QVERIFY(opens(QStringLiteral("health")));
        QTRY_COMPARE(health->stage(), QStringLiteral("done"));
        shows(QStringLiteral("healthCard"), QStringLiteral("40-health-narrow"));
        QVERIFY(opens(QStringLiteral("space")));
        QTRY_COMPARE(space->stage(), QStringLiteral("ready"));
        shows(QStringLiteral("spaceCard"), QStringLiteral("41-space-narrow"));
        space->close();
        QTRY_VERIFY(!full->isVisible());
    }
};

QTEST_MAIN(TestSheet)
#include "test_sheet.moc"
