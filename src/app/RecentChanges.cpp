// SPDX-License-Identifier: GPL-2.0-or-later
#include "app/RecentChanges.h"

#include "app/Product.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QHash>
#include <QProcess>
#include <QStandardPaths>
#include <QLocale>
#include <QMap>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace splitrock {

namespace {

// Run as the person: nothing here changes anything. Each section starts with
// a line "@@ name".
const QString LookScript = QStringLiteral(R"SH(export LANG=C LC_ALL=C
since=$(date -d "-14 days" +%Y-%m-%d 2>/dev/null)
echo "@@ pacman"
[ -r /var/log/pacman.log ] && grep -a '\[ALPM\] \(installed\|upgraded\|removed\|downgraded\|reinstalled\) ' /var/log/pacman.log \
    | awk -v s="[$since" '$1 >= s' | tail -n 4000
# The app a package brings, by the name its launcher shows, as
# "package|launcher|Name": for those installed, from what pacman lists; for
# those removed, from the copy in pacman's cache.
echo "@@ apps"
launcher() {
    awk -F= '/^\[/ { entry = ($0 == "[Desktop Entry]") }
        entry && $1 == "Name" && name == "" { name = substr($0, 6) }
        entry && ($1 == "NoDisplay" || $1 == "Hidden") && $2 == "true" { hidden = 1 }
        entry && $1 == "Type" && $2 != "Application" { hidden = 1 }
        END { if (name != "" && !hidden) print name }'
}
if [ -r /var/log/pacman.log ]; then
    added=$(grep -a '\[ALPM\] installed ' /var/log/pacman.log | awk -v s="[$since" '$1 >= s { print $4 }' | sort -u | head -n 400)
    [ -n "$added" ] && pacman -Ql $added 2>/dev/null | while read -r pkg path; do
        case "$path" in /usr/share/applications/*.desktop) ;; *) continue ;; esac
        name=$(launcher < "$path")
        [ -n "$name" ] && echo "$pkg|${path##*/}|$name"
    done
    command -v bsdtar >/dev/null 2>&1 && grep -a '\[ALPM\] removed ' /var/log/pacman.log \
        | awk -v s="[$since" '$1 >= s { v = $5; gsub(/[()]/, "", v); print $4, v }' | sort -u | head -n 40 \
        | while read -r pkg version; do
            for file in /var/cache/pacman/pkg/"$pkg-$version"-*.pkg.tar.*; do
                case "$file" in *.sig) continue ;; esac
                [ -r "$file" ] || continue
                bsdtar -tf "$file" 2>/dev/null | grep '^usr/share/applications/[^/]*\.desktop$' | while read -r path; do
                    name=$(bsdtar -xOf "$file" "$path" 2>/dev/null | launcher)
                    [ -n "$name" ] && echo "$pkg|${path##*/}|$name"
                done
                break
            done
        done
fi
echo "@@ flatpak"
command -v flatpak >/dev/null 2>&1 && flatpak history --since="$since 00:00:00" --columns=time,change,application 2>/dev/null \
    | tail -n 400
# The commit each app came from, to go back to; an older Flatpak without
# these columns leaves this section empty and nothing else.
echo "@@ flatpak-commits"
command -v flatpak >/dev/null 2>&1 && flatpak history --since="$since 00:00:00" \
    --columns=time,change,application,installation,commit,old-commit 2>/dev/null | tail -n 400
echo "@@ settings"
find "${XDG_CONFIG_HOME:-$HOME/.config}" -maxdepth 2 \( -type f -o -type l \) -newermt "$since" -printf '%T@ %P\n' 2>/dev/null \
    | head -n 3000
find "${XDG_DATA_HOME:-$HOME/.local/share}/kscreen" -type f -newermt "$since" -printf '%T@ kscreen/%P\n' 2>/dev/null \
    | head -n 200
echo "@@ failed"
for scope in system user; do
    flag=
    [ "$scope" = user ] && flag=--user
    systemctl $flag --failed --type=service --no-legend --plain 2>/dev/null | awk '{print $1}' | while read -r unit; do
        at=$(systemctl $flag show -p StateChangeTimestamp --value --timestamp=unix "$unit" 2>/dev/null)
        what=$(systemctl $flag show -p Description --value "$unit" 2>/dev/null)
        # What it said last, for whoever helps: one line, its own bars made slashes.
        said=$(journalctl $flag -u "$unit" -n 12 -o cat --no-pager -q 2>/dev/null | tr '|' '/' | tr '\n' '\037')
        echo "$scope|$unit|${at#@}|$said|$what"
    done
done
echo "@@ kernel"
uname -r
# Missing only counts where kernels are installed as packages at all.
if [ -d "/usr/lib/modules/$(uname -r)" ] || [ -z "$(ls -A /usr/lib/modules 2>/dev/null)" ]; then echo present; else echo missing; fi
# The packages still in pacman's cache, where an older version can come back from.
echo "@@ cache"
ls /var/cache/pacman/pkg 2>/dev/null | grep -v '\.sig$' | tail -n 20000
# What went wrong since, for whoever helps.
echo "@@ errors"
journalctl -p 3 --since "$since" -o short-unix --no-pager -q 2>/dev/null | tail -n 300
echo "@@ snapper"
command -v snapper >/dev/null 2>&1 && [ -f /etc/snapper/configs/root ] && echo yes
echo "@@ end"
)SH");

QDateTime logTime(const QString &line)
{
    // pacman's own stamp, "[2026-10-09T21:02:14-0500]", or an older one,
    // "[2026-10-09 21:02]".
    static const QRegularExpression stamp(QStringLiteral(R"(^\[(\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d)([+-]\d\d)(\d\d)\])"));
    if (const QRegularExpressionMatch m = stamp.match(line); m.hasMatch())
        return QDateTime::fromString(m.captured(1) + m.captured(2) + QLatin1Char(':') + m.captured(3), Qt::ISODate).toLocalTime();
    static const QRegularExpression old(QStringLiteral(R"(^\[(\d{4}-\d\d-\d\d \d\d:\d\d)\])"));
    if (const QRegularExpressionMatch m = old.match(line); m.hasMatch())
        return QDateTime::fromString(m.captured(1), QStringLiteral("yyyy-MM-dd HH:mm"));
    return {};
}

// flatpak's "Oct  9 21:02:14", in this year unless that is still to come.
QDateTime flatpakTime(const QString &text, const QDateTime &now)
{
    const QStringList parts = text.simplified().split(QLatin1Char(' '));
    if (parts.size() != 3)
        return {};
    const QLocale c = QLocale::c();
    for (int year : {now.date().year(), now.date().year() - 1}) {
        const QDateTime at = c.toDateTime(QStringLiteral("%1 %2 %3 %4").arg(parts.at(0), parts.at(1)).arg(year).arg(parts.at(2)),
                                          QStringLiteral("MMM d yyyy HH:mm:ss"));
        if (at.isValid() && at <= now.addSecs(60))
            return at;
    }
    return {};
}

// "a, b and c", with at most four named and the rest counted.
QString listed(QStringList names, const QString &others = QStringLiteral("more"))
{
    names.removeDuplicates();
    if (names.size() > 4) {
        const qsizetype rest = names.size() - 3;
        names = names.mid(0, 3);
        names.append(QStringLiteral("%1 %2").arg(rest).arg(others));
    }
    if (names.size() <= 1)
        return names.value(0);
    return names.mid(0, names.size() - 1).join(QStringLiteral(", ")) + QStringLiteral(" and ") + names.last();
}

QString lowerFirst(QString text)
{
    if (!text.isEmpty() && !text.startsWith(QLatin1String("I ")))
        text[0] = text.at(0).toLower();
    return text;
}

// "on Thursday", "today", "yesterday".
QString onDay(const QDate &day, const QDate &today)
{
    const QString words = RecentChanges::dayWords(day, today);
    if (day == today || day == today.addDays(-1))
        return words.toLower();
    return QStringLiteral("on ") + words;
}

// A Flatpak app's id said as its name: "org.mozilla.firefox" as "Firefox".
QString flatpakName(const QString &id)
{
    // The last part names the app, unless it only says what kind of thing it
    // is: "com.spotify.Client" is Spotify.
    static const QStringList generic{QStringLiteral("Client"), QStringLiteral("App"), QStringLiteral("app"),
                                     QStringLiteral("Desktop"), QStringLiteral("desktop"), QStringLiteral("Launcher")};
    QString name = id.section(QLatin1Char('.'), -1);
    if (generic.contains(name) && id.count(QLatin1Char('.')) >= 2)
        name = id.section(QLatin1Char('.'), -2, -2);
    if (!name.isEmpty())
        name[0] = name.at(0).toUpper();
    return name;
}

// Each package's app by the name its launcher shows, from the look's
// "package|launcher|Name" lines. Where a package brings several, the launcher
// named after the package says it best: elisa's org.kde.elisa.desktop.
QHash<QString, QString> appNames(const QStringList &lines)
{
    QHash<QString, QString> names;
    QSet<QString> matched;
    for (const QString &line : lines) {
        const QStringList parts = line.split(QLatin1Char('|'));
        if (parts.size() < 3 || parts.at(0).isEmpty() || parts.at(2).trimmed().isEmpty())
            continue;
        const QString &package = parts.at(0);
        const QString launcher = parts.at(1).section(QLatin1Char('.'), 0, -2).toLower();
        const bool own = launcher == package || launcher.endsWith(QLatin1Char('.') + package);
        if (matched.contains(package) || (names.contains(package) && !own))
            continue;
        names.insert(package, parts.mid(2).join(QLatin1Char('|')).trimmed());
        if (own)
            matched.insert(package);
    }
    return names;
}

// App parts and runtimes a person never installed by name.
bool flatpakPart(const QString &id)
{
    static const QRegularExpression part(QStringLiteral(R"(\.(Platform|Sdk|GL|GL32|Locale|Debug|Sources|KStyle|PlatformTheme|Gtk3theme|openh264|ffmpeg-full|Codecs|VAAPI)(\.|$))"));
    return part.match(id).hasMatch() || id.startsWith(QLatin1String("org.freedesktop.")) || id.startsWith(QLatin1String("org.gtk.Gtk3theme"));
}

// What a service said last that says why, past systemd's own lines about
// starting and stopping it.
QString lastWords(const QStringList &log)
{
    static const QRegularExpression systemd(QStringLiteral(
        R"(^(Started|Stopped|Starting|Stopping|Failed to start|Scheduled restart|Start request repeated)|: (Main process exited|Failed with result|Consumed|Scheduled restart|Deactivated))"));
    for (qsizetype i = log.size() - 1; i >= 0; --i) {
        const QString line = log.at(i).trimmed();
        if (!line.isEmpty() && !systemd.match(line).hasMatch())
            return line.size() > 120 ? line.left(117) + QStringLiteral("...") : line;
    }
    return {};
}

// What went wrong in the two days after a moment, from the journal's
// errors, each said once: "Errors since: none recorded" when there were none.
QString errorsAfter(const QStringList &lines, const QDateTime &from)
{
    static const QRegularExpression line(QStringLiteral(R"(^(\d+)(?:\.\d+)?\s+\S+\s+([^:\[]+)(?:\[\d+\])?:\s*(.*)$)"));
    QStringList said;
    for (const QString &each : lines) {
        const QRegularExpressionMatch m = line.match(each);
        if (!m.hasMatch())
            continue;
        const QDateTime at = QDateTime::fromSecsSinceEpoch(m.captured(1).toLongLong());
        if (at < from || at > from.addDays(2))
            continue;
        const QString words = QStringLiteral("- %1: %2").arg(m.captured(2).trimmed(), m.captured(3).trimmed());
        if (!said.contains(words))
            said.append(words);
        if (said.size() >= 6)
            break;
    }
    if (said.isEmpty())
        return QStringLiteral("Errors since: none recorded.");
    return QStringLiteral("Errors since:\n") + said.join(QLatin1Char('\n'));
}

} // namespace

QVariantMap RecentChanges::Item::toMap(const QDate &today) const
{
    return {{QStringLiteral("id"), id},
            {QStringLiteral("day"), dayWords(day, today)},
            {QStringLiteral("kind"), kind},
            {QStringLiteral("title"), title},
            {QStringLiteral("detail"), detail},
            {QStringLiteral("action"), action},
            {QStringLiteral("actionLabel"), actionLabel},
            {QStringLiteral("change"), change},
            {QStringLiteral("question"), question},
            {QStringLiteral("help"), help.isEmpty() ? question : help},
            {QStringLiteral("page"), page},
            {QStringLiteral("exact"), exact},
            {QStringLiteral("confirmTitle"), confirmTitle},
            {QStringLiteral("confirmSub"), confirmSub},
            {QStringLiteral("confirmLines"), confirmLines},
            {QStringLiteral("password"), password}};
}

RecentChanges::RecentChanges(QObject *parent)
    : Chore(parent)
    , m_lookScript(LookScript)
{
}

bool RecentChanges::busy() const
{
    return stage() == QLatin1String("looking") || stage() == QLatin1String("working");
}

QString RecentChanges::dayWords(const QDate &day, const QDate &today)
{
    if (day == today)
        return QStringLiteral("Today");
    if (day == today.addDays(-1))
        return QStringLiteral("Yesterday");
    const QLocale c = QLocale::c();
    if (day > today.addDays(-7))
        return c.dayName(day.dayOfWeek());
    return c.toString(day, QStringLiteral("d MMMM"));
}

QStringList RecentChanges::noticeable(const QMap<QString, QString> &updated)
{
    struct Part {
        QRegularExpression name;
        QString words;
        bool version;
    };
    static const QList<Part> parts{
        {QRegularExpression(QStringLiteral("^plasma-workspace$")), QStringLiteral("the Plasma desktop"), true},
        {QRegularExpression(QStringLiteral("^linux(-zen|-lts|-hardened|-rt|-cachyos[a-z0-9-]*)?$")), QStringLiteral("the Linux kernel"), true},
        {QRegularExpression(QStringLiteral("^mesa$")), QStringLiteral("the graphics drivers"), false},
        {QRegularExpression(QStringLiteral("^nvidia(-open)?(-dkms)?$|^nvidia-utils$")), QStringLiteral("the NVIDIA driver"), false},
        {QRegularExpression(QStringLiteral("^linux-firmware$")), QStringLiteral("the devices' firmware"), false},
        {QRegularExpression(QStringLiteral("^systemd$")), QStringLiteral("the system's core services"), false},
        {QRegularExpression(QStringLiteral("^pipewire$")), QStringLiteral("sound"), false},
        {QRegularExpression(QStringLiteral("^networkmanager$")), QStringLiteral("the network"), false},
        {QRegularExpression(QStringLiteral("^bluez$")), QStringLiteral("Bluetooth"), false},
        {QRegularExpression(QStringLiteral("^cups$")), QStringLiteral("printing"), false},
        {QRegularExpression(QStringLiteral("^firefox$")), QStringLiteral("Firefox"), false},
        {QRegularExpression(QStringLiteral("^chromium$")), QStringLiteral("Chromium"), false},
        {QRegularExpression(QStringLiteral("^google-chrome$")), QStringLiteral("Chrome"), false},
        {QRegularExpression(QStringLiteral("^thunderbird$")), QStringLiteral("Thunderbird"), false},
        {QRegularExpression(QStringLiteral("^libreoffice-(fresh|still)$")), QStringLiteral("LibreOffice"), false},
        {QRegularExpression(QStringLiteral("^steam$")), QStringLiteral("Steam"), false},
        {QRegularExpression(QStringLiteral("^vlc$")), QStringLiteral("VLC"), false},
    };
    static const QRegularExpression release(QStringLiteral(R"(^(\d+(\.\d+)*))"));
    QStringList said;
    for (const Part &part : parts) {
        for (auto it = updated.cbegin(); it != updated.cend(); ++it) {
            if (!part.name.match(it.key()).hasMatch())
                continue;
            const QString version = release.match(it.value()).captured(1);
            const QString words = part.version && !version.isEmpty() ? QStringLiteral("%1 %2").arg(part.words, version) : part.words;
            if (!said.contains(words))
                said.append(words);
            break;
        }
    }
    return said;
}

QString RecentChanges::settingsName(const QString &file)
{
    static const QMap<QString, QString> names{
        {QStringLiteral("kdeglobals"), QStringLiteral("Look, colours and fonts")},
        {QStringLiteral("kwinrc"), QStringLiteral("Windows and effects")},
        {QStringLiteral("kwinrulesrc"), QStringLiteral("Window rules")},
        {QStringLiteral("plasma-org.kde.plasma.desktop-appletsrc"), QStringLiteral("Panels and desktop widgets")},
        {QStringLiteral("plasmarc"), QStringLiteral("Plasma style")},
        {QStringLiteral("kcminputrc"), QStringLiteral("Mouse, touchpad and pointer")},
        {QStringLiteral("touchpadxlibinputrc"), QStringLiteral("Mouse, touchpad and pointer")},
        {QStringLiteral("kxkbrc"), QStringLiteral("Keyboard layout")},
        {QStringLiteral("kglobalshortcutsrc"), QStringLiteral("Keyboard shortcuts")},
        {QStringLiteral("powerdevilrc"), QStringLiteral("Power and sleep")},
        {QStringLiteral("powermanagementprofilesrc"), QStringLiteral("Power and sleep")},
        {QStringLiteral("kscreenlockerrc"), QStringLiteral("Screen lock")},
        {QStringLiteral("bluedevilglobalrc"), QStringLiteral("Bluetooth")},
        {QStringLiteral("plasmanotifyrc"), QStringLiteral("Notifications")},
        {QStringLiteral("mimeapps.list"), QStringLiteral("Default apps")},
        {QStringLiteral("kcmfonts"), QStringLiteral("Look, colours and fonts")},
        {QStringLiteral("baloofilerc"), QStringLiteral("File search")},
        {QStringLiteral("kwalletrc"), QStringLiteral("Password storage")},
        {QStringLiteral("kaccessrc"), QStringLiteral("Accessibility")},
        {QStringLiteral("ktimezonedrc"), QStringLiteral("Date and time")},
        {QStringLiteral("plasma-localerc"), QStringLiteral("Language and region")},
        {QStringLiteral("kwinoutputconfig.json"), QStringLiteral("Displays")},
    };
    if (file.startsWith(QLatin1String("kscreen/")))
        return QStringLiteral("Displays");
    if (file.startsWith(QLatin1String("autostart/")))
        return QStringLiteral("Apps that start at sign-in");
    if (file.startsWith(QLatin1String("systemd/user/")))
        return QStringLiteral("Background services");
    return names.value(file);
}

RecentChanges::Look RecentChanges::readLook(const QString &output, const QList<ChangeRunner::Record> &records, const QDateTime &now,
                                            const QList<SettingRow> &settings, const QDateTime &keptSince)
{
    const QMap<QString, QStringList> found = sections(output);
    const QDate today = now.toLocalTime().date();
    const QDate since = today.addDays(-Days);
    Look look;
    look.restorePoints = found.value(QStringLiteral("snapper")).contains(QStringLiteral("yes"));
    const QString backAction = look.restorePoints ? QStringLiteral("restore") : QStringLiteral("ask");
    const QString backLabel = look.restorePoints ? QStringLiteral("Go back") : QStringLiteral("Ask about it");

    // Changes made from Do it cards.
    for (const ChangeRunner::Record &record : records) {
        if (!record.made.isValid() || record.made.date() < since || record.status == QLatin1String("making"))
            continue;
        Item item;
        item.id = QStringLiteral("own-") + record.id;
        item.day = record.made.date();
        item.when = record.made;
        item.kind = QStringLiteral("own");
        item.title = record.proposal.title;
        item.change = record.id;
        if (record.status == QLatin1String("undone")) {
            item.detail = QStringLiteral("Put back %1").arg(lowerFirst(dayWords(record.undone.date(), today)));
        } else if (record.status == QLatin1String("failed")) {
            item.detail = QStringLiteral("Tried from a card, and it didn't work");
        } else if (record.status == QLatin1String("undoFailed")) {
            item.detail = QStringLiteral("Made from a card; putting it back didn't work");
        } else {
            item.detail = QStringLiteral("Made from a card you approved, at %1").arg(SettingRows::timeWords(record.made));
        }
        if (record.canUndo()) {
            item.action = QStringLiteral("undo");
            item.actionLabel = QStringLiteral("Undo");
        }
        look.items.append(item);
    }

    // What the package manager did, a day at a time.
    const QStringList cache = found.value(QStringLiteral("cache"));
    const QStringList errors = found.value(QStringLiteral("errors"));
    const QString restoreWords = look.restorePoints ? QStringLiteral("This computer takes restore points, so the restore point "
                                                                     "from before can put it all back.")
                                                    : QStringLiteral("This computer has no restore points.");
    {
        // A package is said by its app's name, "Elisa", or a known one's,
        // "VLC"; one with no app is a part of the system, named only in the
        // details for whoever helps.
        const QHash<QString, QString> apps = appNames(found.value(QStringLiteral("apps")));
        const auto appName = [&apps](const QString &package) {
            if (const auto it = apps.constFind(package); it != apps.cend())
                return it.value();
            const QString known = noticeable({{package, QString()}}).value(0);
            return known.startsWith(QLatin1String("the ")) || known == QLatin1String("sound") || known == QLatin1String("printing")
                    || known == QLatin1String("Bluetooth")
                ? QString()
                : known;
        };
        const auto partsWords = [](qsizetype n, const QString &done) {
            return n == 1 ? QStringLiteral("1 part of the system %1").arg(done)
                          : QStringLiteral("%1 parts of the system %2").arg(n).arg(done);
        };
        struct Day {
            QMap<QString, QString> updated;
            // Each package's "old -> new", or the version installed or removed.
            QMap<QString, QString> versions;
            QStringList installed;
            QStringList parts;
            QStringList removed;
            QDateTime first;
            QDateTime last;
        };
        QMap<QDate, Day> days;
        static const QRegularExpression entry(QStringLiteral(R"(\[ALPM\] (installed|upgraded|removed|downgraded|reinstalled) (\S+) \((.*)\)\s*$)"));
        for (const QString &line : found.value(QStringLiteral("pacman"))) {
            const QDateTime at = logTime(line);
            const QRegularExpressionMatch m = entry.match(line);
            if (!at.isValid() || !m.hasMatch() || at.date() < since || at.date() > today)
                continue;
            Day &day = days[at.date()];
            if (!day.first.isValid() || at < day.first)
                day.first = at;
            day.last = std::max(day.last, at);
            const QString what = m.captured(1);
            const QString name = m.captured(2);
            const QString versions = m.captured(3);
            day.versions.insert(name, versions);
            if (what == QLatin1String("upgraded") || what == QLatin1String("downgraded") || what == QLatin1String("reinstalled")) {
                day.updated.insert(name, versions.section(QStringLiteral(" -> "), -1));
            } else if (what == QLatin1String("installed")) {
                if (!appName(name).isEmpty())
                    day.installed.append(name);
                else
                    day.parts.append(name);
            } else {
                day.removed.append(name);
            }
        }
        // Each package with its versions, as "mesa 26.1.2-1 to 26.1.3-1".
        const auto versionLines = [](const Day &day, const QStringList &names) {
            QStringList lines;
            for (const QString &name : names) {
                if (lines.size() >= 60) {
                    lines.append(QStringLiteral("- and %1 more").arg(names.size() - 60));
                    break;
                }
                lines.append(QStringLiteral("- %1 %2").arg(name, QString(day.versions.value(name)).replace(QStringLiteral(" -> "), QStringLiteral(" to "))));
            }
            return lines.join(QLatin1Char('\n'));
        };
        // How many of the versions from before are still in pacman's cache.
        const auto inCache = [&cache](const QString &name, const QString &version) {
            const QString prefix = name + QLatin1Char('-') + version + QLatin1Char('-');
            return std::any_of(cache.cbegin(), cache.cend(), [&prefix](const QString &file) { return file.startsWith(prefix); });
        };
        const auto cacheWords = [&inCache](const Day &day, const QStringList &names, bool old) {
            int kept = 0;
            for (const QString &name : names) {
                const QString versions = day.versions.value(name);
                if (inCache(name, old ? versions.section(QStringLiteral(" -> "), 0, 0) : versions))
                    ++kept;
            }
            const QString which = old ? QStringLiteral("the versions from before") : QStringLiteral("them");
            if (kept == 0)
                return QStringLiteral("None of %1 are left in the package cache, /var/cache/pacman/pkg.").arg(which);
            if (kept == names.size())
                return QStringLiteral("All of %1 are still in the package cache, /var/cache/pacman/pkg, so each can be put back.").arg(which);
            return QStringLiteral("%1 of %2 are still in the package cache, /var/cache/pacman/pkg.").arg(kept).arg(names.size());
        };
        for (auto it = days.cbegin(); it != days.cend(); ++it) {
            const QDate &date = it.key();
            const Day &day = it.value();
            const QString key = date.toString(Qt::ISODate);
            const QString at = SettingRows::timeWords(day.first);
            const QString after = errorsAfter(errors, day.first);
            if (!day.updated.isEmpty()) {
                const QStringList noticed = noticeable(day.updated);
                Item item;
                item.id = QStringLiteral("update-") + key;
                item.day = date;
                item.when = day.last;
                item.kind = QStringLiteral("update");
                const qsizetype n = day.updated.size();
                item.title = n == 1 ? QStringLiteral("1 part of the system updated")
                                    : QStringLiteral("%1 parts of the system updated").arg(n);
                item.detail = noticed.isEmpty() ? QStringLiteral("Nothing a person would notice by name")
                                                : QStringLiteral("Including %1").arg(listed(noticed, QStringLiteral("others")));
                item.detail += QStringLiteral(" · ") + at;
                item.action = backAction;
                item.actionLabel = backLabel;
                item.question = QStringLiteral("My computer updated %1 parts of the system %2%3. Could that be why "
                                               "something stopped working, and how do I put it right?")
                                    .arg(n)
                                    .arg(onDay(date, today),
                                         noticed.isEmpty() ? QString() : QStringLiteral(", including %1").arg(listed(noticed)));
                item.help = QStringLiteral("My computer updated %1 parts of the system %2 at %3. Something may have stopped "
                                           "working since.\n\nWhat changed, old to new:\n%4\n\nThe way back: %5 %6\n%7\n\n"
                                           "Which of these could be the cause, and how do I put it back?")
                                .arg(n)
                                .arg(onDay(date, today), at, versionLines(day, day.updated.keys()),
                                     cacheWords(day, day.updated.keys(), true), restoreWords, after);
                look.items.append(item);
            }
            if (!day.installed.isEmpty() || !day.parts.isEmpty()) {
                QStringList names;
                for (const QString &name : day.installed)
                    names.append(appName(name));
                const QString what = names.isEmpty() ? QString() : listed(names);
                Item item;
                item.id = QStringLiteral("install-") + key;
                item.day = date;
                item.when = day.last;
                item.kind = QStringLiteral("install");
                item.title = names.isEmpty() ? partsWords(day.parts.size(), QStringLiteral("installed"))
                                             : QStringLiteral("Installed %1").arg(what);
                item.detail = names.isEmpty() || day.parts.isEmpty() ? at
                            : day.parts.size() == 1 ? QStringLiteral("And 1 part of the system · ") + at
                                                    : QStringLiteral("And %1 parts of the system · %2").arg(day.parts.size()).arg(at);
                item.action = backAction;
                item.actionLabel = backLabel;
                item.question = QStringLiteral("I installed %1 %2. Could that be why something stopped working?")
                                    .arg(names.isEmpty() ? QStringLiteral("some parts of the system") : what, onDay(date, today));
                item.help = QStringLiteral("I installed these on my computer %1 at %2:\n%3\n\nThe way back would be to remove "
                                           "them. %4\n%5\n\nCould that be why something stopped working, and how do I put it right?")
                                .arg(onDay(date, today), at, versionLines(day, day.installed + day.parts), restoreWords, after);
                look.items.append(item);
            }
            if (!day.removed.isEmpty()) {
                QStringList names;
                qsizetype parts = 0;
                for (const QString &name : day.removed) {
                    if (const QString app = appName(name); !app.isEmpty())
                        names.append(app);
                    else
                        ++parts;
                }
                const QString what = names.isEmpty() ? QString() : listed(names, QStringLiteral("others"));
                Item item;
                item.id = QStringLiteral("remove-") + key;
                item.day = date;
                item.when = day.last;
                item.kind = QStringLiteral("remove");
                item.title = names.isEmpty() ? partsWords(parts, QStringLiteral("removed")) : QStringLiteral("Removed %1").arg(what);
                item.detail = names.isEmpty() || parts == 0 ? at
                            : parts == 1 ? QStringLiteral("And 1 part of the system · ") + at
                                         : QStringLiteral("And %1 parts of the system · %2").arg(parts).arg(at);
                item.action = backAction;
                item.actionLabel = backLabel;
                item.question = QStringLiteral("These were removed from my computer %1: %2. Could that be why something "
                                               "stopped working?").arg(onDay(date, today), day.removed.mid(0, 20).join(QStringLiteral(", ")));
                item.help = QStringLiteral("These were removed from my computer %1 at %2:\n%3\n\nThe way back would be to "
                                           "install them again. %4 %5\n%6\n\nCould that be why something stopped working?")
                                .arg(onDay(date, today), at, versionLines(day, day.removed), cacheWords(day, day.removed, false),
                                     restoreWords, after);
                look.items.append(item);
            }
        }
    }

    // Flatpak apps. One updated from a commit Flathub still keeps has a row
    // of its own, to go back to it; the rest of a day's apps share one.
    {
        struct Back {
            QString installation;
            QString commit;
        };
        QHash<QString, Back> backs;
        static const QRegularExpression hex(QStringLiteral("^[0-9a-f]{64}$"));
        for (const QString &line : found.value(QStringLiteral("flatpak-commits"))) {
            const QStringList words = line.simplified().split(QLatin1Char(' '));
            if (words.size() < 7 || !words.join(QLatin1Char(' ')).contains(QLatin1String("update")))
                continue;
            const QDateTime at = flatpakTime(words.mid(0, 3).join(QLatin1Char(' ')), now);
            const QString old = words.constLast();
            if (!at.isValid() || !hex.match(old).hasMatch() || !hex.match(words.at(words.size() - 2)).hasMatch())
                continue;
            const QString installation = words.at(words.size() - 3);
            const QString app = words.at(words.size() - 4);
            backs.insert(app + QLatin1Char('@') + QString::number(at.toSecsSinceEpoch()), {installation, old});
        }
        struct Day {
            QStringList updated;
            QStringList installed;
            QStringList removed;
            QDateTime last;
        };
        QMap<QDate, Day> days;
        for (const QString &line : found.value(QStringLiteral("flatpak"))) {
            // "Oct  9 21:02:14  deploy update  org.mozilla.firefox": the time,
            // what was done, in words, and the app.
            static const QRegularExpression row(QStringLiteral(R"(^\s*(\w{3}\s+\d{1,2}\s+\d\d:\d\d:\d\d)\s+(.+?)\s+(\S+)\s*$)"));
            const QRegularExpressionMatch m = row.match(line);
            if (!m.hasMatch())
                continue;
            const QDateTime at = flatpakTime(m.captured(1), now);
            const QString change = m.captured(2);
            const QString id = m.captured(3);
            if (!at.isValid() || at.date() < since || flatpakPart(id))
                continue;
            if (change.contains(QLatin1String("update")) && !change.contains(QLatin1String("install"))) {
                const auto back = backs.constFind(id + QLatin1Char('@') + QString::number(at.toSecsSinceEpoch()));
                if (back != backs.cend()) {
                    Item item;
                    item.id = QStringLiteral("app-") + id + QLatin1Char('-') + QString::number(at.toSecsSinceEpoch());
                    item.day = at.date();
                    item.when = at;
                    item.kind = QStringLiteral("apps");
                    item.title = QStringLiteral("%1 updated").arg(flatpakName(id));
                    item.detail = QStringLiteral("From Flathub · %1").arg(SettingRows::timeWords(at));
                    item.action = QStringLiteral("goback");
                    item.actionLabel = QStringLiteral("Go back");
                    item.app = id;
                    item.installation = back->installation;
                    item.commit = back->commit;
                    item.password = back->installation != QLatin1String("user");
                    item.confirmTitle = QStringLiteral("Go back to the %1 you had before?").arg(flatpakName(id));
                    item.confirmSub = QStringLiteral("It updated %1 at %2.%3")
                                          .arg(onDay(at.date(), today), SettingRows::timeWords(at),
                                               item.password ? QStringLiteral(" Needs your password.") : QString());
                    item.confirmLines = {QStringLiteral("The version before\n%1 as it was before %2").arg(flatpakName(id), SettingRows::timeWords(at)),
                                         QStringLiteral("Undo\nYour next update brings the new version again")};
                    item.question = QStringLiteral("%1 (%2), a Flathub app, updated on my computer %3 at %4. Could that be "
                                                   "why something stopped working?")
                                        .arg(flatpakName(id), id, onDay(at.date(), today), SettingRows::timeWords(at));
                    item.help = item.question + QStringLiteral("\n\nThe way back: flatpak update --commit=%1 %2").arg(back->commit, id);
                    item.exact = {QStringLiteral("flatpak update --%1 --commit=%2 %3").arg(back->installation, back->commit, id)};
                    look.items.append(item);
                    continue;
                }
            }
            Day &day = days[at.date()];
            day.last = std::max(day.last, at);
            if (change.contains(QLatin1String("uninstall")))
                day.removed.append(flatpakName(id));
            else if (change.contains(QLatin1String("install")))
                day.installed.append(flatpakName(id));
            else if (change.contains(QLatin1String("update")))
                day.updated.append(flatpakName(id));
        }
        for (auto it = days.cbegin(); it != days.cend(); ++it) {
            const Day &day = it.value();
            QStringList said;
            if (!day.updated.isEmpty())
                said.append(QStringLiteral("updated %1").arg(listed(day.updated)));
            if (!day.installed.isEmpty())
                said.append(QStringLiteral("installed %1").arg(listed(day.installed)));
            if (!day.removed.isEmpty())
                said.append(QStringLiteral("removed %1").arg(listed(day.removed)));
            if (said.isEmpty())
                continue;
            Item item;
            item.id = QStringLiteral("apps-") + it.key().toString(Qt::ISODate);
            item.day = it.key();
            item.when = day.last;
            item.kind = QStringLiteral("apps");
            item.title = QStringLiteral("Apps from Flathub");
            QString detail = said.join(QStringLiteral("; "));
            detail[0] = detail.at(0).toUpper();
            item.detail = detail + QStringLiteral(" · ") + SettingRows::timeWords(day.last);
            item.action = QStringLiteral("ask");
            item.actionLabel = QStringLiteral("Copy for help");
            item.question = QStringLiteral("These Flathub apps changed on my computer %1: %2. Could that be why something "
                                           "stopped working?").arg(onDay(it.key(), today), said.join(QStringLiteral("; ")));
            item.help = item.question + QStringLiteral(" The last change was at %1.").arg(SettingRows::timeWords(day.last));
            look.items.append(item);
        }
    }

    // The desktop's settings. Since Core began keeping copies of them, each
    // change is a row of its own, in words, with its way back; before then a
    // file can only be named, by the day it was last written.
    {
        QMap<QDate, QStringList> days;
        QMap<QDate, QDateTime> last;
        for (const QString &line : found.value(QStringLiteral("settings"))) {
            const qsizetype space = line.indexOf(QLatin1Char(' '));
            if (space < 0)
                continue;
            bool ok = false;
            const double seconds = line.left(space).toDouble(&ok);
            const QString name = settingsName(line.mid(space + 1));
            if (!ok || name.isEmpty())
                continue;
            const QDateTime at = QDateTime::fromSecsSinceEpoch(qint64(seconds));
            if (at.date() < since || at.date() > today || (keptSince.isValid() && at >= keptSince))
                continue;
            if (!days[at.date()].contains(name))
                days[at.date()].append(name);
            last[at.date()] = std::max(last.value(at.date()), at);
        }
        for (auto it = days.cbegin(); it != days.cend(); ++it) {
            QStringList names = it.value();
            std::sort(names.begin(), names.end());
            Item item;
            item.id = QStringLiteral("settings-") + it.key().toString(Qt::ISODate);
            item.day = it.key();
            item.when = last.value(it.key());
            item.kind = QStringLiteral("settings");
            // A settings file is written by the person's own changes and, for
            // some, by the desktop keeping its state, so the card says both.
            item.title = QStringLiteral("Settings saved by you or the desktop");
            item.detail = names.join(QStringLiteral(" · "));
            item.action = QStringLiteral("ask");
            item.actionLabel = QStringLiteral("Copy for help");
            item.question = QStringLiteral("These settings changed on my computer %1: %2. What do they control, and how "
                                           "can I put one back?").arg(onDay(it.key(), today), names.join(QStringLiteral(", ")));
            item.help = item.question + QStringLiteral(" They were saved before %1 kept copies of them, so what changed in "
                                                       "them isn't known; the last was saved at %2.")
                                            .arg(productName(), SettingRows::timeWords(item.when));
            look.items.append(item);
        }
    }
    for (const SettingRow &row : settings) {
        if (!row.when.isValid() || row.when.date() < since || row.when.date() > today)
            continue;
        // Written by a change from a card here, which has its own row.
        const bool own = std::any_of(records.cbegin(), records.cend(), [&row](const ChangeRunner::Record &record) {
            const QString touched = record.proposal.command + QLatin1Char(' ') + record.proposal.undo;
            const SettingsChange &c = row.changes.constFirst();
            const bool same = touched.contains(c.file)
                || (c.file == QLatin1String("kwinoutputconfig.json") && touched.contains(QLatin1String("kscreen-doctor")))
                || (c.file == QLatin1String("kglobalshortcutsrc") && touched.contains(QLatin1String("--set-shortcut")))
                || (c.key == QLatin1String("Image") && touched.contains(QLatin1String("plasma-apply-wallpaperimage")));
            const auto near = [&row](const QDateTime &at) { return at.isValid() && qAbs(at.secsTo(row.when)) <= 60; };
            return same && (near(record.made) || near(record.undone));
        });
        if (own)
            continue;
        Item item;
        item.id = row.id;
        item.day = row.when.date();
        item.when = row.when;
        item.kind = QStringLiteral("settings");
        item.title = row.title;
        item.detail = row.detail;
        item.page = row.page;
        item.exact = row.exact;
        item.question = row.question;
        item.help = row.question + QStringLiteral("\n\nExactly, in ~/.config:\n- ") + row.exact.join(QStringLiteral("\n- "));
        if (row.alreadyBack) {
            item.detail += QStringLiteral(" · Back as it was");
        } else if (row.way == QLatin1String("putback")) {
            item.action = QStringLiteral("putback");
            item.actionLabel = QStringLiteral("Put back");
            item.putBack = row.putBack;
            item.confirmTitle = QStringLiteral("Put it back as it was?");
            item.confirmSub = QStringLiteral("%1, at %2 %3. No password needed.")
                                  .arg(row.title, SettingRows::timeWords(row.when), onDay(row.when.date(), today));
            item.confirmLines = {QStringLiteral("%1\nGoes back as it was before %2").arg(row.putBack.what, SettingRows::timeWords(row.when)),
                                 QStringLiteral("Undo\nWhat changed keeps it, with Undo, like every change from a card")};
        } else if (row.way == QLatin1String("settings")) {
            item.action = QStringLiteral("settings");
            item.actionLabel = QStringLiteral("Open in Settings");
        } else {
            item.action = QStringLiteral("ask");
            item.actionLabel = QStringLiteral("Copy for help");
        }
        look.items.append(item);
    }

    // Background services that stopped working, each with what it said last.
    for (const QString &line : found.value(QStringLiteral("failed"))) {
        const QStringList parts = line.split(QLatin1Char('|'));
        if (parts.size() < 5 || parts.at(1).isEmpty())
            continue;
        const QString unit = parts.at(1);
        const QStringList log = parts.at(3).split(QChar(0x1f), Qt::SkipEmptyParts);
        const QString what = parts.mid(4).join(QLatin1Char('|')).trimmed();
        bool ok = false;
        const qint64 seconds = parts.at(2).toLongLong(&ok);
        const QDateTime at = ok && seconds > 0 ? QDateTime::fromSecsSinceEpoch(seconds) : now;
        const bool system = parts.at(0) != QLatin1String("user");
        const QString name = what.isEmpty() || what == unit ? unit : what;
        Item item;
        item.id = QStringLiteral("service-") + unit;
        item.day = std::max(at.date(), since);
        item.when = at;
        item.kind = QStringLiteral("service");
        item.title = QStringLiteral("Stopped working: %1").arg(name);
        const QString said = lastWords(log);
        item.detail = said.isEmpty() ? (system ? QStringLiteral("A background service, %1").arg(unit)
                                               : QStringLiteral("A background service of yours, %1").arg(unit))
                                     : QStringLiteral("Said \"%1\"").arg(said);
        item.detail += QStringLiteral(" · ") + SettingRows::timeWords(at);
        item.unit = unit;
        item.systemUnit = system;
        item.password = system;
        item.action = QStringLiteral("start");
        item.actionLabel = QStringLiteral("Start it again");
        item.confirmTitle = QStringLiteral("Start %1 again?").arg(name);
        item.confirmSub = QStringLiteral("It stopped %1 at %2.%3")
                              .arg(onDay(item.day, today), SettingRows::timeWords(at),
                                   system ? QStringLiteral(" It's part of the system, so it needs your password.")
                                          : QStringLiteral(" No password needed."));
        item.confirmLines = {said.isEmpty() ? QStringLiteral("It said nothing about why") : QStringLiteral("What it said last\n%1").arg(said),
                             QStringLiteral("Nothing to undo\nIt only starts the service again")};
        item.exact = log;
        item.question = QStringLiteral("The background service %1%2 stopped working on my computer %3. What does it do, "
                                       "and can you get it working again?")
                            .arg(unit, what.isEmpty() || what == unit ? QString() : QStringLiteral(" (%1)").arg(what),
                                 onDay(item.day, today));
        item.help = QStringLiteral("The background service %1%2 stopped working on my computer %3 at %4.\n\nWhat it said last:\n%5\n\n"
                                   "What does it do, and how do I get it working again?")
                        .arg(unit, what.isEmpty() || what == unit ? QString() : QStringLiteral(" (%1)").arg(what),
                             onDay(item.day, today), SettingRows::timeWords(at),
                             log.isEmpty() ? QStringLiteral("(nothing)") : log.join(QLatin1Char('\n')));
        look.items.append(item);
    }

    // A new kernel installed under the one running: the update finishes at
    // the next start.
    {
        const QStringList kernel = found.value(QStringLiteral("kernel"));
        if (kernel.size() >= 2 && kernel.at(1) == QLatin1String("missing")) {
            Item item;
            item.id = QStringLiteral("restart");
            item.day = today;
            item.when = now;
            item.kind = QStringLiteral("restart");
            item.title = QStringLiteral("A restart finishes the last update");
            item.detail = QStringLiteral("A new Linux kernel is installed; until a restart, some devices and apps may not work");
            item.action = QStringLiteral("restart");
            item.actionLabel = QStringLiteral("Restart");
            look.items.append(item);
        }
    }

    // Newest day first; within a day, a restart first, then changes made from
    // cards, then the rest newest first.
    const auto rank = [](const Item &item) {
        return item.kind == QLatin1String("restart") ? 0 : item.kind == QLatin1String("own") ? 1 : 2;
    };
    std::stable_sort(look.items.begin(), look.items.end(), [&rank](const Item &a, const Item &b) {
        if (a.day != b.day)
            return a.day > b.day;
        if (rank(a) != rank(b))
            return rank(a) < rank(b);
        return a.when > b.when;
    });
    return look;
}

QVariantList RecentChanges::itemList() const
{
    QVariantList list;
    const QDate today = QDate::currentDate();
    for (const Item &item : m_look.items)
        list.append(item.toMap(today));
    return list;
}

QVariantMap RecentChanges::latestMap() const
{
    for (const Item &item : m_look.items) {
        if (item.kind == QLatin1String("own") && item.action == QLatin1String("undo"))
            return item.toMap(QDate::currentDate());
    }
    return {};
}

QString RecentChanges::question() const
{
    if (m_look.items.isEmpty())
        return {};
    QString list;
    const QDate today = QDate::currentDate();
    for (const Item &item : m_look.items) {
        const QString line = QStringLiteral("- %1: %2%3\n")
                                 .arg(dayWords(item.day, today), item.title,
                                      item.detail.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(item.detail));
        if (list.size() + line.size() > 1500)
            break;
        list += line;
    }
    return QStringLiteral("Something on my computer isn't working the way it did. Here is what changed in the last two "
                          "weeks, newest first:\n%1Which of these could be the cause, and what should I do?").arg(list);
}

void RecentChanges::readSettings()
{
    const QString configHome = m_configHome.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                                                      : m_configHome;
    SettingsHistory history(m_historyFolder.isEmpty() ? SettingsHistory::defaultFolder() : m_historyFolder, configHome);
    // A look of its own too, for what changed since Core's last; the first
    // ever only takes the copies.
    history.look(QDateTime::currentDateTime());
    m_keptSince = history.keptSince();
    const QString program = m_program.isEmpty() ? QCoreApplication::applicationFilePath() : m_program;
    // The value each setting has now, for a row put back already.
    QHash<QString, QMap<QString, QMap<QString, QString>>> read;
    const auto current = [&history, &read](const SettingsChange &change, bool *set) {
        if (!read.contains(change.file)) {
            QFile file(history.pathOf(change.file));
            const QByteArray text = file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
            read.insert(change.file, change.file == QLatin1String("kwinoutputconfig.json") ? SettingsHistory::readOutputs(text)
                                                                                          : SettingsHistory::readIni(text));
        }
        const QMap<QString, QString> keys = read.value(change.file).value(change.group);
        *set = keys.contains(change.key);
        return keys.value(change.key);
    };
    m_settings = SettingRows::rows(history.changes(QDateTime::currentDateTime().addDays(-Days)), program, current);
}

void RecentChanges::check()
{
    if (busy())
        return;
    clearProgress();
    m_notice.clear();
    setStage(QStringLiteral("looking"));
    run(asPerson(m_lookScript), 30000, [this](int, const QString &output) {
        m_lastOutput = output;
        readSettings();
        m_look = readLook(output, m_records ? m_records() : QList<ChangeRunner::Record>(), QDateTime::currentDateTime(),
                          m_settings, m_keptSince);
        settle();
    });
}

void RecentChanges::refreshOwn()
{
    if (stage().isEmpty() || busy())
        return;
    readSettings();
    m_look = readLook(m_lastOutput, m_records ? m_records() : QList<ChangeRunner::Record>(), QDateTime::currentDateTime(),
                      m_settings, m_keptSince);
    settle();
}

std::optional<RecentChanges::Item> RecentChanges::find(const QString &id) const
{
    for (const Item &item : m_look.items) {
        if (item.id == id)
            return item;
    }
    return std::nullopt;
}

std::optional<ChangeProposal> RecentChanges::putBack(const QString &id) const
{
    const std::optional<Item> item = find(id);
    if (!item || item->action != QLatin1String("putback") || item->putBack.command.isEmpty())
        return std::nullopt;
    return item->putBack;
}

void RecentChanges::puttingBack(const QString &id)
{
    const std::optional<Item> item = find(id);
    m_notice.clear();
    setStage(QStringLiteral("working"));
    setNow(item ? QStringLiteral("Putting it back: %1").arg(item->title)
                : QStringLiteral("Putting it back"));
    Q_EMIT changed();
}

void RecentChanges::putBackDone(const ChangeRunner::Record &record)
{
    if (stage() != QLatin1String("working"))
        return;
    finishJob(record.status == QLatin1String("done")
                  ? QStringLiteral("Put back as it was. Undo is on its row, at the top.")
                  : whyFailed(record.output, record.exitCode, QStringLiteral("Putting it back")));
}

void RecentChanges::finishJob(const QString &said)
{
    // A fresh look, then what the job came to, under it.
    stopRunning();
    run(asPerson(m_lookScript), 30000, [this, said](int, const QString &output) {
        m_lastOutput = output;
        readSettings();
        m_look = readLook(output, m_records ? m_records() : QList<ChangeRunner::Record>(), QDateTime::currentDateTime(),
                          m_settings, m_keptSince);
        settle();
        m_notice = said;
        Q_EMIT changed();
    });
}

void RecentChanges::startAgain(const QString &id)
{
    const std::optional<Item> item = find(id);
    if (busy() || stage() == QLatin1String("working") || !item || item->action != QLatin1String("start"))
        return;
    const QString title = QStringLiteral("Start %1 again").arg(item->unit);
    QString error;
    if (!writeDown(item->id, title, QStringLiteral("Nothing to put back: it only starts the service again"), &error)) {
        m_notice = error;
        Q_EMIT changed();
        return;
    }
    const QStringList program = item->systemUnit
        ? asAdmin(QStringLiteral("start-service"), {item->unit})
        : asPerson(QStringLiteral("systemctl --user reset-failed -- \"$1\"; systemctl --user restart -- \"$1\" && sleep 2 && "
                                  "systemctl --user is-active --quiet -- \"$1\" || { journalctl --user -u \"$1\" -n 12 -o cat "
                                  "--no-pager -q; exit 1; }"),
                   {item->unit});
    if (program.isEmpty()) {
        m_notice = QStringLiteral("It can't be started from here: the system's part of %1 isn't installed as it should be.").arg(productName());
        Q_EMIT changed();
        return;
    }
    m_notice.clear();
    setStage(QStringLiteral("working"));
    setNow(QStringLiteral("Starting %1 again").arg(item->unit));
    const QString name = item->title.mid(QStringLiteral("Stopped working: ").size());
    run(program, 60000, [this, id = item->id, name](int exitCode, const QString &output) {
        report(id, exitCode == 0, {}, output);
        const QString said = lastWords(output.split(QLatin1Char('\n'), Qt::SkipEmptyParts));
        finishJob(exitCode == 0 ? QStringLiteral("%1 is running again.").arg(name)
                  : exitCode == 126 || exitCode == 127 ? QStringLiteral("It wasn't started: the password wasn't given.")
                  : said.isEmpty() ? QStringLiteral("%1 stopped again. Copy for help has what it said.").arg(name)
                                   : QStringLiteral("%1 stopped again. It said: %2").arg(name, said));
    });
    Q_EMIT changed();
}

void RecentChanges::goBack(const QString &id)
{
    const std::optional<Item> item = find(id);
    if (busy() || stage() == QLatin1String("working") || !item || item->action != QLatin1String("goback"))
        return;
    const QString name = flatpakName(item->app);
    const bool user = item->installation == QLatin1String("user");
    QString error;
    if (!writeDown(item->id, QStringLiteral("Take %1 back to the version before").arg(name),
                   QStringLiteral("Update it again: flatpak update %1%2").arg(user ? QStringLiteral("--user ") : QString(), item->app),
                   &error)) {
        m_notice = error;
        Q_EMIT changed();
        return;
    }
    const QStringList program = user
        ? asPerson(QStringLiteral("flatpak update --user -y --noninteractive --commit=\"$2\" -- \"$1\""), {item->app, item->commit})
        : asAdmin(QStringLiteral("flatpak-back"), {item->app, item->commit});
    if (program.isEmpty()) {
        m_notice = QStringLiteral("It can't go back from here: the system's part of %1 isn't installed as it should be.").arg(productName());
        Q_EMIT changed();
        return;
    }
    m_notice.clear();
    setStage(QStringLiteral("working"));
    setNow(QStringLiteral("Taking %1 back to the version before").arg(name));
    run(program, 600000, [this, id = item->id, name](int exitCode, const QString &output) {
        report(id, exitCode == 0, {}, output);
        finishJob(exitCode == 0 ? QStringLiteral("%1 is back to the version before.").arg(name)
                  : exitCode == 126 || exitCode == 127 ? QStringLiteral("It didn't go back: the password wasn't given.")
                                                       : whyFailed(output, exitCode, QStringLiteral("Going back")));
    });
    Q_EMIT changed();
}

void RecentChanges::openSettings(const QString &page)
{
    static const QRegularExpression shape(QStringLiteral("^kcm_[a-z0-9_]+$"));
    if (!shape.match(page).hasMatch())
        return;
    const QString settings = QStandardPaths::findExecutable(QStringLiteral("systemsettings"));
    if (!settings.isEmpty())
        QProcess::startDetached(settings, {page});
    else
        QProcess::startDetached(QStringLiteral("kcmshell6"), {page});
}

void RecentChanges::settle()
{
    qInfo().noquote() << QStringLiteral("%1: %2 recent changes").arg(productName()).arg(m_look.items.size());
    setStage(m_look.items.isEmpty() ? QStringLiteral("nothing") : QStringLiteral("ready"));
    Q_EMIT changed();
}

} // namespace splitrock
