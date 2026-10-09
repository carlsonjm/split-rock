// SPDX-License-Identifier: GPL-2.0-or-later
#include "app/SystemUpdate.h"

#include "app/Product.h"

#include <QDebug>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QXmlStreamReader>

#include <algorithm>

namespace splitrock {

namespace {

const QString Shell = QStringLiteral("/bin/sh");

// Run as the person: each section starts with a line "@@ name" that readLook
// reads it by. Nothing here changes anything; checkupdates fetches the package
// lists into a folder of its own, never the system's.
const QString LookScript = QStringLiteral(R"SH(echo "@@ system"
if command -v checkupdates >/dev/null 2>&1; then checkupdates 2>/dev/null; else echo "@@ nochecker"; fi
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
echo "@@ lastupdate"
grep -a "starting full system upgrade" /var/log/pacman.log 2>/dev/null | tail -n 1
echo "@@ news"
command -v curl >/dev/null 2>&1 && curl -fsS --max-time 10 https://archlinux.org/feeds/news/ 2>/dev/null
echo
echo "@@ end"
)SH");

// Run as the administrator: a restore point before, the update, a restore
// point after, each number printed for Split Rock to keep. The computer is
// kept awake, and the lid's switch held, until it finishes. pacman speaks
// English here so the card can follow it, and writes to a log that Split Rock
// follows rather than to Split Rock itself, so the update carries on to the
// end even if Split Rock stops partway. $1 is the description, $2 whether
// Split Rock takes the restore points itself.
const QString UpdateScript = QStringLiteral(R"SH(export LANG=C LC_ALL=C
trap '' HUP PIPE
log=/var/log/split-rock-update.log
pre=
if [ "$2" = 1 ] && command -v snapper >/dev/null 2>&1 && [ -f /etc/snapper/configs/root ]; then
    pre=$(snapper -c root create --type pre --cleanup-algorithm number --print-number --description "$1") || pre=
fi
[ -n "$pre" ] && echo "split-rock-pre=$pre"
: > "$log"
desc=$1
set -- pacman -Syu --noconfirm --noprogressbar --color never
command -v stdbuf >/dev/null 2>&1 && set -- stdbuf -oL "$@"
if systemd-inhibit --list >/dev/null 2>&1; then
    systemd-inhibit --what=sleep:shutdown:idle:handle-lid-switch --who="$desc" --why="Updating the computer" "$@" >"$log" 2>&1 &
else
    "$@" >"$log" 2>&1 &
fi
pid=$!
tail -n +1 --pid="$pid" -f "$log" 2>/dev/null
wait "$pid"
status=$?
if [ -n "$pre" ]; then
    post=$(snapper -c root create --type post --pre-number "$pre" --cleanup-algorithm number --print-number --description "$desc") \
        && echo "split-rock-post=$post"
fi
exit $status
)SH");

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

// Packages a person notices, in their words, and whether they take effect
// only after a restart. Tried in order; the first that fits names it.
struct Known {
    QRegularExpression name;
    QString words;
    bool restart;
};

const QList<Known> &known()
{
    static const QList<Known> list{
        {QRegularExpression(QStringLiteral("^linux(-[a-z0-9]+)*$")), QStringLiteral("A new Linux kernel"), true},
        {QRegularExpression(QStringLiteral("^plasma-workspace$")), QStringLiteral("KDE Plasma"), true},
        {QRegularExpression(QStringLiteral("^(nvidia|nvidia-open|nvidia-dkms|nvidia-utils)$")), QStringLiteral("NVIDIA graphics drivers"), true},
        {QRegularExpression(QStringLiteral("^mesa$")), QStringLiteral("Graphics drivers"), true},
        {QRegularExpression(QStringLiteral("^linux-firmware$")), QStringLiteral("Device firmware files"), true},
        {QRegularExpression(QStringLiteral("^systemd$")), QStringLiteral("System services"), true},
        {QRegularExpression(QStringLiteral("^glibc$")), QStringLiteral("The core system library"), true},
        {QRegularExpression(QStringLiteral("^firefox$")), QStringLiteral("Firefox"), false},
        {QRegularExpression(QStringLiteral("^chromium$")), QStringLiteral("Chromium"), false},
        {QRegularExpression(QStringLiteral("^thunderbird$")), QStringLiteral("Thunderbird"), false},
        {QRegularExpression(QStringLiteral("^libreoffice-(fresh|still)$")), QStringLiteral("LibreOffice"), false},
        {QRegularExpression(QStringLiteral("^steam$")), QStringLiteral("Steam"), false},
        {QRegularExpression(QStringLiteral("^shuffle-plasma$")), QStringLiteral("Shuffle"), true},
    };
    return list;
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

QVariantMap SystemUpdate::Plan::toMap() const
{
    return {{QStringLiteral("system"), system},
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
    : QObject(parent)
    , m_lookScript(LookScript)
    , m_updateScript(UpdateScript)
    , m_appsScript(AppsScript)
    , m_afterScript(AfterScript)
{
}

SystemUpdate::~SystemUpdate()
{
    // An update keeps going without Split Rock: it writes to its own log, and
    // the administrator's process is out of Split Rock's reach.
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(1000);
    }
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
    std::stable_sort(noticed.begin(), noticed.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    for (const auto &n : std::as_const(noticed)) {
        if (plan.highlights.size() < 4)
            plan.highlights.append(n.second);
    }
    for (const QString &line : sections.value(QStringLiteral("community"))) {
        if (update.match(line).hasMatch() && !line.contains(QLatin1String("[ignored]")))
            ++plan.community;
    }
    plan.apps = sections.value(QStringLiteral("apps")).size();

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

    if (!plan.plasmaTo.isEmpty() && plan.plasmaTo != plan.plasmaFrom
        && !sections.value(QStringLiteral("shuffle")).isEmpty())
        plan.warnings.append(QStringLiteral("This brings KDE Plasma %1. Check Shuffle has its update for Plasma %1 "
                                            "first, or parts of Shuffle may not start after the restart.")
                                 .arg(plan.plasmaTo));

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

SystemUpdate::Step SystemUpdate::readStep(const QString &line, int done, int total)
{
    static const QRegularExpression packages(QStringLiteral(R"(^Packages? \((\d+)\))"));
    static const QRegularExpression working(
        QStringLiteral(R"(^(upgrading|installing|reinstalling|downgrading|removing) (\S+?)\.\.\.)"));
    Step step;
    const QString trimmed = line.trimmed();
    QRegularExpressionMatch m = packages.match(trimmed);
    if (m.hasMatch()) {
        step.total = m.captured(1).toInt();
        step.now = QStringLiteral("Getting ready to update %1 packages").arg(step.total);
        return step;
    }
    m = working.match(trimmed);
    if (m.hasMatch()) {
        step.counts = true;
        const QString verb = m.captured(1) == QLatin1String("removing") ? QStringLiteral("Removing")
                                                                          : QStringLiteral("Installing");
        step.now = total > 0 ? QStringLiteral("%1 %2 of %3: %4").arg(verb).arg(done + 1).arg(total).arg(m.captured(2))
                             : QStringLiteral("%1 %2").arg(verb, m.captured(2));
        return step;
    }
    if (trimmed.startsWith(QLatin1String(":: Synchronizing package databases")))
        step.now = QStringLiteral("Checking what's new");
    else if (trimmed.startsWith(QLatin1String(":: Retrieving packages")))
        step.now = QStringLiteral("Downloading the updates");
    else if (trimmed.startsWith(QLatin1String("checking keyring")) || trimmed.startsWith(QLatin1String("checking package integrity")))
        step.now = QStringLiteral("Checking the updates are genuine");
    else if (trimmed.startsWith(QLatin1String("checking for file conflicts")) || trimmed.startsWith(QLatin1String("checking available disk space")))
        step.now = QStringLiteral("Checking there's room");
    else if (trimmed.startsWith(QLatin1String(":: Running post-transaction hooks")))
        step.now = QStringLiteral("Finishing up");
    return step;
}

QString SystemUpdate::whyFailed(const QString &output, int exitCode)
{
    // pkexec's own: the prompt was dismissed, or not allowed.
    if (exitCode == 126 || exitCode == 127)
        return QStringLiteral("The password prompt was closed, so nothing was changed.");
    if (exitCode == -2)
        return QStringLiteral("The update couldn't start: this computer has no password prompt for it.");
    const QString text = output.toLower();
    if (text.contains(QLatin1String("unable to lock database")))
        return QStringLiteral("Something else was installing, so nothing was changed. Try again when it has finished.");
    if (text.contains(QLatin1String("could not resolve host")) || text.contains(QLatin1String("failed to retrieve some files"))
        || text.contains(QLatin1String("failed to synchronize")))
        return QStringLiteral("The updates couldn't be downloaded. Check the internet connection and try again; "
                              "nothing was changed.");
    if (text.contains(QLatin1String("not enough free disk space")))
        return QStringLiteral("There wasn't enough free space, so nothing was changed.");
    if (text.contains(QLatin1String("signature from")) || text.contains(QLatin1String("invalid or corrupted package"))
        || text.contains(QLatin1String("unknown trust")))
        return QStringLiteral("Some updates couldn't be checked as genuine, so nothing was installed. This is usually "
                              "the security keys being out of date; Genie can help.");
    if (text.contains(QLatin1String("conflicting files")))
        return QStringLiteral("Two packages wanted the same file, so nothing was installed. Genie can help find which.");
    if (text.contains(QLatin1String("unable to satisfy dependency")) || text.contains(QLatin1String("could not satisfy dependencies"))
        || text.contains(QLatin1String("conflicting dependencies")))
        return QStringLiteral("Something installed on this computer blocks the update, often an app from outside "
                              "the official shelf. Nothing was installed; Genie can help find it.");
    if (text.contains(QLatin1String("stopped:")))
        return QStringLiteral("The update was stopped.");
    return QStringLiteral("The update didn't finish. Genie can read what happened and help.");
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

void SystemUpdate::setStage(const QString &stage)
{
    if (m_stage == stage)
        return;
    m_stage = stage;
    Q_EMIT changed();
}

void SystemUpdate::appendLog(const QString &line)
{
    m_log += line + QLatin1Char('\n');
    // The fold holds the end of it.
    if (m_log.size() > 20000)
        m_log = m_log.right(16000);
}

void SystemUpdate::check()
{
    if (busy())
        return;
    m_plan = Plan();
    m_result = Result();
    m_done = 0;
    m_total = 0;
    m_now.clear();
    m_log.clear();
    setStage(QStringLiteral("looking"));
    Q_EMIT progressChanged();
    // checkupdates fetches the lists, so it may take a while on a slow line.
    run({Shell, QStringLiteral("-c"), m_lookScript}, 90000, [this](int, const QString &output) {
        m_plan = readLook(output);
        qInfo().noquote() << QStringLiteral("%1: %2 system updates, %3 apps, %4 community waiting")
                                 .arg(productName()).arg(m_plan.system).arg(m_plan.apps).arg(m_plan.community);
        setStage(m_plan.nothing() ? QStringLiteral("nothing") : QStringLiteral("ready"));
        Q_EMIT changed();
    });
}

void SystemUpdate::start()
{
    if (m_stage != QLatin1String("ready") || !m_plan.blockers.isEmpty())
        return;
    m_done = 0;
    m_total = std::max(0, m_plan.system);
    m_now = QStringLiteral("Waiting for your password");
    m_log.clear();
    m_output.clear();
    setStage(QStringLiteral("updating"));
    Q_EMIT progressChanged();

    if (m_plan.system == 0) {
        afterSystem(0, QString());
        return;
    }
    QStringList program = m_elevate;
    program << Shell << QStringLiteral("-c") << m_updateScript << productName()
            << QStringLiteral("%1: Update").arg(productName())
            << (m_snapshots && !m_plan.snapPac ? QStringLiteral("1") : QStringLiteral("0"));
    qInfo().noquote() << QStringLiteral("%1: updating the system").arg(productName());
    // Never stopped by a timer: an update cut off halfway can leave the
    // computer unable to start.
    run(program, -1, [this](int exitCode, const QString &output) { afterSystem(exitCode, output); },
        [this](const QString &line) {
            appendLog(line);
            const Step step = readStep(line, m_done, m_total);
            if (step.total > 0)
                m_total = step.total;
            if (step.counts)
                ++m_done;
            if (!step.now.isEmpty())
                m_now = step.now;
            Q_EMIT progressChanged();
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
    m_result.installed = m_done;
    if (exitCode != 0) {
        m_result.ok = false;
        m_result.why = whyFailed(output, exitCode);
        qInfo().noquote() << QStringLiteral("%1: the update failed, exit code %2").arg(productName()).arg(exitCode);
        finish(output);
        return;
    }
    const auto after = [this] {
        m_now = QStringLiteral("Looking over the computer");
        Q_EMIT progressChanged();
        run({Shell, QStringLiteral("-c"), m_afterScript}, 30000, [this](int, const QString &afterOutput) {
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
    m_now = QStringLiteral("Updating your apps");
    Q_EMIT progressChanged();
    run({Shell, QStringLiteral("-c"), m_appsScript}, 30 * 60000,
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
    m_now.clear();
    setStage(m_result.ok ? QStringLiteral("done") : QStringLiteral("failed"));
    Q_EMIT progressChanged();
    Q_EMIT changed();
    Q_EMIT finished(m_result, output);
}

void SystemUpdate::close()
{
    if (m_stage == QLatin1String("updating"))
        return;
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->deleteLater();
        m_process = nullptr;
    }
    setStage(QString());
}

void SystemUpdate::restart()
{
    // The desktop's own prompt, which counts down and can be cancelled.
    QProcess::startDetached(QStringLiteral("qdbus6"), {QStringLiteral("org.kde.LogoutPrompt"),
                                                       QStringLiteral("/LogoutPrompt"),
                                                       QStringLiteral("promptReboot")});
}

void SystemUpdate::openLink(const QString &link)
{
    const QUrl url(link);
    if (url.scheme() == QLatin1String("https"))
        QProcess::startDetached(QStringLiteral("xdg-open"), {url.toString()});
}

void SystemUpdate::run(const QStringList &program, int timeLimit, std::function<void(int, const QString &)> done,
                       std::function<void(const QString &)> eachLine)
{
    auto *process = new QProcess(this);
    m_process = process;
    process->setProcessChannelMode(QProcess::MergedChannels);
    process->setProgram(program.first());
    process->setArguments(program.mid(1));
    auto output = std::make_shared<QString>();
    auto partial = std::make_shared<QString>();
    connect(process, &QProcess::readyRead, this, [process, output, partial, eachLine] {
        const QString chunk = QString::fromUtf8(process->readAll());
        *output += chunk;
        *partial += chunk;
        qsizetype end;
        while ((end = partial->indexOf(QLatin1Char('\n'))) >= 0) {
            const QString line = partial->left(end);
            partial->remove(0, end + 1);
            if (eachLine)
                eachLine(line);
        }
    });
    connect(process, &QProcess::finished, this, [this, process, output, done](int exitCode, QProcess::ExitStatus status) {
        *output += QString::fromUtf8(process->readAll());
        if (process->property("timedOut").toBool())
            *output += QStringLiteral("\nStopped: it was still running when its time was up.");
        if (m_process == process)
            m_process = nullptr;
        process->deleteLater();
        done(status == QProcess::NormalExit ? exitCode : -1, output->trimmed());
    });
    connect(process, &QProcess::errorOccurred, this, [this, process, done](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        if (m_process == process)
            m_process = nullptr;
        process->deleteLater();
        done(-2, QStringLiteral("%1 could not be started.").arg(process->program()));
    });
    if (timeLimit > 0) {
        auto *limit = new QTimer(process);
        limit->setSingleShot(true);
        connect(limit, &QTimer::timeout, process, [process] {
            process->setProperty("timedOut", true);
            process->kill();
        });
        connect(process, &QProcess::started, limit, [limit, timeLimit] { limit->start(timeLimit); });
    }
    process->start();
    process->closeWriteChannel();
}

} // namespace splitrock
