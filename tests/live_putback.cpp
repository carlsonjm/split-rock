// SPDX-License-Identifier: GPL-2.0-or-later
//
// What changed's ways back, tried on a real desktop: each kind of setting is
// changed as the person would, What changed is asked to put it back, and then
// to undo that, and the setting is read back after each step. Whatever
// happens, every setting ends as it began.
//
// It changes the signed-in person's own settings, so it is never part of the
// ordinary tests: it is built only with -DSPLIT_ROCK_LIVE_TESTS=ON and run by
// hand, inside the desktop session, on a computer kept for testing.
//
//   build/live/tests/live_putback [test name]

#include "app/ChangeRunner.h"
#include "app/Helpers.h"
#include "app/RecentChanges.h"
#include "settings/SettingsHistory.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <functional>

using namespace splitrock;

namespace {

QString configHome()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
}

// Runs a shell command as the person, and says what it printed.
QString sh(const QString &command, int *exitCode = nullptr)
{
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), command});
    p.waitForFinished(30000);
    if (exitCode)
        *exitCode = p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
    return QString::fromUtf8(p.readAll()).trimmed();
}

// A setting's value as its file has it now; empty with set false when it
// isn't there.
QString valueOf(const QString &file, const QString &group, const QString &key, bool *set = nullptr)
{
    QFile f(QDir(configHome()).filePath(file));
    const QByteArray text = f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    const auto read = file == QLatin1String("kwinoutputconfig.json") ? SettingsHistory::readOutputs(text)
                                                                     : SettingsHistory::readIni(text);
    const QMap<QString, QString> keys = read.value(group);
    if (set)
        *set = keys.contains(key);
    return keys.value(key);
}

} // namespace

class LivePutBack : public QObject
{
    Q_OBJECT

    QTemporaryDir m_store;
    QTemporaryDir m_history;
    std::unique_ptr<Helpers> m_helpers;

    RecentChanges *recent() const { return m_helpers->recentChanges(); }

    // Core's copies, taken now, as they would be before the person's change.
    void takeCopies()
    {
        SettingsHistory(m_history.path(), configHome()).look(QDateTime::currentDateTime().addSecs(-1));
    }

    // What changed, looked at again: its row for that setting.
    std::optional<QVariantMap> rowFor(const QString &file, const QString &group, const QString &key)
    {
        recent()->check();
        if (!QTest::qWaitFor([this] { return recent()->stage() == QLatin1String("ready")
                                          || recent()->stage() == QLatin1String("nothing"); }, 60000))
            return std::nullopt;
        const QString exact = QStringLiteral("%1 [%2] %3:").arg(file, group, key);
        QStringList seen;
        for (const QVariant &v : recent()->itemList()) {
            const QVariantMap item = v.toMap();
            seen.append(item.value(QStringLiteral("title")).toString() + QStringLiteral(" (")
                        + item.value(QStringLiteral("action")).toString() + QLatin1Char(')'));
            for (const QVariant &line : item.value(QStringLiteral("exact")).toList()) {
                if (line.toString().startsWith(exact))
                    return item;
            }
        }
        qWarning().noquote() << "What changed has no row for" << exact << "among:" << seen.join(QStringLiteral("; "));
        return std::nullopt;
    }

    // The whole way round for one setting: changed as the person would, put
    // back from its row, undone, and in the end as it began. live, when given,
    // reads the running desktop's own value.
    void roundTrip(const QString &file, const QString &group, const QString &key, const QString &change,
                   const QString &restore, const std::function<QString()> &live = {})
    {
        bool wasSet = false;
        const QString before = valueOf(file, group, key, &wasSet);
        const QString liveBefore = live ? live() : QString();
        qInfo().noquote() << QStringLiteral("%1 [%2] %3 is %4%5").arg(file, group, key,
                                                                         wasSet ? before : QStringLiteral("(not set)"),
                                                                         live ? QStringLiteral(", live %1").arg(liveBefore) : QString());
        auto ends = qScopeGuard([&] {
            sh(restore);
            QTest::qWait(1500);
            bool set = false;
            const QString now = valueOf(file, group, key, &set);
            if (set != wasSet || now != before)
                qWarning().noquote() << QStringLiteral("NOT AS IT BEGAN: %1 [%2] %3 is %4, was %5 — run: %6")
                                            .arg(file, group, key, now, before, restore);
            else
                qInfo().noquote() << QStringLiteral("%1 [%2] %3 is as it began").arg(file, group, key);
        });

        takeCopies();
        int code = 0;
        const QString said = sh(change, &code);
        QVERIFY2(code == 0, qPrintable(QStringLiteral("the change didn't run: ") + said));
        QTRY_VERIFY_WITH_TIMEOUT(valueOf(file, group, key) != before, 10000);
        const QString changed = valueOf(file, group, key);
        qInfo().noquote() << QStringLiteral("changed to %1").arg(changed);

        const std::optional<QVariantMap> row = rowFor(file, group, key);
        QVERIFY(row);
        qInfo().noquote() << QStringLiteral("row: %1 · %2 · %3").arg(row->value(QStringLiteral("title")).toString(),
                                                                     row->value(QStringLiteral("detail")).toString(),
                                                                     row->value(QStringLiteral("actionLabel")).toString());
        QCOMPARE(row->value(QStringLiteral("action")).toString(), QStringLiteral("putback"));

        QVERIFY(m_helpers->putBack(row->value(QStringLiteral("id")).toString()));
        QTRY_VERIFY_WITH_TIMEOUT(recent()->stage() == QLatin1String("ready") || recent()->stage() == QLatin1String("nothing"), 60000);
        qInfo().noquote() << QStringLiteral("said: %1").arg(recent()->notice());
        const ChangeRunner::Record made = m_helpers->changeRunner()->records().constFirst();
        QVERIFY2(made.status == QLatin1String("done"), qPrintable(made.output));
        QTRY_COMPARE_WITH_TIMEOUT(valueOf(file, group, key), before, 10000);
        if (live)
            QTRY_COMPARE_WITH_TIMEOUT(live(), liveBefore, 10000);
        qInfo().noquote() << "put back: the file and the desktop have the value from before";

        QVERIFY(m_helpers->undoChange(made.id));
        QTRY_VERIFY_WITH_TIMEOUT(m_helpers->changeRunner()->record(made.id)->status != QLatin1String("done"), 60000);
        const ChangeRunner::Record undone = *m_helpers->changeRunner()->record(made.id);
        QVERIFY2(undone.status == QLatin1String("undone"), qPrintable(undone.output));
        QTRY_COMPARE_WITH_TIMEOUT(valueOf(file, group, key), changed, 10000);
        qInfo().noquote() << "undone: back to the person's change";
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY2(qEnvironmentVariableIsSet("WAYLAND_DISPLAY") || qEnvironmentVariableIsSet("DISPLAY"),
                 "Run it inside the desktop session.");
        QVERIFY2(!ChangeRunner::bubblewrap().isEmpty(), "bubblewrap is needed to make a change.");
        m_helpers = std::make_unique<Helpers>(m_store.path());
        recent()->setSettingsHistory(m_history.path(), configHome());
        recent()->setProgram(QStringLiteral(SPLIT_ROCK_PROGRAM));
        qInfo().noquote() << sh(QStringLiteral("plasmashell --version; kwin_wayland --version; kscreen-doctor --version 2>/dev/null"));
    }

    // A screen's scale, on the first screen that is on.
    void screenScale()
    {
        const QJsonArray outputs = QJsonDocument::fromJson(sh(QStringLiteral("kscreen-doctor -j")).toUtf8())
                                       .object()
                                       .value(QStringLiteral("outputs"))
                                       .toArray();
        QString name;
        double scale = 1;
        for (const QJsonValue &o : outputs) {
            if (o[QStringLiteral("enabled")].toBool()) {
                name = o[QStringLiteral("name")].toString();
                scale = o[QStringLiteral("scale")].toDouble();
                break;
            }
        }
        if (name.isEmpty())
            QSKIP("No screen is on.");
        const QString other = qFuzzyCompare(scale, 1.25) ? QStringLiteral("1.5") : QStringLiteral("1.25");
        const auto live = [name] {
            for (const QJsonValue &o : QJsonDocument::fromJson(sh(QStringLiteral("kscreen-doctor -j")).toUtf8())
                                           .object()
                                           .value(QStringLiteral("outputs"))
                                           .toArray()) {
                if (o[QStringLiteral("name")].toString() == name)
                    return QString::number(o[QStringLiteral("scale")].toDouble(), 'g', 6);
            }
            return QString();
        };
        roundTrip(QStringLiteral("kwinoutputconfig.json"), name, QStringLiteral("scale"),
                  QStringLiteral("kscreen-doctor output.%1.scale.%2").arg(name, other),
                  QStringLiteral("kscreen-doctor output.%1.scale.%2").arg(name, QString::number(scale, 'g', 6)), live);
    }

    // An effect turned off, as in System Settings.
    void blurEffect()
    {
        bool set = false;
        const QString was = valueOf(QStringLiteral("kwinrc"), QStringLiteral("Plugins"), QStringLiteral("blurEnabled"), &set);
        const bool on = !set || was != QLatin1String("false");
        const auto write = [](bool value) {
            return QStringLiteral("kwriteconfig6 --file kwinrc --group Plugins --key blurEnabled --notify -- %1 && "
                                  "{ qdbus6 org.kde.KWin /KWin reconfigure || qdbus org.kde.KWin /KWin reconfigure; }")
                .arg(value ? QStringLiteral("true") : QStringLiteral("false"));
        };
        const auto live = [] {
            return sh(QStringLiteral("qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.isEffectLoaded blur 2>/dev/null"));
        };
        // Blur may be off by hand, out of step with the settings: KWin is
        // asked to follow its settings first, so Put back has something true
        // to compare with, and blur is left on or off as it was found.
        const QString found = live();
        sh(QStringLiteral("qdbus6 org.kde.KWin /KWin reconfigure"));
        QTest::qWait(1500);
        const QString restore = (set ? write(on) : QStringLiteral("kwriteconfig6 --file kwinrc --group Plugins --key blurEnabled --delete && "
                                                                  "{ qdbus6 org.kde.KWin /KWin reconfigure || true; }"))
            + QStringLiteral("; sleep 1; qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.%1 blur")
                  .arg(found == QLatin1String("true") ? QStringLiteral("loadEffect") : QStringLiteral("unloadEffect"));
        qInfo().noquote() << QStringLiteral("blur was live %1, and %2 once KWin follows its settings").arg(found, live());
        auto leave = qScopeGuard([&] {
            if (live() != found)
                qWarning().noquote() << QStringLiteral("NOT AS IT BEGAN: blur is live %1, was %2 — run: qdbus6 org.kde.KWin /Effects "
                                                       "org.kde.kwin.Effects.%3 blur")
                                            .arg(live(), found, found == QLatin1String("true") ? QStringLiteral("loadEffect")
                                                                                                 : QStringLiteral("unloadEffect"));
        });
        roundTrip(QStringLiteral("kwinrc"), QStringLiteral("Plugins"), QStringLiteral("blurEnabled"), write(!on), restore, live);
    }

    // A keyboard shortcut removed, as in System Settings: through the running
    // desktop, which writes the file itself.
    void shortcut()
    {
        const QString file = QStringLiteral("kglobalshortcutsrc");
        const QString action = QStringLiteral("Window Minimize");
        const QString field = valueOf(file, QStringLiteral("kwin"), action);
        const QString keys = field.section(QLatin1Char(','), 0, 0);
        if (field.isEmpty() || keys.isEmpty() || keys == QLatin1String("none"))
            QSKIP("Minimize Window has no shortcut to remove here.");
        const QString name = field.section(QLatin1Char(','), 2);
        const QString program = QStringLiteral(SPLIT_ROCK_PROGRAM);
        const auto set = [&](const QString &to) {
            return QStringLiteral("'%1' --set-shortcut kwin '%2' '%3' '%4'").arg(program, action, to, name);
        };
        roundTrip(file, QStringLiteral("kwin"), action, set(QString()), set(keys));
    }

    // The keyboard layout indicator, in the layouts' own file.
    void keyboardLayouts()
    {
        bool set = false;
        const QString was = valueOf(QStringLiteral("kxkbrc"), QStringLiteral("Layout"), QStringLiteral("ShowSingle"), &set);
        const bool on = set && was == QLatin1String("true");
        const auto write = [](const QString &value) {
            return QStringLiteral("kwriteconfig6 --file kxkbrc --group Layout --key ShowSingle --notify -- %1 && "
                                  "dbus-send --session --type=signal /Layouts org.kde.keyboard.reloadConfig")
                .arg(value);
        };
        roundTrip(QStringLiteral("kxkbrc"), QStringLiteral("Layout"), QStringLiteral("ShowSingle"),
                  write(on ? QStringLiteral("false") : QStringLiteral("true")),
                  set ? write(was) : QStringLiteral("kwriteconfig6 --file kxkbrc --group Layout --key ShowSingle --delete"));
    }

    // When the screen turns off, plugged in.
    void screenOff()
    {
        bool set = false;
        const QString group = QStringLiteral("AC][Display");
        const QString key = QStringLiteral("TurnOffDisplayIdleTimeoutSec");
        const QString was = valueOf(QStringLiteral("powerdevilrc"), group, key, &set);
        const auto write = [](const QString &value) {
            return QStringLiteral("kwriteconfig6 --file powerdevilrc --group AC --group Display --key TurnOffDisplayIdleTimeoutSec "
                                  "--notify -- %1 && qdbus6 org.kde.Solid.PowerManagement /org/kde/Solid/PowerManagement "
                                  "org.kde.Solid.PowerManagement.reparseConfiguration")
                .arg(value);
        };
        roundTrip(QStringLiteral("powerdevilrc"), group, key, write(was == QLatin1String("900") ? QStringLiteral("1200")
                                                                                               : QStringLiteral("900")),
                  set ? write(was)
                      : QStringLiteral("kwriteconfig6 --file powerdevilrc --group AC --group Display --key "
                                       "TurnOffDisplayIdleTimeoutSec --delete && qdbus6 org.kde.Solid.PowerManagement "
                                       "/org/kde/Solid/PowerManagement org.kde.Solid.PowerManagement.reparseConfiguration"));
    }

    // When the screen locks by itself.
    void screenLock()
    {
        bool set = false;
        const QString was = valueOf(QStringLiteral("kscreenlockerrc"), QStringLiteral("Daemon"), QStringLiteral("Timeout"), &set);
        const auto write = [](const QString &value) {
            return QStringLiteral("kwriteconfig6 --file kscreenlockerrc --group Daemon --key Timeout --notify -- %1 && "
                                  "qdbus6 org.freedesktop.ScreenSaver /ScreenSaver configure")
                .arg(value);
        };
        roundTrip(QStringLiteral("kscreenlockerrc"), QStringLiteral("Daemon"), QStringLiteral("Timeout"),
                  write(was == QLatin1String("15") ? QStringLiteral("20") : QStringLiteral("15")),
                  set ? write(was) : QStringLiteral("kwriteconfig6 --file kscreenlockerrc --group Daemon --key Timeout --delete"));
    }

    // The default app for text files.
    void defaultApp()
    {
        bool set = false;
        const QString group = QStringLiteral("Default Applications");
        const QString key = QStringLiteral("text/plain");
        const QString was = valueOf(QStringLiteral("mimeapps.list"), group, key, &set);
        QString other;
        for (const QString &id : {QStringLiteral("org.kde.kwrite.desktop"), QStringLiteral("org.kde.kate.desktop"),
                                  QStringLiteral("org.gnome.TextEditor.desktop"), QStringLiteral("firefox.desktop")}) {
            if (!QStandardPaths::locate(QStandardPaths::ApplicationsLocation, id).isEmpty() && id != was) {
                other = id;
                break;
            }
        }
        if (other.isEmpty())
            QSKIP("No second app here to make the default for text files.");
        const auto write = [&](const QString &value) {
            return QStringLiteral("kwriteconfig6 --file mimeapps.list --group 'Default Applications' --key text/plain --notify -- '%1'")
                .arg(value);
        };
        roundTrip(QStringLiteral("mimeapps.list"), group, key, write(other),
                  set ? write(was)
                      : QStringLiteral("kwriteconfig6 --file mimeapps.list --group 'Default Applications' --key text/plain --delete"));
    }

    // The wallpaper, where it is a picture.
    void wallpaper()
    {
        const QString file = QStringLiteral("plasma-org.kde.plasma.desktop-appletsrc");
        QFile f(QDir(configHome()).filePath(file));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const auto groups = SettingsHistory::readIni(f.readAll());
        QString group;
        for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
            if (it.key().endsWith(QLatin1String("][Wallpaper][org.kde.image][General")) && it.value().contains(QStringLiteral("Image"))) {
                group = it.key();
                break;
            }
        }
        if (group.isEmpty())
            QSKIP("The wallpaper here isn't a picture.");
        const QString was = valueOf(file, group, QStringLiteral("Image"));
        QString other;
        const QStringList pictures = sh(QStringLiteral("ls /usr/share/wallpapers/*/contents/images/*.* 2>/dev/null | head -n 40"))
                                         .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &p : pictures) {
            if (!was.contains(p.section(QLatin1Char('/'), 4, 4))) {
                other = p;
                break;
            }
        }
        if (other.isEmpty())
            QSKIP("No second picture here.");
        QString path = was;
        if (path.startsWith(QLatin1String("file://")))
            path = path.mid(7);
        roundTrip(file, group, QStringLiteral("Image"), QStringLiteral("plasma-apply-wallpaperimage '%1'").arg(other),
                  QStringLiteral("plasma-apply-wallpaperimage '%1'").arg(path));
    }

    // A service of the person's own that stopped: Start it again, once it
    // can run.
    void startAgain()
    {
        const QString units = QDir(configHome()).filePath(QStringLiteral("systemd/user"));
        QDir().mkpath(units);
        const QString flag = m_store.filePath(QStringLiteral("may-run"));
        const QString unit = QStringLiteral("split-rock-live-check.service");
        QFile file(QDir(units).filePath(unit));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QStringLiteral("[Unit]\nDescription=Split Rock's live check\n[Service]\nType=simple\n"
                                  "ExecStart=/bin/sh -c 'test -e %1 || { echo \"Not ready yet\"; exit 3; }; exec sleep 600'\n")
                       .arg(flag)
                       .toUtf8());
        file.close();
        auto ends = qScopeGuard([&] {
            sh(QStringLiteral("systemctl --user stop %1; systemctl --user reset-failed %1; rm -f '%2'; systemctl --user daemon-reload")
                   .arg(unit, file.fileName()));
        });
        sh(QStringLiteral("systemctl --user daemon-reload; systemctl --user start %1").arg(unit));
        QTRY_COMPARE_WITH_TIMEOUT(sh(QStringLiteral("systemctl --user is-failed %1").arg(unit)), QStringLiteral("failed"), 10000);

        recent()->check();
        QTRY_VERIFY_WITH_TIMEOUT(recent()->stage() == QLatin1String("ready"), 60000);
        QVariantMap row;
        for (const QVariant &v : recent()->itemList()) {
            if (v.toMap().value(QStringLiteral("id")).toString() == QStringLiteral("service-") + unit)
                row = v.toMap();
        }
        QVERIFY2(!row.isEmpty(), "What changed has no row for the stopped service.");
        qInfo().noquote() << QStringLiteral("row: %1 · %2").arg(row.value(QStringLiteral("title")).toString(),
                                                                row.value(QStringLiteral("detail")).toString());
        QVERIFY(row.value(QStringLiteral("detail")).toString().contains(QStringLiteral("Not ready yet")));
        QCOMPARE(row.value(QStringLiteral("action")).toString(), QStringLiteral("start"));

        // Still failing: it says so, with what it said.
        recent()->startAgain(row.value(QStringLiteral("id")).toString());
        QTRY_VERIFY_WITH_TIMEOUT(recent()->stage() == QLatin1String("ready") && !recent()->notice().isEmpty(), 60000);
        qInfo().noquote() << QStringLiteral("said: %1").arg(recent()->notice());
        QVERIFY(recent()->notice().contains(QStringLiteral("stopped again")));

        // Able to run: it runs.
        QFile(flag).open(QIODevice::WriteOnly);
        recent()->check();
        QTRY_VERIFY_WITH_TIMEOUT(recent()->stage() == QLatin1String("ready"), 60000);
        recent()->startAgain(row.value(QStringLiteral("id")).toString());
        QTRY_VERIFY_WITH_TIMEOUT(recent()->stage() == QLatin1String("ready") && recent()->notice().contains(QStringLiteral("running")),
                                 60000);
        qInfo().noquote() << QStringLiteral("said: %1").arg(recent()->notice());
        QCOMPARE(sh(QStringLiteral("systemctl --user is-active %1").arg(unit)), QStringLiteral("active"));
    }

    // Flathub's history as What changed reads it, to go back: each update's
    // commits, in full.
    void flathubCommits()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("flatpak")).isEmpty())
            QSKIP("No Flatpak here.");
        const QString history = sh(QStringLiteral("flatpak history --since=\"$(date -d '-60 days' +%Y-%m-%d) 00:00:00\" "
                                                  "--columns=time,change,application,installation,commit,old-commit | tail -n 8"));
        qInfo().noquote() << "flatpak history, last 8:\n" + history;
        const QStringList lines = history.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        bool anyUpdate = false;
        for (const QString &line : lines) {
            if (!line.contains(QStringLiteral("update")))
                continue;
            anyUpdate = true;
            const QStringList words = line.simplified().split(QLatin1Char(' '));
            QVERIFY2(words.size() >= 7 && words.constLast().size() == 64,
                     qPrintable(QStringLiteral("an update without both full commits: ") + line));
        }
        if (!anyUpdate)
            QSKIP("No Flathub update in the last 60 days to read.");
    }
};

QTEST_GUILESS_MAIN(LivePutBack)
#include "live_putback.moc"
