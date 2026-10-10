// SPDX-License-Identifier: GPL-2.0-or-later
#include "app/RecentChanges.h"

#include "app/Product.h"

#include <QDebug>
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
echo "@@ explicit"
pacman -Qqe 2>/dev/null
echo "@@ flatpak"
command -v flatpak >/dev/null 2>&1 && flatpak history --since="$since 00:00:00" --columns=time,change,application 2>/dev/null \
    | tail -n 400
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
        echo "$scope|$unit|${at#@}|$what"
    done
done
echo "@@ kernel"
uname -r
# Missing only counts where kernels are installed as packages at all.
if [ -d "/usr/lib/modules/$(uname -r)" ] || [ -z "$(ls -A /usr/lib/modules 2>/dev/null)" ]; then echo present; else echo missing; fi
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

// App parts and runtimes a person never installed by name.
bool flatpakPart(const QString &id)
{
    static const QRegularExpression part(QStringLiteral(R"(\.(Platform|Sdk|GL|GL32|Locale|Debug|Sources|KStyle|PlatformTheme|Gtk3theme|openh264|ffmpeg-full|Codecs|VAAPI)(\.|$))"));
    return part.match(id).hasMatch() || id.startsWith(QLatin1String("org.freedesktop.")) || id.startsWith(QLatin1String("org.gtk.Gtk3theme"));
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
            {QStringLiteral("question"), question}};
}

RecentChanges::RecentChanges(QObject *parent)
    : Chore(parent)
    , m_lookScript(LookScript)
{
}

bool RecentChanges::busy() const
{
    return stage() == QLatin1String("looking");
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

RecentChanges::Look RecentChanges::readLook(const QString &output, const QList<ChangeRunner::Record> &records, const QDateTime &now)
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
            item.detail = QStringLiteral("Made from a card you approved, at %1").arg(record.made.toString(QStringLiteral("HH:mm")));
        }
        if (record.canUndo()) {
            item.action = QStringLiteral("undo");
            item.actionLabel = QStringLiteral("Undo");
        }
        look.items.append(item);
    }

    // What the package manager did, a day at a time.
    {
        const QSet<QString> explicitly = [&found] {
            const QStringList names = found.value(QStringLiteral("explicit"));
            return QSet<QString>(names.cbegin(), names.cend());
        }();
        struct Day {
            QMap<QString, QString> updated;
            QStringList installed;
            int parts = 0;
            QStringList removed;
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
            day.last = std::max(day.last, at);
            const QString what = m.captured(1);
            const QString name = m.captured(2);
            const QString versions = m.captured(3);
            if (what == QLatin1String("upgraded") || what == QLatin1String("downgraded") || what == QLatin1String("reinstalled")) {
                day.updated.insert(name, versions.section(QStringLiteral(" -> "), -1));
            } else if (what == QLatin1String("installed")) {
                if (explicitly.contains(name))
                    day.installed.append(name);
                else
                    ++day.parts;
            } else {
                day.removed.append(name);
            }
        }
        for (auto it = days.cbegin(); it != days.cend(); ++it) {
            const QDate &date = it.key();
            const Day &day = it.value();
            const QString key = date.toString(Qt::ISODate);
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
                item.action = backAction;
                item.actionLabel = backLabel;
                item.question = QStringLiteral("My computer updated %1 parts of the system %2%3. Could that be why "
                                               "something stopped working, and how do I put it right?")
                                    .arg(n)
                                    .arg(onDay(date, today),
                                         noticed.isEmpty() ? QString() : QStringLiteral(", including %1").arg(listed(noticed)));
                look.items.append(item);
            }
            if (!day.installed.isEmpty() || day.parts > 0) {
                Item item;
                item.id = QStringLiteral("install-") + key;
                item.day = date;
                item.when = day.last;
                item.kind = QStringLiteral("install");
                item.title = day.installed.isEmpty() ? QStringLiteral("Installed parts of the system")
                                                     : QStringLiteral("Installed %1").arg(listed(day.installed));
                item.detail = day.parts == 0 ? QString()
                            : day.parts == 1 ? QStringLiteral("With 1 part it needs")
                                             : QStringLiteral("With %1 parts they need").arg(day.parts);
                item.action = backAction;
                item.actionLabel = backLabel;
                item.question = QStringLiteral("I installed %1 %2. Could that be why something stopped working?")
                                    .arg(day.installed.isEmpty() ? QStringLiteral("some parts of the system") : listed(day.installed),
                                         onDay(date, today));
                look.items.append(item);
            }
            if (!day.removed.isEmpty()) {
                Item item;
                item.id = QStringLiteral("remove-") + key;
                item.day = date;
                item.when = day.last;
                item.kind = QStringLiteral("remove");
                item.title = QStringLiteral("Removed %1").arg(listed(day.removed, QStringLiteral("others")));
                item.action = backAction;
                item.actionLabel = backLabel;
                item.question = QStringLiteral("These were removed from my computer %1: %2. Could that be why something "
                                               "stopped working?").arg(onDay(date, today), day.removed.mid(0, 20).join(QStringLiteral(", ")));
                look.items.append(item);
            }
        }
    }

    // Flatpak apps.
    {
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
            item.detail = detail;
            item.action = QStringLiteral("ask");
            item.actionLabel = QStringLiteral("Ask about it");
            item.question = QStringLiteral("These Flathub apps changed on my computer %1: %2. Could that be why something "
                                           "stopped working?").arg(onDay(it.key(), today), said.join(QStringLiteral("; ")));
            look.items.append(item);
        }
    }

    // The desktop's settings, by the day each was last written.
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
            if (at.date() < since || at.date() > today)
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
            item.actionLabel = QStringLiteral("Ask about it");
            item.question = QStringLiteral("These settings changed on my computer %1: %2. What do they control, and how "
                                           "can I put one back?").arg(onDay(it.key(), today), names.join(QStringLiteral(", ")));
            look.items.append(item);
        }
    }

    // Background services that stopped working.
    for (const QString &line : found.value(QStringLiteral("failed"))) {
        const QStringList parts = line.split(QLatin1Char('|'));
        if (parts.size() < 4 || parts.at(1).isEmpty())
            continue;
        const QString unit = parts.at(1);
        const QString what = parts.mid(3).join(QLatin1Char('|')).trimmed();
        bool ok = false;
        const qint64 seconds = parts.at(2).toLongLong(&ok);
        const QDateTime at = ok && seconds > 0 ? QDateTime::fromSecsSinceEpoch(seconds) : now;
        Item item;
        item.id = QStringLiteral("service-") + unit;
        item.day = std::max(at.date(), since);
        item.when = at;
        item.kind = QStringLiteral("service");
        item.title = QStringLiteral("Stopped working: %1").arg(what.isEmpty() || what == unit ? unit : what);
        item.detail = parts.at(0) == QLatin1String("user") ? QStringLiteral("A background service of yours, %1").arg(unit)
                                                           : QStringLiteral("A background service, %1").arg(unit);
        item.action = QStringLiteral("ask");
        item.actionLabel = QStringLiteral("Ask about it");
        item.question = QStringLiteral("The background service %1%2 stopped working on my computer %3. What does it do, "
                                       "and can you get it working again?")
                            .arg(unit, what.isEmpty() || what == unit ? QString() : QStringLiteral(" (%1)").arg(what),
                                 onDay(item.day, today));
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

void RecentChanges::check()
{
    if (busy())
        return;
    clearProgress();
    setStage(QStringLiteral("looking"));
    run(asPerson(m_lookScript), 30000, [this](int, const QString &output) {
        m_lastOutput = output;
        m_look = readLook(output, m_records ? m_records() : QList<ChangeRunner::Record>());
        settle();
    });
}

void RecentChanges::refreshOwn()
{
    if (stage().isEmpty() || busy())
        return;
    m_look = readLook(m_lastOutput, m_records ? m_records() : QList<ChangeRunner::Record>());
    settle();
}

void RecentChanges::settle()
{
    qInfo().noquote() << QStringLiteral("%1: %2 recent changes").arg(productName()).arg(m_look.items.size());
    setStage(m_look.items.isEmpty() ? QStringLiteral("nothing") : QStringLiteral("ready"));
    Q_EMIT changed();
}

} // namespace splitrock
