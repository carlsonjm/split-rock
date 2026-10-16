// SPDX-License-Identifier: GPL-2.0-or-later
#include "app/RestorePoints.h"

#include "app/Product.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>

#include <algorithm>

namespace splitrock {

namespace {

// Run as the person: nothing here changes anything.
const QString LookScript = QStringLiteral(R"SH(export LANG=C LC_ALL=C
echo "@@ snapper"
command -v snapper >/dev/null 2>&1 && [ -f /etc/snapper/configs/root ] && echo yes
echo "@@ list"
command -v snapper >/dev/null 2>&1 && snapper -c root --jsonout list 2>&1
echo "@@ menu"
command -v limine-snapper-sync >/dev/null 2>&1 && echo limine
[ -e /etc/grub.d/41_snapshots-btrfs ] && echo grub
echo "@@ root"
findmnt -no OPTIONS / 2>/dev/null
cat /proc/cmdline 2>/dev/null
echo "@@ home"
findmnt -no OPTIONS /home 2>/dev/null
echo "@@ limits"
grep -E '^(NUMBER_CLEANUP|NUMBER_LIMIT|NUMBER_LIMIT_IMPORTANT)=' /etc/snapper/configs/root 2>/dev/null
echo "@@ tools"
command -v btrfs-assistant-launcher >/dev/null 2>&1 && echo btrfs-assistant-launcher
command -v btrfs-assistant >/dev/null 2>&1 && echo btrfs-assistant
echo "@@ end"
)SH");

QString when(const QDateTime &date)
{
    if (!date.isValid())
        return QString();
    const QDate today = QDate::currentDate();
    const QString time = date.toString(QStringLiteral("HH:mm"));
    if (date.date() == today)
        return QStringLiteral("Today, %1").arg(time);
    if (date.date() == today.addDays(-1))
        return QStringLiteral("Yesterday, %1").arg(time);
    if (date.date().year() == today.year())
        return date.toString(QStringLiteral("d MMMM, HH:mm"));
    return date.toString(QStringLiteral("d MMMM yyyy, HH:mm"));
}

} // namespace

QVariantMap RestorePoints::Point::toMap() const
{
    return {{QStringLiteral("number"), number},
            {QStringLiteral("date"), date},
            {QStringLiteral("when"), when(date)},
            {QStringLiteral("what"), what},
            {QStringLiteral("kind"), kind},
            {QStringLiteral("own"), own}};
}

RestorePoints::RestorePoints(QObject *parent)
    : Chore(parent)
    , m_lookScript(LookScript)
{
}

bool RestorePoints::busy() const
{
    return stage() == QLatin1String("looking");
}

QString RestorePoints::describe(const QString &description, const QString &cleanup, QString *kind)
{
    const auto said = [kind](const QString &k, const QString &words) {
        if (kind)
            *kind = k;
        return words;
    };
    const QString d = description.simplified();
    if (cleanup == QLatin1String("timeline") || d == QLatin1String("timeline"))
        return said(QStringLiteral("timeline"), QStringLiteral("Hourly restore point"));
    if (d == QLatin1String("boot"))
        return said(QStringLiteral("boot"), QStringLiteral("When the computer started"));

    // Split Rock's own, under either of its names: "Genie: Install firefox".
    static const QRegularExpression own(QStringLiteral(R"(^(Split Rock|Genie): (.+)$)"));
    if (const QRegularExpressionMatch m = own.match(d); m.hasMatch()) {
        const QString job = m.captured(2);
        if (job == QLatin1String("Update"))
            return said(QStringLiteral("update"), QStringLiteral("Before updating the computer"));
        if (job == QLatin1String("Free up space"))
            return said(QStringLiteral("remove"), QStringLiteral("Before freeing up space"));
        if (job.startsWith(QLatin1String("Install ")))
            return said(QStringLiteral("install"), QStringLiteral("Before installing %1").arg(job.mid(8)));
        if (job.startsWith(QLatin1String("Remove ")))
            return said(QStringLiteral("remove"), QStringLiteral("Before removing %1").arg(job.mid(7)));
        if (job.startsWith(QLatin1String("Community ")))
            return said(QStringLiteral("install"), QStringLiteral("Before updating %1").arg(job.mid(10)));
        return said(QStringLiteral("other"), QStringLiteral("Before a change: %1").arg(job));
    }

    // snap-pac's: the pacman command line, as "pacman -S firefox", or as a
    // helper ran it, "pacman --upgrade --color=always --noconfirm -- /path".
    const QStringList words = d.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const qsizetype at = words.indexOf(QStringLiteral("pacman"));
    if (at >= 0 && at + 1 < words.size()) {
        QString op;
        QStringList names;
        bool fromCommunity = false;
        for (qsizetype i = at + 1; i < words.size(); ++i) {
            const QString &w = words.at(i);
            if (w == QLatin1String("--sync") || w == QLatin1String("-sync"))
                op = op.isEmpty() ? QStringLiteral("-S") : op;
            else if (w == QLatin1String("--upgrade") || w == QLatin1String("-upgrade"))
                op = op.isEmpty() ? QStringLiteral("-U") : op;
            else if (w == QLatin1String("--remove") || w == QLatin1String("-remove"))
                op = op.isEmpty() ? QStringLiteral("-R") : op;
            else if (w == QLatin1String("--sysupgrade") || w == QLatin1String("-sysupgrade"))
                op += QLatin1Char('u');
            else if (w.startsWith(QLatin1Char('-')) && !w.startsWith(QLatin1String("--")) && w.size() > 1 && w.at(1).isUpper())
                op = op.isEmpty() ? w : op;
            else if (w.startsWith(QLatin1Char('-')))
                continue;
            else if (w.contains(QLatin1Char('/'))) {
                // A package file a community app helper built.
                fromCommunity = fromCommunity || w.contains(QLatin1String("/.cache/paru")) || w.contains(QLatin1String("/.cache/yay"))
                    || w.contains(QLatin1String("/.cache/"));
            } else
                names.append(w);
        }
        if (op.startsWith(QLatin1String("-U")) && fromCommunity)
            return said(QStringLiteral("install"), QStringLiteral("Before installing a community app"));
        const QString list = names.mid(0, 3).join(QStringLiteral(", ")) + (names.size() > 3 ? QStringLiteral("…") : QString());
        if (op.startsWith(QLatin1String("-S")) && (op.contains(QLatin1Char('u')) || names.isEmpty()))
            return said(QStringLiteral("update"), QStringLiteral("Before updating the computer"));
        if (op.startsWith(QLatin1String("-S")))
            return said(QStringLiteral("install"), QStringLiteral("Before installing %1").arg(list));
        if (op.startsWith(QLatin1String("-U")))
            return said(QStringLiteral("install"), QStringLiteral("Before installing a downloaded package"));
        if (op.startsWith(QLatin1String("-R")))
            return said(QStringLiteral("remove"), names.isEmpty() ? QStringLiteral("Before removing apps")
                                                                  : QStringLiteral("Before removing %1").arg(list));
    }
    return said(QStringLiteral("other"), d.isEmpty() ? QStringLiteral("Restore point") : d);
}

bool RestorePoints::isOwn(const QString &description)
{
    static const QRegularExpression own(QStringLiteral(R"(^\s*(Split Rock|Genie): )"));
    return own.match(description).hasMatch();
}

QList<RestorePoints::Point> RestorePoints::readList(const QString &json)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    QJsonArray rows;
    if (doc.isObject()) {
        const QJsonObject object = doc.object();
        rows = object.value(QStringLiteral("root")).toArray();
        if (rows.isEmpty() && !object.isEmpty())
            rows = object.begin().value().toArray();
    }
    QList<Point> points;
    // A pair is one restore point: the one before is what going back reaches,
    // so the one after is left out.
    for (const QJsonValue &value : rows) {
        const QJsonObject row = value.toObject();
        const int number = row.value(QStringLiteral("number")).toInt();
        const QString type = row.value(QStringLiteral("type")).toString();
        if (number == 0 || type == QLatin1String("post"))
            continue;
        Point p;
        p.number = number;
        p.date = QDateTime::fromString(row.value(QStringLiteral("date")).toString(), QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        const QString description = row.value(QStringLiteral("description")).toString();
        p.what = describe(description, row.value(QStringLiteral("cleanup")).toString(), &p.kind);
        p.own = isOwn(description);
        points.append(p);
    }
    std::sort(points.begin(), points.end(), [](const Point &a, const Point &b) { return a.number > b.number; });
    return points;
}

RestorePoints::Look RestorePoints::readLook(const QString &output)
{
    Look look;
    const QMap<QString, QStringList> found = sections(output);
    look.configured = found.value(QStringLiteral("snapper")).contains(QStringLiteral("yes"));
    const QString list = found.value(QStringLiteral("list")).join(QLatin1Char('\n'));
    if (list.contains(QLatin1String("No permissions"), Qt::CaseInsensitive)
        || list.contains(QLatin1String("permission denied"), Qt::CaseInsensitive))
        look.denied = true;
    else
        look.points = readList(list);
    const QStringList menu = found.value(QStringLiteral("menu"));
    look.menu = menu.contains(QStringLiteral("limine")) ? QStringLiteral("limine")
              : menu.contains(QStringLiteral("grub"))   ? QStringLiteral("grub")
                                                        : QString();
    // The person's own folder on a volume of its own, which restore points
    // of the system leave as it is.
    look.homeSeparate = found.value(QStringLiteral("home")).join(QLatin1Char(' ')).contains(QLatin1String("subvol="));
    // How many the system keeps before it clears the oldest, as snapper's
    // number cleanup does: "50", or a range such as "2-10", whose top counts.
    for (const QString &line : found.value(QStringLiteral("limits"))) {
        const QString key = line.section(QLatin1Char('='), 0, 0);
        const QString value = line.section(QLatin1Char('='), 1).remove(QLatin1Char('"')).trimmed();
        const int top = value.section(QLatin1Char('-'), -1).toInt();
        if (key == QLatin1String("NUMBER_CLEANUP"))
            look.cleanup = value == QLatin1String("yes");
        else if (key == QLatin1String("NUMBER_LIMIT"))
            look.limit = top;
        else if (key == QLatin1String("NUMBER_LIMIT_IMPORTANT"))
            look.limitImportant = top;
    }
    const QStringList tools = found.value(QStringLiteral("tools"));
    look.keepTool = tools.value(0);
    // Started from a restore point: / is mounted from its snapshot.
    static const QRegularExpression snapshot(QStringLiteral(R"(\.snapshots/(\d+)/snapshot)"));
    if (const QRegularExpressionMatch m = snapshot.match(found.value(QStringLiteral("root")).join(QLatin1Char(' ')));
        m.hasMatch())
        look.bootedFrom = m.captured(1).toInt();
    return look;
}

QVariantList RestorePoints::pointList() const
{
    QVariantList list;
    for (const Point &p : m_look.points)
        list.append(p.toMap());
    return list;
}

QString RestorePoints::keeping() const
{
    if (!m_look.cleanup || m_look.limit <= 0)
        return {};
    const QString kept = m_look.limitImportant > 0
        ? QStringLiteral("the newest %1, plus %2 marked important, such as those before a new kernel").arg(m_look.limit).arg(m_look.limitImportant)
        : QStringLiteral("the newest %1").arg(m_look.limit);
    return QStringLiteral("You don't need to delete any. The computer clears old ones by itself and keeps %1.").arg(kept);
}

int RestorePoints::ownCount() const
{
    return int(std::count_if(m_look.points.cbegin(), m_look.points.cend(), [](const Point &p) { return p.own; }));
}

QString RestorePoints::newest() const
{
    return m_look.points.isEmpty() ? QString() : when(m_look.points.first().date);
}

QStringList RestorePoints::steps() const
{
    if (m_chosen.number <= 0)
        return {};
    const QString which = QStringLiteral("Pick the one from %1, number %2.").arg(when(m_chosen.date)).arg(m_chosen.number);
    if (m_look.menu == QLatin1String("limine"))
        return {QStringLiteral("Restart the computer."),
                QStringLiteral("In the startup menu, open Snapshots."), which,
                QStringLiteral("Look around. Nothing is kept until you say so: restarting normally comes back to now."),
                QStringLiteral("If it's right, open %1 again and tap Keep this.").arg(productName())};
    if (m_look.menu == QLatin1String("grub"))
        return {QStringLiteral("Restart the computer."),
                QStringLiteral("In the startup menu, open the snapshots entry."), which,
                QStringLiteral("Look around. Nothing is kept until you say so: restarting normally comes back to now."),
                QStringLiteral("If it's right, open %1 again and tap Keep this.").arg(productName())};
    return {};
}

void RestorePoints::check()
{
    if (busy())
        return;
    m_chosen = Point();
    clearProgress();
    setStage(QStringLiteral("looking"));
    run(asPerson(m_lookScript), 30000, [this](int, const QString &output) {
        m_look = readLook(output);
        settle();
    });
}

void RestorePoints::checkAsAdmin()
{
    if (stage() != QLatin1String("denied"))
        return;
    setStage(QStringLiteral("looking"));
    run(asAdmin(QStringLiteral("restore-list")), 60000, [this](int, const QString &output) {
        const Look listed = readLook(output);
        m_look.denied = listed.denied;
        m_look.points = listed.points;
        settle();
    });
}

void RestorePoints::settle()
{
    qInfo().noquote() << QStringLiteral("%1: %2 restore points").arg(productName()).arg(m_look.points.size());
    if (m_look.bootedFrom > 0)
        setStage(QStringLiteral("booted"));
    else if (m_look.denied)
        setStage(QStringLiteral("denied"));
    else if (m_look.points.isEmpty())
        setStage(QStringLiteral("nothing"));
    else
        setStage(QStringLiteral("ready"));
    Q_EMIT changed();
}

void RestorePoints::choose(int number)
{
    if (stage() != QLatin1String("ready"))
        return;
    const auto found = std::find_if(m_look.points.cbegin(), m_look.points.cend(),
                                    [number](const Point &p) { return p.number == number; });
    if (found == m_look.points.cend())
        return;
    m_chosen = *found;
    setStage(QStringLiteral("chosen"));
    Q_EMIT changed();
}

void RestorePoints::back()
{
    if (stage() != QLatin1String("chosen"))
        return;
    m_chosen = Point();
    setStage(QStringLiteral("ready"));
    Q_EMIT changed();
}

void RestorePoints::openKeepTool()
{
    if (!m_look.keepTool.isEmpty())
        QProcess::startDetached(m_look.keepTool, {});
}

} // namespace splitrock
