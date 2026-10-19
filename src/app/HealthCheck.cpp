// SPDX-License-Identifier: GPL-2.0-or-later
#include "app/HealthCheck.h"

#include "app/Product.h"
#include "app/RestorePoints.h"

#include <QDebug>
#include <QProcess>
#include <QStandardPaths>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QFile>
#include <QSettings>
#include <QSysInfo>
#include <QVariantMap>

#include <algorithm>

namespace splitrock {

namespace {

// Run as the person: nothing here changes anything.
const QString LookScript = QStringLiteral(R"SH(export LANG=C LC_ALL=C
echo "@@ failed"
systemctl --failed --no-legend --plain 2>/dev/null | awk '{print $1}'
systemctl --user --failed --no-legend --plain 2>/dev/null | awk '{print $1}'
echo "@@ disk"
df -Pk / 2>/dev/null | awk 'NR==2 {print $2, $4}'
echo "@@ drive"
command -v udisksctl >/dev/null 2>&1 && udisksctl dump 2>/dev/null \
    | grep -E '^[[:space:]]*(SmartFailing|SmartCriticalWarning|SmartPercentUsed):'
echo "@@ battery"
for d in /sys/class/power_supply/*; do
    [ "$(cat "$d/type" 2>/dev/null)" = Battery ] || continue
    [ "$(cat "$d/scope" 2>/dev/null)" = Device ] && continue
    full=$(cat "$d/energy_full" 2>/dev/null || cat "$d/charge_full" 2>/dev/null)
    design=$(cat "$d/energy_full_design" 2>/dev/null || cat "$d/charge_full_design" 2>/dev/null)
    echo "$full $design $(cat "$d/cycle_count" 2>/dev/null)"
done
echo "@@ lastupdate"
grep -a "starting full system upgrade" /var/log/pacman.log 2>/dev/null | tail -n 1
echo "@@ snapper"
command -v snapper >/dev/null 2>&1 && [ -f /etc/snapper/configs/root ] && echo yes
echo "@@ points"
command -v snapper >/dev/null 2>&1 && snapper -c root --jsonout list 2>/dev/null
echo "@@ pacnew"
find /etc -name '*.pacnew' 2>/dev/null
echo "@@ crashes"
command -v coredumpctl >/dev/null 2>&1 && coredumpctl list --since "7 days ago" --json=short --no-pager 2>/dev/null
echo "@@ pkgcache"
du -sk /var/cache/pacman/pkg 2>/dev/null | cut -f1
echo "@@ kernel"
uname -r
# Missing only counts where kernels are installed as packages at all.
if [ -d "/usr/lib/modules/$(uname -r)" ] || [ -z "$(ls -A /usr/lib/modules 2>/dev/null)" ]; then echo present; else echo missing; fi
echo "@@ packages"
command -v pacman >/dev/null 2>&1 && pacman -Dk 2>&1 | grep '^error' | head -n 20
echo "@@ lock"
[ -e /var/lib/pacman/db.lck ] && ! pgrep -x 'pacman|paru|yay|pamac|pamac-daemon|packagekitd' >/dev/null 2>&1 && echo stuck
echo "@@ downloads"
d=$(xdg-user-dir DOWNLOAD 2>/dev/null)
{ [ -z "$d" ] || [ "$d" = "$HOME" ]; } && d="$HOME/Downloads"
[ -d "$d" ] && find "$d" -mindepth 1 -maxdepth 1 -type f -ctime +90 -atime +90 -printf '%s\n' 2>/dev/null
echo "@@ end"
)SH");

QDateTime logTime(const QString &line)
{
    static const QRegularExpression stamp(QStringLiteral(R"(^\[(\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d)([+-]\d\d)(\d\d)\])"));
    const QRegularExpressionMatch m = stamp.match(line);
    if (!m.hasMatch())
        return {};
    return QDateTime::fromString(m.captured(1) + m.captured(2) + QLatin1Char(':') + m.captured(3), Qt::ISODate);
}

QString days(qint64 n)
{
    if (n <= 0)
        return QStringLiteral("today");
    if (n == 1)
        return QStringLiteral("yesterday");
    return QStringLiteral("%1 days ago").arg(n);
}

// Where the last look's time and findings are kept: the only things the check
// writes.
const QString Organization = QStringLiteral("split-rock");
const QString Settings = QStringLiteral("upkeep");

// The question about this week's crashes, with what Split Rock knows of each
// app, so the answer is about these apps rather than crashes in general.
QString crashQuestion(const QList<std::pair<QString, int>> &apps, int total, int together)
{
    static const QHash<QString, QString> known{
        {QStringLiteral("xdg-desktop-portal-kde"),
         QStringLiteral("the part of the desktop that opens file pickers, screenshots and screen sharing for apps; "
                        "it often crashes as the desktop shuts down at a sign-out or restart, or after a desktop "
                        "update until the next restart")},
        {QStringLiteral("kscreen-doctor"),
         QStringLiteral("a small tool that other programs run to change screen settings such as the refresh rate or "
                        "resolution; nobody opens it by hand, so a program on this computer runs it, such as a "
                        "power or display tool, and that program is the one to look at")},
        {QStringLiteral("ksecretd"),
         QStringLiteral("the desktop's password keeper (KWallet), which apps ask for saved passwords; it can crash "
                        "as the desktop shuts down or while the wallet is locked")},
        {QStringLiteral("kwin_wayland"),
         QStringLiteral("the part that draws every window; when it crashes, windows flash or the session restarts")},
        {QStringLiteral("plasmashell"),
         QStringLiteral("the desktop itself: the panel, the wallpaper and its widgets")},
        {QStringLiteral("kded6"), QStringLiteral("the desktop's background helper for many small services")},
    };
    QStringList names;
    QStringList about;
    for (qsizetype i = 0; i < std::min<qsizetype>(3, apps.size()); ++i) {
        const auto &[name, count] = apps.at(i);
        names.append(count > 1 ? QStringLiteral("%1 (%2 times)").arg(name).arg(count) : name);
        if (known.contains(name))
            about.append(QStringLiteral("%1 is %2.").arg(name, known.value(name)));
    }
    QString question = QStringLiteral("These apps crashed on my computer this week: %1.").arg(names.join(QStringLiteral(", ")));
    if (!about.isEmpty())
        question += QLatin1Char(' ') + about.join(QLatin1Char(' '));
    if (total > 1 && together * 2 >= total)
        question += QStringLiteral(" Most of the crashes happened at the same moments as each other, which points "
                                   "to one cause, such as signing out or restarting, rather than each app failing on "
                                   "its own.");
    else if (total > 1)
        question += QStringLiteral(" They happened at different times, each on its own.");
    question += QStringLiteral(" For each app, say in a sentence what it is and the likely reason here, then what I "
                               "can do.");
    return question;
}

} // namespace

QVariantMap HealthCheck::Item::toMap() const
{
    return {{QStringLiteral("id"), id},
            {QStringLiteral("title"), title},
            {QStringLiteral("state"), state},
            {QStringLiteral("detail"), detail},
            {QStringLiteral("action"), action},
            {QStringLiteral("actionLabel"), actionLabel},
            {QStringLiteral("question"), question}};
}

HealthCheck::Item HealthCheck::Item::fromMap(const QVariantMap &map)
{
    return {map.value(QStringLiteral("id")).toString(),        map.value(QStringLiteral("title")).toString(),
            map.value(QStringLiteral("state")).toString(),     map.value(QStringLiteral("detail")).toString(),
            map.value(QStringLiteral("action")).toString(),    map.value(QStringLiteral("actionLabel")).toString(),
            map.value(QStringLiteral("question")).toString()};
}

QString HealthCheck::tile(const Item &item)
{
    if (item.state == QLatin1String("good"))
        return {};
    // The detail's words without its full stop.
    QString detail = item.detail;
    if (detail.endsWith(QLatin1Char('.')))
        detail.chop(1);
    if (item.id == QLatin1String("services")) {
        const int n = detail.startsWith(QLatin1String("One ")) ? 1 : detail.section(QLatin1Char(' '), 0, 0).toInt();
        return n > 1 ? QStringLiteral("%1 background services didn't start").arg(n)
                     : QStringLiteral("A background service didn't start");
    }
    if (item.id == QLatin1String("space"))
        return QStringLiteral("Only %1 free on the disk").arg(detail.section(QStringLiteral(" free"), 0, 0));
    if (item.id == QLatin1String("drive"))
        return item.state == QLatin1String("bad") ? QStringLiteral("The drive reports a problem")
                                                  : QStringLiteral("The drive is wearing out");
    if (item.id == QLatin1String("battery"))
        return QStringLiteral("The battery has worn down");
    if (item.id == QLatin1String("updates"))
        return detail;
    if (item.id == QLatin1String("restore"))
        return item.action == QLatin1String("ask") ? QStringLiteral("No restore points are being taken")
                                                   : QStringLiteral("The newest restore point is old");
    if (item.id == QLatin1String("settings"))
        return QStringLiteral("New settings files wait beside yours");
    if (item.id == QLatin1String("crashes"))
        return QStringLiteral("%1 crashes this week").arg(detail.section(QLatin1Char(' '), 0, 0));
    if (item.id == QLatin1String("restart"))
        return QStringLiteral("Restart to finish the update");
    if (item.id == QLatin1String("packages"))
        return item.title == QLatin1String("Installing") ? QStringLiteral("An update was cut off")
                                                         : QStringLiteral("The installed apps list has a problem");
    if (item.id == QLatin1String("downloads"))
        return QStringLiteral("Old downloads take %1").arg(detail.section(QStringLiteral(" take "), 1));
    if (item.id == QLatin1String("pkgcache"))
        return QStringLiteral("Old update copies take %1").arg(detail.section(QStringLiteral("take "), 1));
    return item.title;
}

HealthCheck::HealthCheck(QObject *parent)
    : Chore(parent)
    , m_checked(QSettings(Organization, Settings).value(QStringLiteral("health/checked")).toDateTime())
    , m_lastAttention(QSettings(Organization, Settings).value(QStringLiteral("health/attention"), -1).toInt())
    , m_lookScript(LookScript)
    , m_coreSettings(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/split-rock-corerc"))
{
    for (const QVariant &item : QSettings(Organization, Settings).value(QStringLiteral("health/items")).toList())
        m_items.append(Item::fromMap(item.toMap()));
}

bool HealthCheck::busy() const
{
    return stage() == QLatin1String("looking");
}

QList<HealthCheck::Item> HealthCheck::readLook(const QString &output, const QDateTime &now)
{
    const QMap<QString, QStringList> found = sections(output);
    QList<Item> items;

    // Services.
    {
        QStringList failed = found.value(QStringLiteral("failed"));
        failed.removeDuplicates();
        if (failed.isEmpty()) {
            items.append({QStringLiteral("services"), QStringLiteral("Services"), QStringLiteral("good"),
                          QStringLiteral("Everything that runs in the background started."), {}, {}, {}});
        } else {
            const QString names = failed.mid(0, 3).join(QStringLiteral(", ")) + (failed.size() > 3 ? QStringLiteral("…") : QString());
            items.append({QStringLiteral("services"), QStringLiteral("Services"), QStringLiteral("bad"),
                          failed.size() == 1 ? QStringLiteral("One didn't start: %1.").arg(names)
                                             : QStringLiteral("%1 didn't start: %2.").arg(failed.size()).arg(names),
                          QStringLiteral("ask"), QStringLiteral("Ask about it"),
                          QStringLiteral("These background services failed to start on my computer: %1. What do they do, "
                                         "and does it matter?").arg(failed.join(QStringLiteral(", ")))});
        }
    }

    // Space.
    {
        const QStringList disk = found.value(QStringLiteral("disk")).value(0).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (disk.size() == 2 && disk.at(0).toLongLong() > 0) {
            const qint64 total = disk.at(0).toLongLong();
            const qint64 free = disk.at(1).toLongLong();
            const double share = double(free) / double(total);
            const QString words = QStringLiteral("%1 free of %2.").arg(size(free), size(total));
            const QString state = share < 0.05 || free < 2 * 1024 * 1024 ? QStringLiteral("bad")
                                : share < 0.15                           ? QStringLiteral("warn")
                                                                         : QStringLiteral("good");
            items.append({QStringLiteral("space"), QStringLiteral("Space"), state, words,
                          state == QLatin1String("good") ? QString() : QStringLiteral("space"),
                          state == QLatin1String("good") ? QString() : QStringLiteral("Free up space"), {}});
        }
    }

    // The drive's own report, where it gives one.
    {
        bool failing = false;
        bool warning = false;
        int worn = -1;
        for (const QString &line : found.value(QStringLiteral("drive"))) {
            const QString value = line.section(QLatin1Char(':'), 1).trimmed();
            if (line.startsWith(QLatin1String("SmartFailing")))
                failing = failing || value == QLatin1String("true");
            else if (line.startsWith(QLatin1String("SmartCriticalWarning")))
                warning = warning || (!value.isEmpty() && value != QLatin1String("[]") && value != QLatin1String("0"));
            else if (line.startsWith(QLatin1String("SmartPercentUsed")))
                worn = std::max(worn, value.toInt());
        }
        if (failing || warning) {
            items.append({QStringLiteral("drive"), QStringLiteral("Drive"), QStringLiteral("bad"),
                          QStringLiteral("The drive reports a problem. Copy your files somewhere safe soon."),
                          QStringLiteral("ask"), QStringLiteral("Ask about it"),
                          QStringLiteral("My computer's drive reports a health problem. How do I back up my files, and "
                                         "what should I do next?")});
        } else if (!found.value(QStringLiteral("drive")).isEmpty()) {
            items.append({QStringLiteral("drive"), QStringLiteral("Drive"),
                          worn >= 90 ? QStringLiteral("warn") : QStringLiteral("good"),
                          worn >= 90 ? QStringLiteral("It reports %1% of its life used.").arg(worn)
                                     : QStringLiteral("It reports no problems."),
                          {}, {}, {}});
        }
    }

    // The battery: what it holds now, against when it was new.
    {
        const QStringList parts = found.value(QStringLiteral("battery")).value(0).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (parts.size() >= 2 && parts.at(1).toLongLong() > 0) {
            const int health = qRound(100.0 * parts.at(0).toLongLong() / parts.at(1).toLongLong());
            const int cycles = parts.value(2).toInt();
            QString words = QStringLiteral("Holds %1% of what it did when new").arg(std::min(health, 100));
            if (cycles > 0)
                words += QStringLiteral(", after %1 charges").arg(cycles);
            items.append({QStringLiteral("battery"), QStringLiteral("Battery"),
                          health < 60 ? QStringLiteral("warn") : QStringLiteral("good"), words + QLatin1Char('.'), {}, {}, {}});
        }
    }

    // Updates.
    {
        const QDateTime last = logTime(found.value(QStringLiteral("lastupdate")).value(0));
        if (last.isValid()) {
            const qint64 ago = last.daysTo(now);
            const QString state = ago > 30 ? QStringLiteral("warn") : QStringLiteral("good");
            items.append({QStringLiteral("updates"), QStringLiteral("Updates"), state,
                          QStringLiteral("Last updated %1.").arg(days(ago)),
                          ago > 14 ? QStringLiteral("update") : QString(),
                          ago > 14 ? QStringLiteral("Check for updates") : QString(), {}});
        }
    }

    // Restore points.
    {
        if (!found.value(QStringLiteral("snapper")).contains(QStringLiteral("yes"))) {
            items.append({QStringLiteral("restore"), QStringLiteral("Restore points"), QStringLiteral("warn"),
                          QStringLiteral("This computer doesn't take them, so a bad update can't be gone back from."),
                          QStringLiteral("ask"), QStringLiteral("Ask about it"),
                          QStringLiteral("My computer doesn't take restore points (snapshots). Should it, and how do I "
                                         "set them up safely?")});
        } else {
            const QList<RestorePoints::Point> points
                = RestorePoints::readList(found.value(QStringLiteral("points")).join(QLatin1Char('\n')));
            if (!points.isEmpty() && points.first().date.isValid()) {
                const qint64 ago = points.first().date.daysTo(now);
                items.append({QStringLiteral("restore"), QStringLiteral("Restore points"),
                              ago > 30 ? QStringLiteral("warn") : QStringLiteral("good"),
                              QStringLiteral("%1 kept; the newest is from %2.").arg(points.size()).arg(days(ago)),
                              QStringLiteral("restore"), QStringLiteral("See them"), {}});
            } else {
                items.append({QStringLiteral("restore"), QStringLiteral("Restore points"), QStringLiteral("good"),
                              QStringLiteral("Taken before each change."), QStringLiteral("restore"),
                              QStringLiteral("See them"), {}});
            }
        }
    }

    // Settings files an update left beside the person's own.
    {
        const QStringList files = found.value(QStringLiteral("pacnew"));
        if (!files.isEmpty())
            items.append({QStringLiteral("settings"), QStringLiteral("Settings files"), QStringLiteral("warn"),
                          files.size() == 1 ? QStringLiteral("1 has a new version waiting beside yours.")
                                            : QStringLiteral("%1 have new versions waiting beside yours.").arg(files.size()),
                          QStringLiteral("ask"), QStringLiteral("Ask about them"),
                          QStringLiteral("Updates left new versions of these settings files beside mine: %1. Which "
                                         "ones matter, and what should I do?").arg(files.join(QStringLiteral(", ")))});
    }

    // Crashes this week, by app.
    {
        const QJsonDocument doc = QJsonDocument::fromJson(found.value(QStringLiteral("crashes")).join(QLatin1Char('\n')).toUtf8());
        QMap<QString, int> byApp;
        // Which apps crashed in each minute, to tell crashes that came
        // together, as at a sign-out, from ones that came alone.
        QMap<qint64, QSet<QString>> byMinute;
        QList<std::pair<qint64, QString>> each;
        int total = 0;
        for (const QJsonValue &v : doc.array()) {
            const QString exe = v.toObject().value(QStringLiteral("exe")).toString().section(QLatin1Char('/'), -1);
            if (exe.isEmpty())
                continue;
            ++byApp[exe];
            ++total;
            const qint64 minute = qint64(v.toObject().value(QStringLiteral("time")).toDouble() / 60e6);
            byMinute[minute].insert(exe);
            each.append({minute, exe});
        }
        const int together = int(std::count_if(each.cbegin(), each.cend(), [&byMinute](const auto &e) {
            return byMinute.value(e.first).size() > 1;
        }));
        if (doc.isArray()) {
            if (total == 0) {
                items.append({QStringLiteral("crashes"), QStringLiteral("Crashes"), QStringLiteral("good"),
                              QStringLiteral("Nothing crashed this week."), {}, {}, {}});
            } else {
                QList<std::pair<QString, int>> apps(byApp.keyValueBegin(), byApp.keyValueEnd());
                std::sort(apps.begin(), apps.end(), [](const auto &a, const auto &b) { return a.second > b.second; });
                QStringList names;
                for (qsizetype i = 0; i < std::min<qsizetype>(3, apps.size()); ++i)
                    names.append(apps.at(i).second > 1 ? QStringLiteral("%1 (%2)").arg(apps.at(i).first).arg(apps.at(i).second)
                                                       : apps.at(i).first);
                items.append({QStringLiteral("crashes"), QStringLiteral("Crashes"),
                              total >= 3 ? QStringLiteral("warn") : QStringLiteral("good"),
                              total == 1 ? QStringLiteral("1 this week: %1.").arg(names.join(QStringLiteral(", ")))
                                         : QStringLiteral("%1 this week: %2.").arg(total).arg(names.join(QStringLiteral(", "))),
                              total >= 3 ? QStringLiteral("ask") : QString(),
                              total >= 3 ? QStringLiteral("Ask about it") : QString(),
                              crashQuestion(apps, total, together)});
            }
        }
    }

    // Old copies of packages, when they take a lot.
    {
        const qint64 kb = found.value(QStringLiteral("pkgcache")).value(0).toLongLong();
        if (kb > 5LL * 1024 * 1024)
            items.append({QStringLiteral("pkgcache"), QStringLiteral("Old copies"), QStringLiteral("warn"),
                          QStringLiteral("Copies of past updates take %1.").arg(size(kb)), QStringLiteral("space"),
                          QStringLiteral("Free up space"), {}});
    }

    // A new kernel installed under the one running: the last update finishes
    // when the computer starts again.
    {
        const QStringList kernel = found.value(QStringLiteral("kernel"));
        if (kernel.size() >= 2 && kernel.at(1) == QLatin1String("missing"))
            items.append({QStringLiteral("restart"), QStringLiteral("Restart"), QStringLiteral("warn"),
                          QStringLiteral("A restart finishes the last update. Until then, some devices and apps may not work."),
                          QStringLiteral("restart"), QStringLiteral("Restart"), {}});
    }

    // The package manager's own list of what is installed.
    {
        if (found.value(QStringLiteral("lock")).contains(QStringLiteral("stuck"))) {
            items.append({QStringLiteral("packages"), QStringLiteral("Installing"), QStringLiteral("bad"),
                          QStringLiteral("An update or install was cut off, so nothing else can install until it's put right."),
                          QStringLiteral("ask"), QStringLiteral("Ask about it"),
                          QStringLiteral("The package manager on my computer is locked (/var/lib/pacman/db.lck is there) "
                                         "but nothing is installing. Was an update cut off, and how do I put it right "
                                         "safely?")});
        } else if (const QStringList errors = found.value(QStringLiteral("packages")); !errors.isEmpty()) {
            items.append({QStringLiteral("packages"), QStringLiteral("Installed apps"), QStringLiteral("bad"),
                          errors.size() == 1 ? QStringLiteral("The list of what's installed has a problem.")
                                             : QStringLiteral("The list of what's installed has %1 problems.").arg(errors.size()),
                          QStringLiteral("ask"), QStringLiteral("Ask about it"),
                          QStringLiteral("Checking my computer's list of installed packages (pacman -Dk) found this:\n%1\n"
                                         "What does it mean, and how do I fix it safely?").arg(errors.join(QLatin1Char('\n')))});
        }
    }

    // Downloads that sat unopened for three months, when they take a lot. They
    // are the person's own files, so the card only opens the folder: moving
    // them to the Trash would free nothing until it is emptied, and deleting
    // them can't be undone.
    {
        int count = 0;
        qint64 bytes = 0;
        for (const QString &line : found.value(QStringLiteral("downloads"))) {
            bool ok = false;
            const qint64 b = line.toLongLong(&ok);
            if (ok) {
                bytes += b;
                ++count;
            }
        }
        if (bytes >= 1024LL * 1024 * 1024)
            items.append({QStringLiteral("downloads"), QStringLiteral("Old downloads"), QStringLiteral("warn"),
                          QStringLiteral("%1 files you downloaded over three months ago and haven't opened since take %2.")
                              .arg(count).arg(size(bytes / 1024)),
                          QStringLiteral("downloads"), QStringLiteral("Show them"), {}});
    }

    // Problems first, then what's worth a look, then what's good.
    const auto rank = [](const Item &i) {
        return i.state == QLatin1String("bad") ? 0 : i.state == QLatin1String("warn") ? 1 : 2;
    };
    std::stable_sort(items.begin(), items.end(), [&rank](const Item &a, const Item &b) { return rank(a) < rank(b); });
    return items;
}

QString HealthCheck::defaultLookScript()
{
    return LookScript;
}

bool HealthCheck::lookFinished(const QString &output)
{
    return sections(output).contains(QStringLiteral("end"));
}

QString HealthCheck::report(const QList<Item> &items)
{
    const int attention = int(std::count_if(items.cbegin(), items.cend(), [](const Item &i) { return i.state != QLatin1String("good"); }));
    QStringList lines{attention == 0 ? QStringLiteral("Health check: nothing needs a look.")
                      : attention == 1 ? QStringLiteral("Health check: 1 thing needs a look.")
                                       : QStringLiteral("Health check: %1 things need a look.").arg(attention)};
    for (const Item &item : items) {
        const QString state = item.state == QLatin1String("bad")    ? QStringLiteral("problem")
                            : item.state == QLatin1String("warn")   ? QStringLiteral("look")
                                                                    : QStringLiteral("good");
        lines.append(QStringLiteral("%1  %2: %3").arg(state.leftJustified(7), item.title, item.detail));
    }
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

QString HealthCheck::facts(const QList<Item> &items)
{
    QString out;
    for (const Item &item : items) {
        const QString state = item.state == QLatin1String("bad")    ? QStringLiteral("problem")
                            : item.state == QLatin1String("warn")   ? QStringLiteral("look")
                                                                    : QStringLiteral("good");
        // A tab or a line break inside the words would split the line.
        const auto plain = [](QString text) { return text.replace(QLatin1Char('\t'), QLatin1Char(' ')).replace(QLatin1Char('\n'), QLatin1Char(' ')); };
        out += state + QLatin1Char('\t') + plain(item.id) + QLatin1Char('\t') + plain(item.title) + QLatin1Char('\t')
            + plain(item.detail) + QLatin1Char('\n');
    }
    return out;
}

QVariantList HealthCheck::itemList() const
{
    QVariantList list;
    for (const Item &item : m_items)
        list.append(item.toMap());
    return list;
}

int HealthCheck::attention() const
{
    // Before this run's own look, what the last one found.
    if (m_items.isEmpty())
        return std::max(0, m_lastAttention);
    return int(std::count_if(m_items.cbegin(), m_items.cend(), [](const Item &i) { return i.state != QLatin1String("good"); }));
}

QString HealthCheck::lastChecked() const
{
    if (!m_checked.isValid())
        return QString();
    const qint64 ago = m_checked.daysTo(QDateTime::currentDateTime());
    return ago <= 0 ? QStringLiteral("Today") : ago == 1 ? QStringLiteral("Yesterday") : QStringLiteral("%1 days ago").arg(ago);
}

bool HealthCheck::watched() const
{
    QSettings core(m_coreSettings, QSettings::IniFormat);
    return core.value(QStringLiteral("General/Enabled"), true).toBool()
        && core.value(QStringLiteral("Health/Enabled"), true).toBool();
}

QString HealthCheck::helpText(const QString &question) const
{
    return helpText(question, aboutThisComputer());
}

QString HealthCheck::helpText(const QString &question, const QString &computer)
{
    if (computer.isEmpty())
        return question;
    return question + QStringLiteral("\n\nAbout my computer:\n") + computer;
}

QString HealthCheck::aboutThisComputer()
{
    const auto line = [](const QString &path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()).trimmed() : QString();
    };
    QStringList lines;
    const QString model = (line(QStringLiteral("/sys/class/dmi/id/sys_vendor")) + QLatin1Char(' ')
                           + line(QStringLiteral("/sys/class/dmi/id/product_name"))).trimmed();
    if (!model.isEmpty())
        lines.append(QStringLiteral("Computer: %1").arg(model));
    lines.append(QStringLiteral("System: %1").arg(QSysInfo::prettyProductName()));
    lines.append(QStringLiteral("Linux kernel: %1").arg(QSysInfo::kernelVersion()));
    // "plasmashell 6.8.0", where Plasma is installed.
    QProcess plasma;
    plasma.start(QStringLiteral("plasmashell"), {QStringLiteral("--version")});
    if (plasma.waitForFinished(2000) && plasma.exitStatus() == QProcess::NormalExit && plasma.exitCode() == 0) {
        const QString version = QString::fromUtf8(plasma.readAllStandardOutput()).trimmed().section(QLatin1Char(' '), -1);
        if (!version.isEmpty())
            lines.append(QStringLiteral("KDE Plasma: %1").arg(version));
    } else {
        plasma.kill();
    }
    return lines.join(QLatin1Char('\n'));
}

void HealthCheck::openDownloads()
{
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (!folder.isEmpty())
        QProcess::startDetached(QStringLiteral("xdg-open"), {folder});
}

void HealthCheck::check()
{
    if (busy())
        return;
    look(true);
}

void HealthCheck::checkIfDue()
{
    if (busy() || !stage().isEmpty())
        return;
    if (m_checked.isValid() && m_checked.secsTo(QDateTime::currentDateTime()) < 20 * 60 * 60)
        return;
    look(false);
}

void HealthCheck::look(bool open)
{
    clearProgress();
    m_quiet = !open;
    if (open)
        setStage(QStringLiteral("looking"));
    run(asPerson(m_lookScript), 60000, [this](int, const QString &output) {
        m_items = readLook(output);
        m_checked = QDateTime::currentDateTime();
        m_lastAttention = attention();
        QSettings settings(Organization, Settings);
        settings.setValue(QStringLiteral("health/checked"), m_checked);
        settings.setValue(QStringLiteral("health/attention"), m_lastAttention);
        settings.setValue(QStringLiteral("health/items"), itemList());
        qInfo().noquote() << QStringLiteral("%1: the health check found %2 to look at").arg(productName()).arg(attention());
        if (!m_quiet)
            setStage(QStringLiteral("done"));
        Q_EMIT changed();
    });
}

} // namespace splitrock
