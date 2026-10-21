// SPDX-License-Identifier: GPL-2.0-or-later
#include "app/SettingRows.h"

#include <QFile>
#include <QLocale>
#include <QMap>
#include <QRegularExpression>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>

namespace splitrock {

namespace {

const QString Outputs = QStringLiteral("kwinoutputconfig.json");
const QString Applets = QStringLiteral("plasma-org.kde.plasma.desktop-appletsrc");
const QString Shortcuts = QStringLiteral("kglobalshortcutsrc");

// Files whose old values, written again with KDE's own tool and the desktop
// told, take effect at once. The rest open their settings page.
const QStringList Written{QStringLiteral("kwinrc"),        QStringLiteral("kwinrulesrc"),   QStringLiteral("kxkbrc"),
                          QStringLiteral("powerdevilrc"),  QStringLiteral("kscreenlockerrc"), QStringLiteral("plasmanotifyrc"),
                          QStringLiteral("mimeapps.list")};

QString capitalised(QString text)
{
    if (!text.isEmpty())
        text[0] = text.at(0).toUpper();
    return text;
}

// "blurEnabled" as "blur enabled", "TurnOffDisplayIdleTimeoutSec" as "turn
// off display idle timeout sec".
QString keyWords(const QString &key)
{
    QString words;
    for (qsizetype i = 0; i < key.size(); ++i) {
        const QChar c = key.at(i);
        if (c == QLatin1Char('_') || c == QLatin1Char('-')) {
            words += QLatin1Char(' ');
            continue;
        }
        if (c.isUpper() && i > 0 && key.at(i - 1).isLower())
            words += QLatin1Char(' ');
        words += c.toLower();
    }
    return words.simplified();
}

bool builtIn(const QString &connector)
{
    return connector.startsWith(QLatin1String("eDP")) || connector.startsWith(QLatin1String("LVDS"))
        || connector.startsWith(QLatin1String("DSI"));
}

// KWin keeps every screen it has known; a second one on the same connector
// is named "DP-1 (its EDID)" (settings/SettingsHistory.h).
QString connectorOf(const QString &group)
{
    return group.section(QStringLiteral(" ("), 0, 0);
}

QString screenName(const QString &group)
{
    const QString connector = connectorOf(group);
    return builtIn(connector) ? QStringLiteral("Built-in screen") : QStringLiteral("Screen on %1").arg(connector);
}

QString percent(const QString &scale)
{
    bool ok = false;
    const double value = scale.toDouble(&ok);
    return ok ? QStringLiteral("%1%").arg(qRound(value * 100)) : scale;
}

// "2560x1600@180000" as "2560 × 1600 at 180 Hz".
QString modeWords(const QString &mode)
{
    static const QRegularExpression shape(QStringLiteral(R"(^(\d+)x(\d+)@(\d+)$)"));
    const QRegularExpressionMatch m = shape.match(mode);
    if (!m.hasMatch())
        return mode;
    return QStringLiteral("%1 × %2 at %3 Hz").arg(m.captured(1), m.captured(2)).arg(qRound(m.captured(3).toDouble() / 1000));
}

QString turnWords(const QString &transform)
{
    static const QMap<QString, QString> words{{QStringLiteral("Normal"), QStringLiteral("upright")},
                                              {QStringLiteral("Rotated90"), QStringLiteral("turned left")},
                                              {QStringLiteral("Rotated180"), QStringLiteral("upside down")},
                                              {QStringLiteral("Rotated270"), QStringLiteral("turned right")}};
    return words.value(transform, transform);
}

// A shortcut's field, "Meta+T\tMeta+Shift+T,Meta+T,Tile windows": the keys
// now, the default and the action's name.
struct Shortcut {
    QString keys;
    QString name;
};
Shortcut shortcut(const QString &group, const QString &key, const QString &value)
{
    if (group.startsWith(QLatin1String("services][")))
        return {value == QLatin1String("none") ? QString() : value, SettingRows::appName(group.mid(10))};
    // The name may hold commas; the keys and the default never do, except
    // as the key itself, which KDE writes as "\\,".
    const QStringList parts = value.split(QLatin1Char(','));
    QString keys = parts.value(0);
    if (keys == QLatin1String("none"))
        keys.clear();
    keys.replace(QLatin1Char('\t'), QStringLiteral(" or "));
    const QString name = parts.size() >= 3 ? parts.mid(2).join(QLatin1Char(',')) : key;
    return {keys, name.isEmpty() ? key : name};
}

QString pluginName(const QString &plugin)
{
    static const QMap<QString, QString> names{
        {QStringLiteral("digitalclock"), QStringLiteral("Digital clock")},
        {QStringLiteral("kickoff"), QStringLiteral("Application launcher")},
        {QStringLiteral("kicker"), QStringLiteral("Application menu")},
        {QStringLiteral("icontasks"), QStringLiteral("Task manager")},
        {QStringLiteral("taskmanager"), QStringLiteral("Task manager")},
        {QStringLiteral("systemtray"), QStringLiteral("System tray")},
        {QStringLiteral("showdesktop"), QStringLiteral("Show desktop")},
        {QStringLiteral("panelspacer"), QStringLiteral("Spacer")},
        {QStringLiteral("marginsseparator"), QStringLiteral("Spacer")},
        {QStringLiteral("notes"), QStringLiteral("Sticky note")},
        {QStringLiteral("volume"), QStringLiteral("Audio volume")},
        {QStringLiteral("networkmanagement"), QStringLiteral("Networks")},
        {QStringLiteral("devicenotifier"), QStringLiteral("Disks and devices")},
        {QStringLiteral("panel"), QStringLiteral("panel")},
        {QStringLiteral("folder"), QStringLiteral("Folder view")},
    };
    const QString last = plugin.section(QLatin1Char('.'), -1);
    return names.value(last, capitalised(keyWords(last)));
}

QString fileName(QString path)
{
    if (path.startsWith(QLatin1String("file://")))
        path = path.mid(7);
    return path.section(QLatin1Char('/'), -1);
}

// What a MIME type opens, in words: "web links", "PDFs".
QString openedWords(const QString &type)
{
    static const QMap<QString, QString> words{
        {QStringLiteral("x-scheme-handler/http"), QStringLiteral("web links")},
        {QStringLiteral("x-scheme-handler/https"), QStringLiteral("web links")},
        {QStringLiteral("text/html"), QStringLiteral("web pages")},
        {QStringLiteral("x-scheme-handler/mailto"), QStringLiteral("email links")},
        {QStringLiteral("inode/directory"), QStringLiteral("folders")},
        {QStringLiteral("application/pdf"), QStringLiteral("PDFs")},
        {QStringLiteral("text/plain"), QStringLiteral("text files")},
    };
    if (words.contains(type))
        return words.value(type);
    if (type.startsWith(QLatin1String("image/")))
        return QStringLiteral("pictures (%1)").arg(type.mid(6));
    if (type.startsWith(QLatin1String("video/")))
        return QStringLiteral("videos (%1)").arg(type.mid(6));
    if (type.startsWith(QLatin1String("audio/")))
        return QStringLiteral("music (%1)").arg(type.mid(6));
    return type;
}

QString appsWords(const QString &list)
{
    QStringList names;
    for (const QString &id : list.split(QLatin1Char(';'), Qt::SkipEmptyParts))
        names.append(SettingRows::appName(id));
    return names.isEmpty() ? QStringLiteral("none") : names.join(QStringLiteral(", "));
}

// Seconds as "5 minutes", "1 hour", "never" for 0 or less.
QString duration(const QString &seconds)
{
    bool ok = false;
    const int s = seconds.toInt(&ok);
    if (!ok)
        return seconds;
    if (s <= 0)
        return QStringLiteral("never");
    if (s % 3600 == 0)
        return s == 3600 ? QStringLiteral("1 hour") : QStringLiteral("%1 hours").arg(s / 3600);
    if (s % 60 == 0)
        return s == 60 ? QStringLiteral("1 minute") : QStringLiteral("%1 minutes").arg(s / 60);
    return QStringLiteral("%1 seconds").arg(s);
}

QString onOff(const QString &value)
{
    return value == QLatin1String("true") ? QStringLiteral("on") : value == QLatin1String("false") ? QStringLiteral("off") : value;
}

QString valueOr(const QString &value, bool set)
{
    return !set ? QStringLiteral("not set") : value.isEmpty() ? QStringLiteral("empty") : value;
}

} // namespace

QString SettingRows::quoted(const QString &word)
{
    QString inner = word;
    inner.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + inner + QLatin1Char('\'');
}

QString SettingRows::appName(const QString &desktopId)
{
    QString id = desktopId;
    if (!id.endsWith(QLatin1String(".desktop")))
        id += QStringLiteral(".desktop");
    const QString path = QStandardPaths::locate(QStandardPaths::ApplicationsLocation, id);
    QFile file(path);
    if (!path.isEmpty() && file.open(QIODevice::ReadOnly)) {
        for (const QByteArray &line : file.readAll().split('\n')) {
            if (line.startsWith("Name="))
                return QString::fromUtf8(line.mid(5)).trimmed();
        }
    }
    QString name = id.chopped(8).section(QLatin1Char('.'), -1);
    return capitalised(name);
}

QString SettingRows::timeWords(const QDateTime &when)
{
    return QLocale::c().toString(when.toLocalTime().time(), QStringLiteral("h:mm ap")).toLower();
}

QString SettingRows::area(const QString &file)
{
    static const QMap<QString, QString> areas{
        {QStringLiteral("kdeglobals"), QStringLiteral("Look, colours and fonts")},
        {QStringLiteral("kwinrc"), QStringLiteral("Windows and effects")},
        {QStringLiteral("kwinrulesrc"), QStringLiteral("Window rules")},
        {Applets, QStringLiteral("Panels and desktop widgets")},
        {QStringLiteral("plasmarc"), QStringLiteral("Plasma style")},
        {QStringLiteral("kcminputrc"), QStringLiteral("Mouse, touchpad and pointer")},
        {QStringLiteral("touchpadxlibinputrc"), QStringLiteral("Mouse, touchpad and pointer")},
        {QStringLiteral("kxkbrc"), QStringLiteral("Keyboard layout")},
        {Shortcuts, QStringLiteral("Keyboard shortcuts")},
        {QStringLiteral("powerdevilrc"), QStringLiteral("Power and sleep")},
        {QStringLiteral("powermanagementprofilesrc"), QStringLiteral("Power and sleep")},
        {QStringLiteral("kscreenlockerrc"), QStringLiteral("Screen lock")},
        {QStringLiteral("bluedevilglobalrc"), QStringLiteral("Bluetooth")},
        {QStringLiteral("plasmanotifyrc"), QStringLiteral("Notifications")},
        {QStringLiteral("mimeapps.list"), QStringLiteral("Default apps")},
        {QStringLiteral("kcmfonts"), QStringLiteral("Look, colours and fonts")},
        {QStringLiteral("baloofilerc"), QStringLiteral("File search")},
        {QStringLiteral("kaccessrc"), QStringLiteral("Accessibility")},
        {QStringLiteral("ktimezonedrc"), QStringLiteral("Date and time")},
        {QStringLiteral("plasma-localerc"), QStringLiteral("Language and region")},
        {Outputs, QStringLiteral("Displays")},
    };
    return areas.value(file, file);
}

SettingRows::Words SettingRows::words(const SettingsChange &c)
{
    const QString was = valueOr(c.before, c.wasSet);
    const QString now = valueOr(c.after, c.isSet);
    const QString generic = QStringLiteral("%1: %2").arg(area(c.file), keyWords(c.key));
    const QString genericWasNow = QStringLiteral("Was %1, now %2").arg(was, now);

    if (c.file == Outputs) {
        const QString screen = screenName(c.group);
        if (c.key == QLatin1String("scale"))
            return {QStringLiteral("%1: scale %2 to %3").arg(screen, percent(c.before), percent(c.after)), {}};
        if (c.key == QLatin1String("mode"))
            return {QStringLiteral("%1: resolution changed").arg(screen),
                    QStringLiteral("Was %1, now %2").arg(modeWords(c.before), modeWords(c.after))};
        if (c.key == QLatin1String("transform"))
            return {QStringLiteral("%1: %2 to %3").arg(screen, turnWords(c.before), turnWords(c.after)), {}};
        if (c.key.startsWith(QLatin1String("enabled in ")))
            return {QStringLiteral("%1 turned %2").arg(screen, onOff(c.after)), QStringLiteral("With %1").arg(c.key.mid(11))};
        if (c.key.startsWith(QLatin1String("position in ")))
            return {QStringLiteral("%1 moved").arg(screen), QStringLiteral("Was at %1, now at %2").arg(was, now)};
        if (c.key == QLatin1String("brightness") || c.key == QLatin1String("sdrBrightness"))
            return {{}, {}, false};
        return {QStringLiteral("%1: %2").arg(screen, keyWords(c.key)), genericWasNow};
    }

    if (c.file == Shortcuts) {
        const Shortcut before = shortcut(c.group, c.key, c.before);
        const Shortcut after = shortcut(c.group, c.key, c.after);
        // Only the name or the default written again: nobody changed it.
        if (before.keys == after.keys)
            return {{}, {}, false};
        const QString name = after.name.isEmpty() ? before.name : after.name;
        if (before.keys.isEmpty())
            return {QStringLiteral("Shortcut added for %1").arg(name), QStringLiteral("Now %1").arg(after.keys)};
        if (after.keys.isEmpty())
            return {QStringLiteral("Shortcut for %1 removed").arg(name), QStringLiteral("Was %1").arg(before.keys)};
        return {QStringLiteral("Shortcut for %1 changed").arg(name),
                QStringLiteral("Was %1, now %2").arg(before.keys, after.keys)};
    }

    if (c.file == Applets) {
        if (c.key == QLatin1String("Image"))
            return {QStringLiteral("Wallpaper changed"),
                    QStringLiteral("Was %1, now %2").arg(c.wasSet ? fileName(c.before) : QStringLiteral("the default"),
                                                          c.isSet ? fileName(c.after) : QStringLiteral("the default"))};
        const bool panel = !c.group.contains(QLatin1String("Applets"));
        const QString what = pluginName(c.isSet ? c.after : c.before);
        if (panel) {
            if (c.after == QLatin1String("org.kde.panel") || c.before == QLatin1String("org.kde.panel"))
                return {c.isSet ? QStringLiteral("A panel was added") : QStringLiteral("A panel was removed"), {}};
            return {{}, {}, false};
        }
        if (!c.wasSet)
            return {QStringLiteral("Added the %1 widget").arg(what), {}};
        if (!c.isSet)
            return {QStringLiteral("Removed the %1 widget").arg(what), {}};
        return {QStringLiteral("A widget changed to %1").arg(what), {}};
    }

    if (c.file == QLatin1String("kwinrc")) {
        if (c.group == QLatin1String("Plugins") && c.key.endsWith(QLatin1String("Enabled")))
            return {QStringLiteral("%1 effect turned %2").arg(capitalised(keyWords(c.key.chopped(7))), onOff(c.after)), {}};
        if (c.group == QLatin1String("NightColor") && c.key == QLatin1String("Active"))
            return {QStringLiteral("Night Light turned %1").arg(onOff(c.after)), {}};
        if (c.group == QLatin1String("NightColor"))
            return {QStringLiteral("Night Light: %1").arg(keyWords(c.key)), genericWasNow};
    }

    if (c.file == QLatin1String("kdeglobals")) {
        if (c.key == QLatin1String("ColorScheme"))
            return {QStringLiteral("Colours: %1 to %2").arg(was, now), {}};
        if (c.key == QLatin1String("LookAndFeelPackage"))
            return {QStringLiteral("Global theme: %1 to %2").arg(was, now), {}};
        if (c.group == QLatin1String("Icons") && c.key == QLatin1String("Theme"))
            return {QStringLiteral("Icons: %1 to %2").arg(was, now), {}};
        if (c.key == QLatin1String("AccentColor"))
            return {QStringLiteral("Accent colour changed"), genericWasNow};
        if (c.key == QLatin1String("font") || c.key == QLatin1String("fixed") || c.key.endsWith(QLatin1String("Font")))
            return {QStringLiteral("Font changed"), QStringLiteral("Was %1, now %2").arg(c.before.section(QLatin1Char(','), 0, 1),
                                                                                        c.after.section(QLatin1Char(','), 0, 1))};
    }

    if (c.file == QLatin1String("plasmarc") && c.group == QLatin1String("Theme") && c.key == QLatin1String("name"))
        return {QStringLiteral("Plasma style: %1 to %2").arg(was, now), {}};

    if (c.file == QLatin1String("kcminputrc") && c.group == QLatin1String("Mouse")) {
        if (c.key == QLatin1String("cursorTheme"))
            return {QStringLiteral("Pointer: %1 to %2").arg(was, now), {}};
        if (c.key == QLatin1String("cursorSize"))
            return {QStringLiteral("Pointer size: %1 to %2").arg(was, now), {}};
    }

    if (c.file == QLatin1String("kxkbrc") && c.group == QLatin1String("Layout") && c.key == QLatin1String("LayoutList"))
        return {QStringLiteral("Keyboard layouts changed"), genericWasNow};

    if (c.file == QLatin1String("powerdevilrc")) {
        // "AC][Display", "Battery][SuspendAndShutdown", "LowBattery][...".
        const QString when = c.group.startsWith(QLatin1String("AC")) ? QStringLiteral("Plugged in")
                           : c.group.startsWith(QLatin1String("LowBattery")) ? QStringLiteral("On low battery")
                           : c.group.startsWith(QLatin1String("Battery")) ? QStringLiteral("On battery")
                           : area(c.file);
        if (c.key.endsWith(QLatin1String("TimeoutSec"))) {
            const QString what = c.key.startsWith(QLatin1String("TurnOffDisplay")) ? QStringLiteral("screen turns off")
                               : c.key.startsWith(QLatin1String("DimDisplay")) ? QStringLiteral("screen dims")
                               : c.key.startsWith(QLatin1String("AutoSuspend")) ? QStringLiteral("computer sleeps")
                               : keyWords(c.key.chopped(10));
            return {QStringLiteral("%1, the %2 after %3").arg(when, what, duration(c.after)),
                    QStringLiteral("Was after %1").arg(duration(c.before))};
        }
        return {QStringLiteral("%1: %2").arg(when, keyWords(c.key)), genericWasNow};
    }

    if (c.file == QLatin1String("kscreenlockerrc") && c.group == QLatin1String("Daemon")) {
        if (c.key == QLatin1String("Autolock"))
            return {QStringLiteral("Locking by itself turned %1").arg(onOff(c.after)), {}};
        if (c.key == QLatin1String("Timeout")) {
            // 0, or none, is never by itself.
            const auto minutes = [](const QString &value, bool set) {
                const int n = set ? value.toInt() : 0;
                return n <= 0 ? QString() : n == 1 ? QStringLiteral("1 minute") : QStringLiteral("%1 minutes").arg(n);
            };
            const QString after = minutes(c.after, c.isSet);
            const QString before = minutes(c.before, c.wasSet);
            return {after.isEmpty() ? QStringLiteral("Screen no longer locks by itself") : QStringLiteral("Screen locks after %1").arg(after),
                    before.isEmpty() ? QStringLiteral("Was: never") : QStringLiteral("Was after %1").arg(before)};
        }
        if (c.key == QLatin1String("LockOnResume"))
            return {QStringLiteral("Locking after sleep turned %1").arg(onOff(c.after)), {}};
    }

    if (c.file == QLatin1String("plasmanotifyrc")) {
        if (c.key == QLatin1String("Until") || c.key == QLatin1String("Seen"))
            return {{}, {}, false};
    }

    if (c.file == QLatin1String("mimeapps.list")) {
        if (c.group != QLatin1String("Default Applications"))
            return {{}, {}, false};
        return {QStringLiteral("Default app for %1 changed").arg(openedWords(c.key)),
                QStringLiteral("Was %1, now %2").arg(c.wasSet ? appsWords(c.before) : QStringLiteral("not set"),
                                                     c.isSet ? appsWords(c.after) : QStringLiteral("not set"))};
    }

    return {generic, genericWasNow};
}

QString SettingRows::page(const SettingsChange &c)
{
    if (c.file == QLatin1String("kdeglobals")) {
        if (c.key == QLatin1String("ColorScheme") || c.key == QLatin1String("AccentColor"))
            return QStringLiteral("kcm_colors");
        if (c.group == QLatin1String("Icons"))
            return QStringLiteral("kcm_icons");
        if (c.key == QLatin1String("font") || c.key == QLatin1String("fixed") || c.key.endsWith(QLatin1String("Font")))
            return QStringLiteral("kcm_fonts");
        return QStringLiteral("kcm_lookandfeel");
    }
    if (c.file == QLatin1String("kcminputrc")) {
        if (c.group == QLatin1String("Mouse") && c.key.startsWith(QLatin1String("cursor")))
            return QStringLiteral("kcm_cursortheme");
        if (c.group.startsWith(QLatin1String("Keyboard")))
            return QStringLiteral("kcm_keyboard");
        return c.group.contains(QLatin1String("Touchpad"), Qt::CaseInsensitive) ? QStringLiteral("kcm_touchpad")
                                                                                 : QStringLiteral("kcm_mouse");
    }
    if (c.file == QLatin1String("kwinrc")) {
        if (c.group == QLatin1String("Plugins"))
            return QStringLiteral("kcm_kwin_effects");
        if (c.group == QLatin1String("NightColor"))
            return QStringLiteral("kcm_nightlight");
        return QStringLiteral("kcm_kwinoptions");
    }
    static const QMap<QString, QString> pages{
        {QStringLiteral("kwinrulesrc"), QStringLiteral("kcm_kwinrules")},
        {Applets, QStringLiteral("kcm_wallpaper")},
        {QStringLiteral("plasmarc"), QStringLiteral("kcm_desktoptheme")},
        {QStringLiteral("touchpadxlibinputrc"), QStringLiteral("kcm_touchpad")},
        {QStringLiteral("kxkbrc"), QStringLiteral("kcm_keyboard")},
        {Shortcuts, QStringLiteral("kcm_keys")},
        {QStringLiteral("powerdevilrc"), QStringLiteral("kcm_powerdevilprofilesconfig")},
        {QStringLiteral("powermanagementprofilesrc"), QStringLiteral("kcm_powerdevilprofilesconfig")},
        {QStringLiteral("kscreenlockerrc"), QStringLiteral("kcm_screenlocker")},
        {QStringLiteral("bluedevilglobalrc"), QStringLiteral("kcm_bluetooth")},
        {QStringLiteral("plasmanotifyrc"), QStringLiteral("kcm_notifications")},
        {QStringLiteral("mimeapps.list"), QStringLiteral("kcm_componentchooser")},
        {QStringLiteral("kcmfonts"), QStringLiteral("kcm_fonts")},
        {QStringLiteral("baloofilerc"), QStringLiteral("kcm_baloofile")},
        {QStringLiteral("kaccessrc"), QStringLiteral("kcm_access")},
        {QStringLiteral("ktimezonedrc"), QStringLiteral("kcm_clock")},
        {QStringLiteral("plasma-localerc"), QStringLiteral("kcm_regionandlang")},
        {Outputs, QStringLiteral("kcm_kscreen")},
    };
    return pages.value(c.file);
}

QString SettingRows::reload(const QString &file)
{
    // kwinrc is read again by the change runner itself (app/ChangeRunner.h).
    if (file == QLatin1String("kwinrulesrc"))
        return QStringLiteral("{ qdbus6 org.kde.KWin /KWin reconfigure || qdbus org.kde.KWin /KWin reconfigure; } >/dev/null 2>&1 || true");
    if (file == QLatin1String("kxkbrc"))
        return QStringLiteral("dbus-send --session --type=signal /Layouts org.kde.keyboard.reloadConfig || true");
    if (file == QLatin1String("powerdevilrc"))
        return QStringLiteral("{ qdbus6 org.kde.Solid.PowerManagement /org/kde/Solid/PowerManagement "
                              "org.kde.Solid.PowerManagement.reparseConfiguration; qdbus6 org.kde.Solid.PowerManagement "
                              "/org/kde/Solid/PowerManagement org.kde.Solid.PowerManagement.refreshStatus; } >/dev/null 2>&1 || true");
    if (file == QLatin1String("kscreenlockerrc"))
        return QStringLiteral("qdbus6 org.freedesktop.ScreenSaver /ScreenSaver configure >/dev/null 2>&1 || true");
    return {};
}

QString SettingRows::command(const SettingsChange &c, bool before, const QString &program)
{
    const bool set = before ? c.wasSet : c.isSet;
    const QString value = before ? c.before : c.after;

    if (c.file == Outputs) {
        // kscreen-doctor sets one output's setting on the running desktop,
        // and KWin keeps it: on the screen in that connector now.
        const QString connector = connectorOf(c.group);
        if (connector.isEmpty() || connector.contains(QLatin1Char(' ')) || !set || value.isEmpty())
            return {};
        const QString output = QStringLiteral("output.%1.").arg(connector);
        if (c.key == QLatin1String("scale"))
            return QStringLiteral("kscreen-doctor %1").arg(quoted(output + QStringLiteral("scale.") + value));
        if (c.key == QLatin1String("mode")) {
            static const QRegularExpression shape(QStringLiteral(R"(^(\d+)x(\d+)@(\d+)$)"));
            const QRegularExpressionMatch m = shape.match(value);
            if (!m.hasMatch())
                return {};
            return QStringLiteral("kscreen-doctor %1").arg(quoted(output + QStringLiteral("mode.%1x%2@%3").arg(m.captured(1), m.captured(2)).arg(qRound(m.captured(3).toDouble() / 1000))));
        }
        if (c.key == QLatin1String("transform")) {
            static const QMap<QString, QString> rotation{{QStringLiteral("Normal"), QStringLiteral("normal")},
                                                         {QStringLiteral("Rotated90"), QStringLiteral("left")},
                                                         {QStringLiteral("Rotated180"), QStringLiteral("inverted")},
                                                         {QStringLiteral("Rotated270"), QStringLiteral("right")}};
            if (!rotation.contains(value))
                return {};
            return QStringLiteral("kscreen-doctor %1").arg(quoted(output + QStringLiteral("rotation.") + rotation.value(value)));
        }
        return {};
    }

    if (c.file == Shortcuts) {
        // KDE's shortcuts live in the running desktop, which writes the file
        // itself; Split Rock asks it on the bus (src/main.cpp, --set-shortcut).
        if (program.isEmpty())
            return {};
        const Shortcut s = shortcut(c.group, c.key, set ? value : QString());
        QString keys = set ? value.section(QLatin1Char(','), 0, 0) : QString();
        if (c.group.startsWith(QLatin1String("services][")))
            keys = set ? value : QString();
        if (keys == QLatin1String("none"))
            keys.clear();
        const QString component = c.group.startsWith(QLatin1String("services][")) ? c.group.mid(10) : c.group;
        return QStringLiteral("%1 --set-shortcut %2 %3 %4 %5")
            .arg(quoted(program), quoted(component), quoted(c.key), quoted(keys), quoted(s.name));
    }

    if (c.file == Applets) {
        if (c.key != QLatin1String("Image") || !set || value.isEmpty())
            return {};
        QString path = value;
        if (path.startsWith(QLatin1String("file://")))
            path = path.mid(7);
        return QStringLiteral("plasma-apply-wallpaperimage %1").arg(quoted(path));
    }

    if (!Written.contains(c.file))
        return {};
    // KDE writes some values escaped; those are left to the settings page.
    static const QRegularExpression localised(QStringLiteral(R"(\[[^\]]*\]$)"));
    if (c.group.contains(QLatin1Char('\\')) || c.key.contains(QLatin1Char('\\')) || value.contains(QLatin1Char('\\'))
        || localised.match(c.key).hasMatch())
        return {};
    QStringList where{quoted(c.file)};
    for (const QString &part : c.group.split(QStringLiteral("][")))
        where << QStringLiteral("--group") << quoted(part);
    where << QStringLiteral("--key") << quoted(c.key);
    const QString at = where.join(QLatin1Char(' '));
    // The value after --, so one such as -1 isn't read as an option; then
    // read back, since kwriteconfig6 ends well even when it wrote nothing.
    if (set)
        return QStringLiteral("kwriteconfig6 --file %1 --notify -- %2 && [ \"$(kreadconfig6 --file %1)\" = %2 ]").arg(at, quoted(value));
    // Gone, it may read as the system's own default, so it isn't read back.
    return QStringLiteral("kwriteconfig6 --file %1 --notify --delete").arg(at);
}

QList<SettingRow> SettingRows::rows(const QList<SettingsChange> &changes, const QString &program, const Current &current)
{
    QList<SettingsChange> sorted;
    for (const SettingsChange &change : changes) {
        // The same change seen twice, by Core and by a look of its own.
        const bool seen = std::any_of(sorted.cbegin(), sorted.cend(), [&change](const SettingsChange &other) {
            return other.file == change.file && other.group == change.group && other.key == change.key
                && other.before == change.before && other.after == change.after
                && qAbs(other.when.secsTo(change.when)) <= 10;
        });
        if (!seen && words(change).meaningful)
            sorted.append(change);
    }
    std::stable_sort(sorted.begin(), sorted.end(), [](const SettingsChange &a, const SettingsChange &b) { return a.when < b.when; });

    // A screen's changes are one place each; any other file is one.
    const auto place = [](const SettingsChange &c) { return c.file == Outputs ? c.file + QLatin1Char('/') + c.group : c.file; };
    QList<QList<SettingsChange>> groups;
    for (const SettingsChange &change : std::as_const(sorted)) {
        bool joined = false;
        for (qsizetype i = groups.size() - 1; i >= 0; --i) {
            QList<SettingsChange> &group = groups[i];
            if (place(group.constLast()) == place(change) && group.constLast().when.secsTo(change.when) <= 120) {
                // The same setting changed again: one change, from the first
                // value to the last.
                auto again = std::find_if(group.begin(), group.end(), [&change](const SettingsChange &c) {
                    return c.group == change.group && c.key == change.key;
                });
                if (again != group.end()) {
                    again->after = change.after;
                    again->isSet = change.isSet;
                    again->when = change.when;
                } else {
                    group.append(change);
                }
                joined = true;
                break;
            }
        }
        if (!joined)
            groups.append({change});
    }

    QList<SettingRow> rows;
    for (QList<SettingsChange> group : std::as_const(groups)) {
        // Changed and changed back within the two minutes.
        group.erase(std::remove_if(group.begin(), group.end(), [](const SettingsChange &c) {
                        return c.wasSet == c.isSet && c.before == c.after;
                    }),
                    group.end());
        if (group.isEmpty())
            continue;
        const SettingsChange &first = group.constFirst();
        SettingRow row;
        row.changes = group;
        row.when = group.constLast().when;
        row.area = area(first.file);
        row.id = QStringLiteral("setting-%1-%2").arg(first.file, QString::number(first.when.toSecsSinceEpoch()));
        if (first.file == Outputs)
            row.id += QLatin1Char('-') + first.group;
        row.page = page(first);
        const Words said = words(first);
        const QString at = timeWords(row.when);
        if (group.size() == 1) {
            row.title = said.title;
            row.detail = (said.wasNow.isEmpty() ? QStringList{row.area, at} : QStringList{said.wasNow, row.area, at})
                             .join(QStringLiteral(" · "));
        } else {
            row.title = QStringLiteral("%1 and %2 more").arg(said.title).arg(group.size() - 1);
            row.detail = QStringList{row.area, at}.join(QStringLiteral(" · "));
        }

        QStringList forward;
        QStringList back;
        QStringList reloads;
        QStringList named;
        bool every = true;
        bool alreadyBack = static_cast<bool>(current);
        for (const SettingsChange &c : std::as_const(group)) {
            const Words w = words(c);
            named.append(w.wasNow.isEmpty() ? w.title : QStringLiteral("%1 (%2)").arg(w.title, w.wasNow));
            row.exact.append(QStringLiteral("%1 [%2] %3: %4 to %5")
                                 .arg(c.file, c.group, c.key, valueOr(c.before, c.wasSet), valueOr(c.after, c.isSet)));
            const QString to = command(c, true, program);
            const QString from = command(c, false, program);
            if (to.isEmpty() || from.isEmpty())
                every = false;
            back.append(to);
            forward.append(from);
            const QString reloading = reload(c.file);
            if (!reloading.isEmpty() && !reloads.contains(reloading))
                reloads.append(reloading);
            if (current) {
                bool set = false;
                const QString now = current(c, &set);
                if (set != c.wasSet || (set && now != c.before))
                    alreadyBack = false;
            }
        }
        row.alreadyBack = alreadyBack;
        row.question = QStringLiteral("This setting changed on my computer at %1 on %2: %3. %4")
                           .arg(at, QLocale::c().toString(row.when.toLocalTime().date(), QStringLiteral("d MMMM")),
                                named.join(QStringLiteral("; ")),
                                QStringLiteral("How do I put it back the way it was?"));
        if (every) {
            row.way = QStringLiteral("putback");
            const QString tail = reloads.isEmpty() ? QString() : QStringLiteral(" ; ") + reloads.join(QStringLiteral(" ; "));
            row.putBack.title = QStringLiteral("Put back: %1").arg(said.title);
            row.putBack.what = named.join(QStringLiteral("; "));
            row.putBack.why = QStringLiteral("You asked for it back as it was before %1.").arg(at);
            row.putBack.command = back.join(QStringLiteral(" && ")) + tail;
            row.putBack.undo = forward.join(QStringLiteral(" && ")) + tail;
            if (first.file != Outputs && first.file != Shortcuts && first.file != Applets) {
                const QString path = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                    + QLatin1Char('/') + first.file;
                row.putBack.files = {path};
            }
        } else {
            row.way = row.page.isEmpty() || (first.file == Applets && first.key != QLatin1String("Image")) ? QStringLiteral("help")
                                                                                                         : QStringLiteral("settings");
        }
        rows.append(row);
    }
    std::stable_sort(rows.begin(), rows.end(), [](const SettingRow &a, const SettingRow &b) { return a.when > b.when; });
    return rows;
}

} // namespace splitrock
