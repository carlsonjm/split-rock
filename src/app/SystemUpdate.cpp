// SPDX-License-Identifier: GPL-2.0-or-later
#include "app/SystemUpdate.h"

#include "app/Product.h"

#include <QDebug>
#include <QRegularExpression>
#include <QVariantList>
#include <QXmlStreamReader>

#include <algorithm>
#include <utility>

namespace splitrock {

namespace {

// Run as the person: each section starts with a line "@@ name" that readLook
// reads it by. Nothing here changes anything; checkupdates fetches the package
// lists into a folder of its own, never the system's, the one Core's own look
// fetches into (core::updatesListsFolder), so after Core has looked only what
// changed since is fetched.
// The download section asks pacman what the update would fetch, from the
// lists checkupdates fetched, without root and without fetching anything.
const QString LookScript = QStringLiteral(R"SH(echo "@@ system"
export CHECKUPDATES_DB="${XDG_CACHE_HOME:-$HOME/.cache}/split-rock/checkup-db"
if command -v checkupdates >/dev/null 2>&1; then
    checkupdates 2>/dev/null
    echo "@@ download"
    [ -d "$CHECKUPDATES_DB/sync" ] && pacman -Sup --dbpath "$CHECKUPDATES_DB" --logfile /dev/null --noconfirm \
        --print-format '%n %v %s' 2>/dev/null
else
    echo "@@ nochecker"
fi
echo "@@ community"
if command -v paru >/dev/null 2>&1; then paru -Qua 2>/dev/null; elif command -v yay >/dev/null 2>&1; then yay -Qua 2>/dev/null; fi
echo "@@ apps"
command -v flatpak >/dev/null 2>&1 && flatpak remote-ls --updates --columns=application 2>/dev/null
echo "@@ free"
df -Pk / 2>/dev/null | awk 'NR==2 {print $4}'
echo "@@ power"
for d in /sys/class/power_supply/*; do
    [ -r "$d/type" ] || continue
    case "$(cat "$d/type")" in
        Mains) echo "ac $(cat "$d/online" 2>/dev/null)" ;;
        Battery) [ "$(cat "$d/scope" 2>/dev/null)" = Device ] || echo "battery $(cat "$d/capacity" 2>/dev/null)" ;;
    esac
done
echo "@@ snapshots"
command -v snapper >/dev/null 2>&1 && [ -f /etc/snapper/configs/root ] && echo yes
ls /usr/share/libalpm/hooks 2>/dev/null | grep -q snap-pac && echo snap-pac
echo "@@ busy"
[ -e /var/lib/pacman/db.lck ] && echo locked
echo "@@ shuffle"
pacman -Q shuffle-plasma 2>/dev/null
for d in "${XDG_DATA_HOME:-$HOME/.local/share}" /usr/share; do
    [ -e "$d/plasma/plasmoids/co.goodinput.shuffle.bottomsurface" ] && echo "shuffle $d"
done
echo "@@ lastupdate"
grep -a "starting full system upgrade" /var/log/pacman.log 2>/dev/null | tail -n 1
echo "@@ news"
command -v curl >/dev/null 2>&1 && curl -fsS --max-time 10 https://archlinux.org/feeds/news/ 2>/dev/null
echo
echo "@@ end"
)SH");

// The look again, from the lists the last fetch left, by Core's look or this
// card's, so it takes a moment and fetches nothing: pacman reads what is
// newer in those lists, Flatpak its own cached ones. No community apps and
// no news, which only the network has; the fresh look after it brings them.
// "@@ stale" when no fetch has left lists yet.
QString quickLookOf(QString script)
{
    script.replace(QStringLiteral("    checkupdates 2>/dev/null\n"),
                   QStringLiteral("    pacman -Qu --dbpath \"$CHECKUPDATES_DB\" --logfile /dev/null 2>/dev/null\n"));
    script.replace(QStringLiteral("if command -v paru >/dev/null 2>&1; then paru -Qua 2>/dev/null; elif command -v yay "
                                  ">/dev/null 2>&1; then yay -Qua 2>/dev/null; fi\n"),
                   QString());
    script.replace(QStringLiteral("flatpak remote-ls --updates "), QStringLiteral("flatpak remote-ls --updates --cached "));
    script.replace(QStringLiteral("command -v curl >/dev/null 2>&1 && curl -fsS --max-time 10 https://archlinux.org/feeds/news/ "
                                  "2>/dev/null\n"),
                   QString());
    const qsizetype lists = script.indexOf(QStringLiteral("if command -v checkupdates"));
    script.insert(lists, QStringLiteral("[ -d \"$CHECKUPDATES_DB/sync\" ] || { echo \"@@ stale\"; exit 0; }\n"));
    return script;
}

// Run as the person: their Flatpak apps.
const QString AppsScript = QStringLiteral(R"SH(flatpak update -y --noninteractive 2>&1
)SH");

// Run as the person after the update: whether the running kernel's modules
// are gone, settings files the update left beside the person's own, and
// services that failed.
const QString AfterScript = QStringLiteral(R"SH(echo "@@ kernel"
[ -d "/usr/lib/modules/$(uname -r)" ] || echo restart
echo "@@ settings"
find /etc -name '*.pacnew' 2>/dev/null
echo "@@ failed"
systemctl --failed --no-legend --plain 2>/dev/null | awk '{print $1}'
echo "@@ end"
)SH");

// Packages a person notices, in their words, whether they take effect only
// after a restart, and, for the big pieces of the desktop, what the list at
// the top of the card calls them and where. Tried in order; the first that
// fits names it.
struct Known {
    QRegularExpression name;
    QString words;
    bool restart;
    QString piece = QString();
    int pieceOrder = 0;
};

const QList<Known> &known()
{
    // The kernel, not its headers, documentation or the firmware files.
    static const QList<Known> list{
        {QRegularExpression(QStringLiteral("^linux(?!-firmware|-api-headers)(-[a-z0-9]+)*(?<!-headers)(?<!-docs)$")),
         QStringLiteral("A new Linux kernel"), true, QStringLiteral("Linux kernel"), 1},
        {QRegularExpression(QStringLiteral("^plasma-workspace$")), QStringLiteral("KDE Plasma"), true, QStringLiteral("KDE Plasma"), 2},
        {QRegularExpression(QStringLiteral("^(nvidia|nvidia-open|nvidia-dkms|nvidia-utils)$")), QStringLiteral("NVIDIA graphics drivers"), true,
         QStringLiteral("NVIDIA graphics drivers"), 5},
        {QRegularExpression(QStringLiteral("^mesa$")), QStringLiteral("Graphics drivers"), true, QStringLiteral("Graphics drivers"), 5},
        {QRegularExpression(QStringLiteral("^linux-firmware$")), QStringLiteral("Device firmware files"), true, QStringLiteral("Device firmware"), 8},
        {QRegularExpression(QStringLiteral("^systemd$")), QStringLiteral("System services"), true, QStringLiteral("System services"), 6},
        {QRegularExpression(QStringLiteral("^glibc$")), QStringLiteral("The core system library"), true, QStringLiteral("The core system library"), 7},
        {QRegularExpression(QStringLiteral("^firefox$")), QStringLiteral("Firefox"), false},
        {QRegularExpression(QStringLiteral("^chromium$")), QStringLiteral("Chromium"), false},
        {QRegularExpression(QStringLiteral("^thunderbird$")), QStringLiteral("Thunderbird"), false},
        {QRegularExpression(QStringLiteral("^libreoffice-(fresh|still)$")), QStringLiteral("LibreOffice"), false},
        {QRegularExpression(QStringLiteral("^steam$")), QStringLiteral("Steam"), false},
        {QRegularExpression(QStringLiteral("^shuffle-plasma$")), QStringLiteral("Shuffle"), true, QStringLiteral("Shuffle"), 9},
        {QRegularExpression(QStringLiteral("^kwin$")), QStringLiteral("KWin"), true, QStringLiteral("KWin, which draws the windows"), 3},
        {QRegularExpression(QStringLiteral("^qt6-base$")), QStringLiteral("Qt"), true, QStringLiteral("Qt, which apps are drawn with"), 4},
    };
    return list;
}

// The first Known that fits name, or -1.
qsizetype knownIndex(const QString &name)
{
    for (qsizetype i = 0; i < known().size(); ++i) {
        if (known().at(i).name.match(name).hasMatch())
            return i;
    }
    return -1;
}

// A version as a person reads it: "1:25.2.4-1" to "25.2.4". The package's
// own release number stays when only it changed, so the two never read the
// same.
std::pair<QString, QString> readable(const QString &from, const QString &to)
{
    const auto plain = [](QString v) {
        v = v.section(QLatin1Char(':'), -1);
        return v.section(QLatin1Char('-'), 0, 0);
    };
    if (!from.isEmpty() && plain(from) == plain(to))
        return {from.section(QLatin1Char(':'), -1), to.section(QLatin1Char(':'), -1)};
    return {from.isEmpty() ? QString() : plain(from), plain(to)};
}

// "6.7.5-1" to "6.7".
QString minorOf(const QString &version)
{
    const QStringList parts = version.section(QLatin1Char('-'), 0, 0).split(QLatin1Char('.'));
    return parts.size() >= 2 ? parts.at(0) + QLatin1Char('.') + parts.at(1) : parts.value(0);
}

// pacman.log's "[2026-10-01T10:00:00-0500]".
QDateTime logTime(const QString &line)
{
    static const QRegularExpression stamp(QStringLiteral(R"(^\[(\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d)([+-]\d\d)(\d\d)\])"));
    const QRegularExpressionMatch m = stamp.match(line);
    if (!m.hasMatch())
        return {};
    return QDateTime::fromString(m.captured(1) + m.captured(2) + QLatin1Char(':') + m.captured(3), Qt::ISODate);
}

QVariantList newsList(const QList<SystemUpdate::News> &news)
{
    QVariantList list;
    for (const SystemUpdate::News &n : news)
        list.append(QVariantMap{{QStringLiteral("title"), n.title},
                                {QStringLiteral("link"), n.link},
                                {QStringLiteral("date"), n.posted.toString(QStringLiteral("d MMMM"))}});
    return list;
}

} // namespace

QStringList SystemUpdate::Plan::fingerprint() const
{
    QStringList found;
    for (const Package &p : list)
        found.append(p.name + QLatin1Char(' ') + p.to);
    for (const QString &app : appIds)
        found.append(QStringLiteral("app ") + app);
    found.sort();
    return found;
}

QVariantMap SystemUpdate::Plan::toMap() const
{
    QVariantList pieces;
    QVariantList rows;
    for (const Package &p : list) {
        const auto [from, to] = readable(p.from, p.to);
        const QVariantMap row{{QStringLiteral("name"), p.name},
                              {QStringLiteral("from"), from},
                              {QStringLiteral("to"), to},
                              {QStringLiteral("size"), p.size > 0 ? Chore::size(std::max<qint64>(1, p.size / 1024)) : QString()},
                              {QStringLiteral("piece"), p.piece},
                              {QStringLiteral("step"), p.step}};
        if (!p.piece.isEmpty())
            pieces.append(row);
        rows.append(row);
    }
    return {{QStringLiteral("system"), system},
            {QStringLiteral("list"), rows},
            {QStringLiteral("pieces"), pieces},
            {QStringLiteral("appIds"), appIds},
            {QStringLiteral("download"), download > 0 ? Chore::size(std::max<qint64>(1, download / 1024)) : QString()},
            {QStringLiteral("desktopRisk"), desktopRisk},
            {QStringLiteral("changed"), changed},
            {QStringLiteral("community"), community},
            {QStringLiteral("apps"), apps},
            {QStringLiteral("highlights"), highlights},
            {QStringLiteral("packages"), packages.join(QLatin1Char('\n'))},
            {QStringLiteral("restartLikely"), restartLikely},
            {QStringLiteral("snapshots"), snapshots || snapPac},
            {QStringLiteral("news"), newsList(news)},
            {QStringLiteral("warnings"), warnings},
            {QStringLiteral("blockers"), blockers},
            {QStringLiteral("nothing"), nothing()}};
}

QVariantMap SystemUpdate::Result::toMap() const
{
    return {{QStringLiteral("ok"), ok},
            {QStringLiteral("why"), why},
            {QStringLiteral("installed"), installed},
            {QStringLiteral("restorePoint"), pre},
            {QStringLiteral("restart"), restart},
            {QStringLiteral("appsUpdated"), appsUpdated},
            {QStringLiteral("settingsFiles"), settingsFiles},
            {QStringLiteral("failedServices"), failedServices}};
}

SystemUpdate::SystemUpdate(QObject *parent)
    : Chore(parent)
    , m_lookScript(LookScript)
    , m_quickScript(quickLookOf(LookScript))
    , m_appsScript(AppsScript)
    , m_afterScript(AfterScript)
{
}

QString SystemUpdate::quickLook()
{
    return quickLookOf(LookScript);
}

bool SystemUpdate::busy() const
{
    return stage() == QLatin1String("looking") || stage() == QLatin1String("rechecking")
        || stage() == QLatin1String("updating");
}

QList<SystemUpdate::News> SystemUpdate::readNews(const QString &rss)
{
    QList<News> list;
    QXmlStreamReader xml(rss);
    News item;
    bool inItem = false;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            if (xml.name() == QLatin1String("item")) {
                inItem = true;
                item = News();
            } else if (inItem && xml.name() == QLatin1String("title")) {
                item.title = xml.readElementText().simplified();
            } else if (inItem && xml.name() == QLatin1String("link")) {
                item.link = xml.readElementText().trimmed();
            } else if (inItem && xml.name() == QLatin1String("pubDate")) {
                item.posted = QDateTime::fromString(xml.readElementText().trimmed(), Qt::RFC2822Date);
            }
        } else if (xml.isEndElement() && xml.name() == QLatin1String("item")) {
            inItem = false;
            if (!item.title.isEmpty() && item.posted.isValid())
                list.append(item);
        }
    }
    std::sort(list.begin(), list.end(), [](const News &a, const News &b) { return a.posted > b.posted; });
    return list;
}

SystemUpdate::Plan SystemUpdate::readLook(const QString &output, const QDateTime &now)
{
    QMap<QString, QStringList> sections;
    QString section;
    QString news;
    for (const QString &line : output.split(QLatin1Char('\n'))) {
        if (line.startsWith(QLatin1String("@@ "))) {
            section = line.mid(3).trimmed();
            sections[section];
            continue;
        }
        if (section == QLatin1String("news"))
            news += line + QLatin1Char('\n');
        else if (!line.trimmed().isEmpty())
            sections[section].append(line.trimmed());
    }

    Plan plan;
    static const QRegularExpression update(QStringLiteral(R"(^(\S+)\s+(\S+)\s+->\s+(\S+))"));

    // The system's own packages, and the ones a person notices, most
    // noticeable first.
    QList<std::pair<qsizetype, QString>> noticed;
    if (sections.contains(QStringLiteral("nochecker"))) {
        plan.system = -1;
    } else {
        for (const QString &line : sections.value(QStringLiteral("system"))) {
            const QRegularExpressionMatch m = update.match(line);
            if (!m.hasMatch())
                continue;
            ++plan.system;
            const QString name = m.captured(1);
            plan.packages.append(QStringLiteral("%1  %2 → %3").arg(name, m.captured(2), m.captured(3)));
            Package package;
            package.name = name;
            package.from = m.captured(2);
            package.to = m.captured(3);
            package.step = minorOf(package.from) != minorOf(package.to);
            if (const qsizetype i = knownIndex(name); i >= 0)
                package.piece = known().at(i).piece;
            plan.list.append(package);
            for (qsizetype i = 0; i < known().size(); ++i) {
                const Known &k = known().at(i);
                if (!k.name.match(name).hasMatch())
                    continue;
                plan.restartLikely = plan.restartLikely || k.restart;
                QString words = k.words;
                if (name == QLatin1String("plasma-workspace")) {
                    plan.plasmaFrom = minorOf(m.captured(2));
                    plan.plasmaTo = minorOf(m.captured(3));
                    words = QStringLiteral("KDE Plasma %1").arg(m.captured(3).section(QLatin1Char('-'), 0, 0));
                } else if (name == QLatin1String("firefox")) {
                    words = QStringLiteral("Firefox %1").arg(m.captured(3).section(QLatin1Char('.'), 0, 0));
                }
                if (std::none_of(noticed.cbegin(), noticed.cend(), [&words](const auto &n) { return n.second == words; }))
                    noticed.append({i, words});
                break;
            }
        }
    }
    // What each costs to download, and the packages the update brings new,
    // such as a new dependency, which checkupdates doesn't list.
    static const QRegularExpression fetched(QStringLiteral(R"(^(\S+)\s+(\S+)\s+(\d+)$)"));
    if (sections.contains(QStringLiteral("download")) && plan.system > 0) {
        qint64 total = 0;
        bool any = false;
        for (const QString &line : sections.value(QStringLiteral("download"))) {
            const QRegularExpressionMatch m = fetched.match(line);
            if (!m.hasMatch())
                continue;
            any = true;
            const qint64 size = m.captured(3).toLongLong();
            total += size;
            auto it = std::find_if(plan.list.begin(), plan.list.end(), [&m](const Package &p) { return p.name == m.captured(1); });
            if (it != plan.list.end()) {
                it->size = size;
                continue;
            }
            Package package;
            package.name = m.captured(1);
            package.to = m.captured(2);
            package.size = size;
            if (const qsizetype i = knownIndex(package.name); i >= 0)
                package.piece = known().at(i).piece;
            plan.list.append(package);
        }
        if (any)
            plan.download = total;
    }
    // The big pieces first, then by name.
    const auto order = [](const Package &p) {
        const qsizetype i = knownIndex(p.name);
        return i >= 0 && !p.piece.isEmpty() ? known().at(i).pieceOrder : 100;
    };
    std::stable_sort(plan.list.begin(), plan.list.end(), [&order](const Package &a, const Package &b) {
        const int oa = order(a);
        const int ob = order(b);
        return oa != ob ? oa < ob : a.name < b.name;
    });

    std::stable_sort(noticed.begin(), noticed.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    for (const auto &n : std::as_const(noticed)) {
        if (plan.highlights.size() < 4)
            plan.highlights.append(n.second);
    }
    for (const QString &line : sections.value(QStringLiteral("community"))) {
        if (update.match(line).hasMatch() && !line.contains(QLatin1String("[ignored]")))
            ++plan.community;
    }
    plan.appIds = sections.value(QStringLiteral("apps"));
    plan.apps = plan.appIds.size();

    // Whether the update can start, and what to read first.
    const QStringList snapshots = sections.value(QStringLiteral("snapshots"));
    plan.snapshots = snapshots.contains(QStringLiteral("yes"));
    plan.snapPac = snapshots.contains(QStringLiteral("snap-pac"));
    if (!sections.value(QStringLiteral("busy")).isEmpty())
        plan.blockers.append(QStringLiteral("Something else is installing right now. Try again when it has finished."));

    bool ok = false;
    const qint64 freeKb = sections.value(QStringLiteral("free")).value(0).toLongLong(&ok);
    if (ok) {
        const double gb = freeKb / 1024.0 / 1024.0;
        if (gb < 2)
            plan.blockers.append(QStringLiteral("Only %1 GB is free, and an update needs room to unpack. "
                                                "Free up some space first.").arg(gb, 0, 'f', 1));
        else if (gb < 5)
            plan.warnings.append(QStringLiteral("Space is getting low: %1 GB free.").arg(gb, 0, 'f', 1));
    }

    bool onMains = false;
    bool hasMains = false;
    int battery = -1;
    for (const QString &line : sections.value(QStringLiteral("power"))) {
        if (line.startsWith(QLatin1String("ac "))) {
            hasMains = true;
            onMains = onMains || line.mid(3).trimmed() == QLatin1String("1");
        } else if (line.startsWith(QLatin1String("battery "))) {
            const int level = line.mid(8).trimmed().toInt(&ok);
            if (ok)
                battery = battery < 0 ? level : std::min(battery, level);
        }
    }
    if (hasMains && !onMains && battery >= 0) {
        if (battery < 50)
            plan.blockers.append(QStringLiteral("Plug in first: the battery is at %1%, and an update that stops "
                                                "halfway can leave the computer unable to start.").arg(battery));
        else
            plan.warnings.append(QStringLiteral("Plugging in is safer. The battery is at %1%.").arg(battery));
    }

    if (!plan.snapshots && !plan.snapPac)
        plan.warnings.append(QStringLiteral("This computer doesn't take restore points, so this update can't be "
                                            "gone back from."));

    // Shuffle is installed as a package, or from its own folder: either way
    // its panels and Cards are built for the Plasma and KWin there now.
    const bool shuffle = !sections.value(QStringLiteral("shuffle")).isEmpty();
    const auto kwin = std::find_if(plan.list.cbegin(), plan.list.cend(), [](const Package &p) {
        return p.name == QLatin1String("kwin") && !p.from.isEmpty();
    });
    if (shuffle && !plan.plasmaTo.isEmpty() && plan.plasmaTo != plan.plasmaFrom) {
        plan.desktopRisk = true;
        plan.warnings.append(QStringLiteral("This brings KDE Plasma %1. Check Shuffle has its update for Plasma %1 "
                                            "first, or parts of Shuffle may not start after the restart.")
                                 .arg(plan.plasmaTo));
    } else if (shuffle && kwin != plan.list.cend() && kwin->step) {
        plan.desktopRisk = true;
        plan.warnings.append(QStringLiteral("This brings KWin %1, which draws the windows. Check Shuffle has its "
                                            "update for it first, or Shuffle's Cards may not start after the restart.")
                                 .arg(minorOf(kwin->to.section(QLatin1Char(':'), -1))));
    }

    // News Arch posted since the last update, or in the last two weeks when
    // the last update isn't known.
    QDateTime since = logTime(sections.value(QStringLiteral("lastupdate")).value(0));
    if (!since.isValid())
        since = now.addDays(-14);
    for (const News &n : readNews(news)) {
        if (n.posted > since)
            plan.news.append(n);
    }
    if (!plan.news.isEmpty())
        plan.warnings.prepend(plan.news.size() == 1
                                  ? QStringLiteral("Arch posted a notice since your last update. Read it first: "
                                                   "some updates need a step by hand.")
                                  : QStringLiteral("Arch posted %1 notices since your last update. Read them first: "
                                                   "some updates need a step by hand.").arg(plan.news.size()));
    return plan;
}

void SystemUpdate::readAfter(const QString &output, Result &result)
{
    QString section;
    for (const QString &raw : output.split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line.startsWith(QLatin1String("@@ "))) {
            section = line.mid(3);
            continue;
        }
        if (line.isEmpty())
            continue;
        if (section == QLatin1String("kernel") && line == QLatin1String("restart"))
            result.restart = true;
        else if (section == QLatin1String("settings"))
            result.settingsFiles.append(line);
        else if (section == QLatin1String("failed"))
            result.failedServices.append(line);
    }
}

void SystemUpdate::check()
{
    if (busy() || m_refreshing)
        return;
    m_plan = Plan();
    m_result = Result();
    clearProgress();
    setStage(QStringLiteral("looking"));
    if (m_quickScript.isEmpty()) {
        look();
        return;
    }
    // The list the last fetch left shows at once, Core's every few hours or
    // this card's; the fresh look follows and puts it right in place.
    run(asPerson(m_quickScript), 15000, [this](int, const QString &output) {
        const Plan quick = readLook(output);
        if (output.startsWith(QLatin1String("@@ stale")) || quick.system < 0) {
            look();
            return;
        }
        m_plan = quick;
        setStage(m_plan.nothing() ? QStringLiteral("nothing") : QStringLiteral("ready"));
        Q_EMIT changed();
        refresh();
    });
}

void SystemUpdate::refresh()
{
    m_refreshing = true;
    run(asPerson(m_lookScript), 90000, [this](int, const QString &output) {
        m_refreshing = false;
        Plan now = readLook(output);
        if (stage() == QLatin1String("rechecking") && m_recheckRead) {
            const QStringList read = *m_recheckRead;
            m_recheckRead.reset();
            afterRecheck(read, now);
            return;
        }
        // The person may have read on, to the question before a desktop
        // step: it stands while the list is the same.
        if (stage() == QLatin1String("confirming") && now.fingerprint() == m_plan.fingerprint() && now.blockers.isEmpty())
            return;
        if (stage() != QLatin1String("ready") && stage() != QLatin1String("nothing") && stage() != QLatin1String("confirming"))
            return;
        m_plan = now;
        setStage(m_plan.nothing() ? QStringLiteral("nothing") : QStringLiteral("ready"));
        setWaiting(m_plan);
        Q_EMIT changed();
    });
}

void SystemUpdate::look()
{
    // checkupdates fetches the lists, so it may take a while on a slow line.
    run(asPerson(m_lookScript), 90000, [this](int, const QString &output) {
        m_plan = readLook(output);
        qInfo().noquote() << QStringLiteral("%1: %2 system updates, %3 apps, %4 community waiting")
                                 .arg(productName()).arg(m_plan.system).arg(m_plan.apps).arg(m_plan.community);
        setStage(m_plan.nothing() ? QStringLiteral("nothing") : QStringLiteral("ready"));
        setWaiting(m_plan);
        Q_EMIT changed();
    });
}

void SystemUpdate::peek()
{
    if (m_peeking || busy() || !stage().isEmpty())
        return;
    m_peeking = true;
    run(asPerson(m_lookScript), 90000, [this](int, const QString &output) {
        m_peeking = false;
        setWaiting(readLook(output));
    });
}

void SystemUpdate::setWaiting(const Plan &plan)
{
    const int waiting = plan.system < 0 ? -1 : plan.system + plan.apps;
    if (waiting == m_waiting)
        return;
    m_waiting = waiting;
    Q_EMIT waitingChanged();
}

void SystemUpdate::start()
{
    if (stage() != QLatin1String("ready") || !m_plan.blockers.isEmpty())
        return;
    if (m_plan.desktopRisk) {
        setStage(QStringLiteral("confirming"));
        return;
    }
    recheck();
}

void SystemUpdate::confirm()
{
    if (stage() == QLatin1String("confirming"))
        recheck();
}

void SystemUpdate::recheck()
{
    // The update installs what is newest when it runs, so the list the person
    // read is checked again first: something new can arrive in between.
    setStage(QStringLiteral("rechecking"));
    const QStringList read = m_plan.fingerprint();
    if (m_refreshing) {
        m_recheckRead = read;
        return;
    }
    run(asPerson(m_lookScript), 90000, [this, read](int, const QString &output) { afterRecheck(read, readLook(output)); });
}

void SystemUpdate::afterRecheck(const QStringList &read, Plan now)
{
    if (now.fingerprint() == read && now.blockers.isEmpty()) {
        update();
        return;
    }
    now.changed = now.fingerprint() != read;
    qInfo().noquote() << QStringLiteral("%1: the update changed since it was read, %2 system updates now")
                             .arg(productName()).arg(now.system);
    m_plan = now;
    setStage(m_plan.nothing() ? QStringLiteral("nothing") : QStringLiteral("ready"));
    setWaiting(m_plan);
    Q_EMIT changed();
}

void SystemUpdate::update()
{
    // Written down, with how to go back, before anything changes.
    m_id = QStringLiteral("update-%1").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    const bool restorePoints = m_plan.snapshots || m_plan.snapPac;
    QString unrecorded;
    if (!writeDown(m_id,
                   m_plan.system > 0 ? QStringLiteral("Updated the computer (%1 packages)").arg(m_plan.system)
                                     : QStringLiteral("Updated the computer"),
                   restorePoints && m_plan.system != 0
                       ? QStringLiteral("Go back to the restore point taken just before, from the Restore points card")
                       : QString(),
                   &unrecorded)) {
        m_result = Result();
        m_result.why = QStringLiteral("Nothing was changed: it couldn't be written in the notebook first. %1").arg(unrecorded);
        finish(QString());
        return;
    }
    clearProgress();
    setProgress(0, std::max(0, m_plan.system));
    setNow(QStringLiteral("Waiting for your password"));
    m_output.clear();
    setStage(QStringLiteral("updating"));

    if (m_plan.system == 0) {
        afterSystem(0, QString());
        return;
    }
    const QStringList program = asAdmin(QStringLiteral("update"),
                                        {productName(), m_snapshots && !m_plan.snapPac ? QStringLiteral("1") : QStringLiteral("0")});
    qInfo().noquote() << QStringLiteral("%1: updating the system").arg(productName());
    // Never stopped by a timer: an update cut off halfway can leave the
    // computer unable to start.
    run(program, -1, [this](int exitCode, const QString &output) { afterSystem(exitCode, output); },
        [this](const QString &line) {
            appendLog(line);
            const Step step = pacmanStep(line, done(), total());
            setProgress(done() + (step.counts ? 1 : 0), step.total > 0 ? step.total : total());
            if (!step.now.isEmpty())
                setNow(step.now);
        });
}

void SystemUpdate::afterSystem(int exitCode, const QString &output)
{
    m_output = output;
    static const QRegularExpression pre(QStringLiteral("split-rock-pre=(\\d+)"));
    static const QRegularExpression post(QStringLiteral("split-rock-post=(\\d+)"));
    if (const QRegularExpressionMatch m = pre.match(output); m.hasMatch())
        m_result.pre = m.captured(1).toInt();
    if (const QRegularExpressionMatch m = post.match(output); m.hasMatch())
        m_result.post = m.captured(1).toInt();
    m_result.installed = done();
    if (exitCode != 0) {
        m_result.ok = false;
        m_result.why = whyFailed(output, exitCode);
        qInfo().noquote() << QStringLiteral("%1: the update failed, exit code %2").arg(productName()).arg(exitCode);
        finish(output);
        return;
    }
    const auto after = [this] {
        setNow(QStringLiteral("Looking over the computer"));
        run(asPerson(m_afterScript), 30000, [this](int, const QString &afterOutput) {
            readAfter(afterOutput, m_result);
            m_result.restart = m_result.restart || (m_plan.restartLikely && m_result.installed > 0);
            m_result.ok = true;
            finish(m_output);
        });
    };
    if (m_plan.apps <= 0) {
        after();
        return;
    }
    setNow(QStringLiteral("Updating your apps"));
    run(asPerson(m_appsScript), 30 * 60000,
        [this, after](int appsExit, const QString &appsOutput) {
            m_result.appsUpdated = appsExit == 0;
            m_output += QLatin1Char('\n') + appsOutput;
            after();
        },
        [this](const QString &line) {
            appendLog(line);
            Q_EMIT progressChanged();
        });
}

void SystemUpdate::finish(const QString &output)
{
    setNow(QString());
    setStage(m_result.ok ? QStringLiteral("done") : QStringLiteral("failed"));
    if (m_result.ok && m_waiting != 0) {
        m_waiting = 0;
        Q_EMIT waitingChanged();
    }
    Q_EMIT changed();
    Q_EMIT finished(m_result, output);
    if (m_id.isEmpty())
        return;
    QString tail = output;
    if (tail.size() > 3000)
        tail = QStringLiteral("...") + tail.right(3000);
    report(std::exchange(m_id, QString()), m_result.ok,
           m_result.ok ? QStringLiteral("Updated the computer") : QStringLiteral("The update didn't finish"),
           QStringLiteral("%1 ran its own Update card, which the person started. %2\n"
                          "Restart needed: %3. Settings files left beside the person's own (.pacnew): %4. "
                          "Services that failed after: %5.\nThe end of the output:\n%6\n\n"
                          "Tell the person in plain words what needs them, if anything. Don't propose the "
                          "update again.")
               .arg(productName(),
                    m_result.ok ? QStringLiteral("It finished, %1 packages installed.").arg(m_result.installed)
                                : QStringLiteral("It failed: %1").arg(m_result.why),
                    m_result.restart ? QStringLiteral("yes") : QStringLiteral("no"),
                    m_result.settingsFiles.isEmpty() ? QStringLiteral("none") : m_result.settingsFiles.join(QStringLiteral(", ")),
                    m_result.failedServices.isEmpty() ? QStringLiteral("none") : m_result.failedServices.join(QStringLiteral(", ")),
                    tail.isEmpty() ? QStringLiteral("(none)") : tail));
}

} // namespace splitrock
