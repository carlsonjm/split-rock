// SPDX-License-Identifier: GPL-2.0-or-later
#include "app/SpaceCleanup.h"

#include "app/Product.h"

#include <QDateTime>
#include <QLocale>
#include <QMap>
#include <QDebug>
#include <QRegularExpression>
#include <QVariantMap>

#include <algorithm>
#include <utility>

namespace splitrock {

namespace {

// Run as the person: each section starts with a line "@@ name". Nothing here
// changes anything; paccache's dry runs only count. The parts nothing uses
// come from split-rock-admin's own list (its unused job), the same one it
// removes from, so what building software needs is never among them.
const QString LookScript = QStringLiteral(R"SH(export LANG=C LC_ALL=C
echo "@@ disk"
df -Pk / 2>/dev/null | awk 'NR==2 {print $2, $4}'
echo "@@ pkgcache"
if command -v paccache >/dev/null 2>&1; then
    # Each copy it would clear, as "file BYTES NAME", then its total.
    for keep in -dvk1 -dvuk0; do
        paccache "$keep" 2>/dev/null | while IFS= read -r line; do
            f=$(printf '%s\n' "$line" | grep -oE '[^ /]+\.pkg\.tar(\.[a-z0-9]+)?' | head -n 1)
            if [ -n "$f" ]; then
                echo "file $(stat -c %s "/var/cache/pacman/pkg/$f" 2>/dev/null || echo -1) $f"
            else
                printf '%s\n' "$line"
            fi
        done
    done
fi
echo "@@ journal"
journalctl --disk-usage 2>/dev/null
echo "@@ journalfiles"
find /var/log/journal -type f -name '*@*.journal*' -printf '%T@ %s %f\n' 2>/dev/null | sort -n
echo "@@ crashes"
find /var/lib/systemd/coredump -type f -printf '%s %T@ %f\n' 2>/dev/null
echo "@@ cache"
find "$HOME/.cache" -type f -atime +30 ! -path "$HOME/.cache/split-rock/*" ! -path "$HOME/.cache/paru/*" \
    ! -path "$HOME/.cache/yay/*" -printf '%s %P\n' 2>/dev/null
echo "@@ builds"
for d in "$HOME/.cache/paru/clone" "$HOME/.cache/yay" "$HOME/.cache/split-rock/aur"; do
    [ -d "$d" ] && printf '%s %s\n' "$(du -sk "$d" 2>/dev/null | cut -f1)" "${d#"$HOME"/}"
done
echo "@@ trash"
[ -d "$HOME/.local/share/Trash/files" ] && du -sk "$HOME/.local/share/Trash" 2>/dev/null | cut -f1 \
    && ls -A "$HOME/.local/share/Trash/files" | wc -l
echo "@@ trashfiles"
for f in "$HOME/.local/share/Trash/files"/* "$HOME/.local/share/Trash/files"/.[!.]* \
    "$HOME/.local/share/Trash/files"/..?*; do
    [ -e "$f" ] || [ -L "$f" ] || continue
    printf '%s\t%s\n' "$(du -sk -- "$f" 2>/dev/null | cut -f1)" "${f##*/}"
done
echo "@@ apps"
command -v flatpak >/dev/null 2>&1 && printf 'n\n' | flatpak uninstall --unused 2>/dev/null
echo "@@ snapshots"
ls /usr/share/libalpm/hooks 2>/dev/null | grep -q snap-pac && echo snap-pac
echo "@@ end"
)SH");

// Run as the person: the jobs it names.
const QString PersonScript = QStringLiteral(R"SH(export LANG=C LC_ALL=C
for job in "$@"; do
    case $job in
        cache) echo "@@ step cache"
            find "$HOME/.cache" -type f -atime +30 ! -path "$HOME/.cache/split-rock/*" ! -path "$HOME/.cache/paru/*" \
                ! -path "$HOME/.cache/yay/*" -delete 2>/dev/null
            find "$HOME/.cache" -mindepth 1 -type d -empty ! -path "$HOME/.cache/split-rock*" ! -path "$HOME/.cache/paru*" \
                ! -path "$HOME/.cache/yay*" -delete 2>/dev/null ;;
        builds) echo "@@ step builds"
            rm -rf -- "$HOME/.cache/paru/clone" "$HOME/.cache/yay" "$HOME/.cache/split-rock/aur" ;;
        trash) echo "@@ step trash"
            if command -v ktrash6 >/dev/null 2>&1; then ktrash6 --empty
            elif command -v gio >/dev/null 2>&1; then gio trash --empty
            fi ;;
        apps) echo "@@ step apps"; flatpak uninstall --unused -y --noninteractive ;;
    esac
done
exit 0
)SH");

// The free space once the drive has written out what was deleted: a btrfs
// drive gives space back a moment after the files go.
const QString FreeScript = QStringLiteral("sync -f / 2>/dev/null; sleep 3; df -Pk / 2>/dev/null | awk 'NR==2 {print $4}'");

qint64 sumBytes(const QStringList &lines, int *count = nullptr)
{
    qint64 bytes = 0;
    int n = 0;
    for (const QString &line : lines) {
        bool ok = false;
        const qint64 b = line.section(QLatin1Char(' '), 0, 0).toLongLong(&ok);
        if (ok) {
            bytes += b;
            ++n;
        }
    }
    if (count)
        *count = n;
    return bytes;
}

// The most a row's list shows; the rest are counted.
constexpr int Longest = 300;

QStringList capped(QStringList list)
{
    if (list.size() > Longest) {
        const qsizetype more = list.size() - Longest;
        list = list.mid(0, Longest);
        list.append(QStringLiteral("and %1 more").arg(more));
    }
    return list;
}

// "Oct 15" from seconds since 1970, as find's %T@ prints them.
QString day(const QString &seconds)
{
    const QDateTime when = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(seconds.toDouble()));
    return when.isValid() && seconds.toDouble() > 0 ? QLocale(QLocale::English).toString(when.date(), QStringLiteral("MMM d"))
                                                    : QString();
}

// "firefox 131.0-1" from "firefox-131.0-1-x86_64.pkg.tar.zst".
QString packageFile(const QString &file)
{
    const QStringList parts = file.section(QStringLiteral(".pkg.tar"), 0, 0).split(QLatin1Char('-'));
    if (parts.size() < 4)
        return file;
    return QStringLiteral("%1 %2-%3").arg(parts.mid(0, parts.size() - 3).join(QLatin1Char('-')),
                                          parts.at(parts.size() - 3), parts.at(parts.size() - 2));
}

// "kwin_wayland" from "core.kwin_wayland.1000.<boot>.<pid>.<time>.zst",
// with systemd's \x2d escapes read back.
QString crashedApp(const QString &file)
{
    QString app = file.section(QLatin1Char('.'), 1, 1);
    static const QRegularExpression escaped(QStringLiteral(R"(\\x([0-9a-fA-F]{2}))"));
    QRegularExpressionMatch m;
    while ((m = escaped.match(app)).hasMatch())
        app.replace(m.capturedStart(), m.capturedLength(), QChar(m.captured(1).toInt(nullptr, 16)));
    return app.isEmpty() ? file : app;
}

QString line(const QString &name, qint64 kb, const QString &when = {})
{
    QStringList parts{name};
    if (kb >= 0)
        parts.append(Chore::size(kb));
    if (!when.isEmpty())
        parts.append(when);
    return parts.join(QStringLiteral(" · "));
}

// What each job's step line says the card is doing.
QString doing(const QString &id)
{
    static const QMap<QString, QString> words{
        {QStringLiteral("pkgcache"), QStringLiteral("Clearing old versions of installed apps")},
        {QStringLiteral("journal"), QStringLiteral("Trimming the system logs")},
        {QStringLiteral("crashes"), QStringLiteral("Clearing crash reports")},
        {QStringLiteral("orphans"), QStringLiteral("Removing parts nothing uses")},
        {QStringLiteral("cache"), QStringLiteral("Clearing old cache files")},
        {QStringLiteral("builds"), QStringLiteral("Clearing community apps' build files")},
        {QStringLiteral("trash"), QStringLiteral("Emptying the trash")},
        {QStringLiteral("apps"), QStringLiteral("Removing app parts nothing uses")},
    };
    return words.value(id);
}

} // namespace

SpaceCleanup::SpaceCleanup(QObject *parent)
    : Chore(parent)
    , m_lookScript(LookScript)
    , m_personScript(PersonScript)
    , m_freeScript(FreeScript)
{
}

bool SpaceCleanup::busy() const
{
    return stage() == QLatin1String("looking") || stage() == QLatin1String("clearing");
}

qint64 SpaceCleanup::readSize(const QString &text)
{
    static const QRegularExpression amount(QStringLiteral(R"(([\d.]+)\s*([KMGT])(i?B)?\b)"));
    const QRegularExpressionMatch m = amount.match(text);
    if (!m.hasMatch())
        return -1;
    bool ok = false;
    const double value = m.captured(1).toDouble(&ok);
    if (!ok)
        return -1;
    const QChar unit = m.captured(2).at(0);
    const double kb = unit == QLatin1Char('K') ? value
                    : unit == QLatin1Char('M') ? value * 1024
                    : unit == QLatin1Char('G') ? value * 1024 * 1024
                                               : value * 1024 * 1024 * 1024;
    return static_cast<qint64>(kb);
}

SpaceCleanup::Look SpaceCleanup::readLook(const QString &output)
{
    const QMap<QString, QStringList> found = sections(output);
    Look look;
    const QStringList disk = found.value(QStringLiteral("disk")).value(0).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (disk.size() == 2) {
        look.diskKb = disk.at(0).toLongLong();
        look.freeKb = disk.at(1).toLongLong();
    }
    look.snapPac = found.value(QStringLiteral("snapshots")).contains(QStringLiteral("snap-pac"));

    // paccache's dry runs: "==> finished dry run: 23 candidates (disk space saved: 1.2 GiB)".
    {
        static const QRegularExpression dry(QStringLiteral(R"(dry run: (\d+) candidates? \(disk space saved: ([^)]+)\))"));
        Item item{QStringLiteral("pkgcache"), QStringLiteral("Old versions of installed apps"),
                  QStringLiteral("Copies kept from earlier updates. The one before each app's newest stays."),
                  0, 0, true, true, {}, QStringLiteral("teal")};
        QStringList seen;
        for (const QString &text : found.value(QStringLiteral("pkgcache"))) {
            if (text.startsWith(QLatin1String("file "))) {
                const QString file = text.section(QLatin1Char(' '), 2);
                if (file.isEmpty() || seen.contains(file))
                    continue;
                seen.append(file);
                const qint64 bytes = text.section(QLatin1Char(' '), 1, 1).toLongLong();
                item.list.append(line(packageFile(file), bytes < 0 ? -1 : bytes / 1024));
                continue;
            }
            const QRegularExpressionMatch m = dry.match(text);
            if (!m.hasMatch())
                continue;
            item.count += m.captured(1).toInt();
            item.kb += std::max<qint64>(0, readSize(m.captured(2)));
        }
        item.list.sort(Qt::CaseInsensitive);
        item.list = capped(item.list);
        if (item.count > 0)
            look.items.append(item);
    }

    {
        Item item{QStringLiteral("orphans"), QStringLiteral("Parts nothing uses"),
                  QStringLiteral("Installed for an app since removed. Only the ones listed are removed, each by name. "
                                 "Tools and files for building software always stay."),
                  0, 0, true, false, found.value(QStringLiteral("orphans")), QStringLiteral("violet")};
        static const QRegularExpression name(QStringLiteral("^[a-z0-9@._+-]+$"));
        item.list.erase(std::remove_if(item.list.begin(), item.list.end(),
                                       [](const QString &n) { return !name.match(n).hasMatch(); }),
                        item.list.end());
        item.list.removeDuplicates();
        item.count = item.list.size();
        for (const QString &line : found.value(QStringLiteral("orphansize")))
            item.kb += std::max<qint64>(0, readSize(line));
        if (item.count > 0)
            look.items.append(item);
    }

    // "Archived and active journals take up 1.2G in the file system."; 200 MB stays.
    {
        const qint64 kb = readSize(found.value(QStringLiteral("journal")).join(QLatin1Char(' ')));
        const qint64 kept = 200 * 1024;
        if (kb > kept + 50 * 1024) {
            // Trimming takes the oldest finished log files until what is left
            // fits: those, as they are named for what they hold.
            QStringList list;
            qint64 left = kb;
            for (const QString &text : found.value(QStringLiteral("journalfiles"))) {
                if (left <= kept)
                    break;
                const QStringList f = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
                if (f.size() < 3)
                    continue;
                const qint64 fileKb = f.at(1).toLongLong() / 1024;
                const QString file = f.at(2);
                const QString whose = file.startsWith(QLatin1String("user-")) ? QStringLiteral("Your apps' log")
                                                                               : QStringLiteral("System log");
                list.append(line(whose, fileKb, day(f.at(0))));
                left -= fileKb;
            }
            look.items.append({QStringLiteral("journal"), QStringLiteral("System logs"),
                               QStringLiteral("Records of what the computer did. The most recent stay."),
                               kb - kept, 0, true, true, capped(list), QStringLiteral("amber")});
        }
    }

    {
        int count = 0;
        const qint64 bytes = sumBytes(found.value(QStringLiteral("crashes")), &count);
        QStringList list;
        for (const QString &text : found.value(QStringLiteral("crashes"))) {
            const QStringList f = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (f.size() >= 3)
                list.append(line(crashedApp(f.at(2)), f.at(0).toLongLong() / 1024, day(f.at(1))));
        }
        if (count > 0)
            look.items.append({QStringLiteral("crashes"), QStringLiteral("Crash reports"),
                               QStringLiteral("Saved when an app crashed. Only needed to report that crash."),
                               bytes / 1024, count, true, true, capped(list), QStringLiteral("coral")});
    }

    {
        int count = 0;
        const qint64 bytes = sumBytes(found.value(QStringLiteral("cache")), &count);
        // By the folder each app keeps them in, biggest first.
        QMap<QString, qint64> folders;
        for (const QString &text : found.value(QStringLiteral("cache"))) {
            const QString path = text.section(QLatin1Char(' '), 1);
            if (!path.isEmpty())
                folders[path.section(QLatin1Char('/'), 0, 0)] += text.section(QLatin1Char(' '), 0, 0).toLongLong();
        }
        QList<std::pair<QString, qint64>> sorted(folders.keyValueBegin(), folders.keyValueEnd());
        std::stable_sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) { return a.second > b.second; });
        QStringList list;
        for (const auto &[folder, b] : sorted)
            list.append(line(folder, b / 1024));
        if (bytes > 10 * 1024 * 1024)
            look.items.append({QStringLiteral("cache"), QStringLiteral("Old cache files"),
                               QStringLiteral("Apps' spare copies, unused for a month. Apps make them again when needed."),
                               bytes / 1024, count, false, true, capped(list), QStringLiteral("blue")});
    }

    {
        qint64 kb = 0;
        QStringList list;
        for (const QString &text : found.value(QStringLiteral("builds"))) {
            const qint64 folderKb = text.section(QLatin1Char(' '), 0, 0).toLongLong();
            kb += folderKb;
            const QString folder = text.section(QLatin1Char(' '), 1);
            if (!folder.isEmpty())
                list.append(line(QStringLiteral("~/") + folder, folderKb));
        }
        if (kb > 10 * 1024)
            look.items.append({QStringLiteral("builds"), QStringLiteral("Community apps' build files"),
                               QStringLiteral("Left from building community apps. Fetched again for the next update."),
                               kb, 0, false, true, list, QStringLiteral("orange")});
    }

    {
        const QStringList trash = found.value(QStringLiteral("trash"));
        const qint64 kb = trash.value(0).toLongLong();
        const int count = trash.value(1).toInt();
        // "KB<tab>name", the size empty when it couldn't be read.
        QList<std::pair<QString, qint64>> things;
        for (const QString &text : found.value(QStringLiteral("trashfiles"))) {
            const qsizetype tab = text.indexOf(QLatin1Char('\t'));
            const QString name = tab >= 0 ? text.mid(tab + 1).section(QLatin1Char('/'), -1) : text;
            if (name.isEmpty())
                continue;
            bool ok = false;
            const qint64 thingKb = tab > 0 ? text.left(tab).toLongLong(&ok) : -1;
            things.append({name, ok ? thingKb : -1});
        }
        std::stable_sort(things.begin(), things.end(), [](const auto &a, const auto &b) { return a.second > b.second; });
        QStringList list;
        for (const auto &[name, thingKb] : things)
            list.append(line(name, thingKb));
        if (count > 0)
            look.items.append({QStringLiteral("trash"), QStringLiteral("Trash"),
                               count == 1 ? QStringLiteral("1 thing you deleted. Emptied for good.")
                                          : QStringLiteral("%1 things you deleted. Emptied for good.").arg(count),
                               kb, count, false, false, capped(list), QStringLiteral("pink")});
    }

    // flatpak's own list, answered no: " 1.   org.kde.Platform   6.7   system   r".
    {
        static const QRegularExpression ref(QStringLiteral(R"(^\s*\d+\.\s+(\S+\.\S+)\s)"));
        Item item{QStringLiteral("apps"), QStringLiteral("App parts nothing uses"),
                  QStringLiteral("Shared parts left by apps from Discover or Flathub that are gone."),
                  -1, 0, false, true, {}, QStringLiteral("green")};
        for (const QString &line : output.section(QStringLiteral("@@ apps"), 1).section(QStringLiteral("@@ "), 0, 0)
                                       .split(QLatin1Char('\n'))) {
            const QRegularExpressionMatch m = ref.match(line);
            if (m.hasMatch())
                item.list.append(m.captured(1));
        }
        item.count = item.list.size();
        if (item.count > 0)
            look.items.append(item);
    }

    // Biggest first; an unknown size last.
    std::stable_sort(look.items.begin(), look.items.end(), [](const Item &a, const Item &b) { return a.kb > b.kb; });
    return look;
}

QVariantList SpaceCleanup::itemList() const
{
    QVariantList list;
    for (const Item &item : m_look.items) {
        QString words = item.kb >= 0 ? size(item.kb) : QString();
        if (item.kb < 0)
            words = item.count == 1 ? QStringLiteral("1 part") : QStringLiteral("%1 parts").arg(item.count);
        list.append(QVariantMap{{QStringLiteral("id"), item.id},
                                {QStringLiteral("title"), item.title},
                                {QStringLiteral("detail"), item.detail},
                                {QStringLiteral("size"), words},
                                {QStringLiteral("kb"), item.kb},
                                {QStringLiteral("count"), item.count},
                                {QStringLiteral("admin"), item.admin},
                                {QStringLiteral("chosen"), item.chosen},
                                {QStringLiteral("list"), item.list.join(QLatin1Char('\n'))},
                                {QStringLiteral("tone"), item.tone}});
    }
    return list;
}

qint64 SpaceCleanup::chosenKb() const
{
    qint64 kb = 0;
    for (const Item &item : m_look.items) {
        if (item.chosen && item.kb > 0)
            kb += item.kb;
    }
    return kb;
}

bool SpaceCleanup::needsPassword() const
{
    return std::any_of(m_look.items.cbegin(), m_look.items.cend(), [](const Item &i) { return i.chosen && i.admin; });
}

void SpaceCleanup::check()
{
    if (busy())
        return;
    m_look = Look();
    m_freed.clear();
    m_heldBack.clear();
    m_why.clear();
    clearProgress();
    setStage(QStringLiteral("looking"));
    run(asPerson(m_lookScript), 120000, [this](int, const QString &output) {
        // Then the parts nothing uses, as the administrator's job counts
        // them; none are offered when it can't run.
        run(asPersonJob(QStringLiteral("unused")), 120000, [this, output](int code, const QString &unused) {
            looked(code == 0 ? output + QLatin1Char('\n') + unused : output);
        });
    });
}

void SpaceCleanup::looked(const QString &output)
{
    m_look = readLook(output);
    qInfo().noquote() << QStringLiteral("%1: %2 kinds of leftovers, %3").arg(productName()).arg(m_look.items.size())
                             .arg(size(chosenKb()));
    setStage(m_look.items.isEmpty() ? QStringLiteral("nothing") : QStringLiteral("ready"));
    Q_EMIT changed();
}

void SpaceCleanup::choose(const QString &id, bool chosen)
{
    if (stage() != QLatin1String("ready"))
        return;
    for (Item &item : m_look.items) {
        if (item.id == id)
            item.chosen = chosen;
    }
    Q_EMIT changed();
}

void SpaceCleanup::start()
{
    if (stage() != QLatin1String("ready"))
        return;
    QStringList adminJobs;
    QStringList personJobs;
    QStringList orphans;
    m_chosenTitles.clear();
    for (const Item &item : std::as_const(m_look.items)) {
        if (!item.chosen)
            continue;
        m_chosenTitles.append(item.title.toLower());
        if (item.id == QLatin1String("orphans"))
            orphans = item.list;
        else if (item.admin)
            adminJobs.append(item.id);
        else
            personJobs.append(item.id);
    }
    if (m_chosenTitles.isEmpty())
        return;

    m_id = QStringLiteral("space-%1").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    QString unrecorded;
    const QString undo = orphans.isEmpty()
        ? QString()
        : QStringLiteral("Install again what was removed (%1), or go back to the restore point taken just before")
              .arg(orphans.join(QStringLiteral(", ")));
    if (!writeDown(m_id, QStringLiteral("Freed up space: %1").arg(m_chosenTitles.join(QStringLiteral(", "))), undo,
                   &unrecorded)) {
        m_why = QStringLiteral("Nothing was changed: it couldn't be written in the notebook first. %1").arg(unrecorded);
        m_id.clear();
        setStage(QStringLiteral("failed"));
        return;
    }
    clearProgress();
    setProgress(0, adminJobs.size() + personJobs.size() + (orphans.isEmpty() ? 0 : 1));
    m_removed.clear();
    m_pre = -1;
    setStage(QStringLiteral("clearing"));

    const auto person = [this, personJobs](const QString &adminOutput) {
        if (personJobs.isEmpty()) {
            runPerson(adminOutput);
            return;
        }
        run(asPerson(m_personScript, personJobs), 30 * 60000,
            [this, adminOutput](int, const QString &output) { runPerson(adminOutput + QLatin1Char('\n') + output); },
            [this](const QString &line) { eachLine(line); });
    };
    if (adminJobs.isEmpty() && orphans.isEmpty()) {
        person(QString());
        return;
    }
    setNow(QStringLiteral("Waiting for your password"));
    QStringList arguments{productName(), m_look.snapPac ? QStringLiteral("0") : QStringLiteral("1")};
    arguments << adminJobs << QStringLiteral("--") << orphans;
    // Never stopped by a timer: it may be removing packages.
    run(asAdmin(QStringLiteral("free-space"), arguments), -1,
        [this, person](int exitCode, const QString &output) {
            if (exitCode != 0) {
                m_why = whyFailed(output, exitCode, QStringLiteral("Freeing up space"));
                finish(false, output);
                return;
            }
            person(output);
        },
        [this](const QString &line) { eachLine(line); });
}

void SpaceCleanup::eachLine(const QString &line)
{
    appendLog(line);
    static const QRegularExpression pre(QStringLiteral("^split-rock-pre=(\\d+)"));
    static const QRegularExpression removed(QStringLiteral("^split-rock-removed=(.*)"));
    if (line.startsWith(QLatin1String("@@ step "))) {
        setProgress(done() + 1, total());
        setNow(doing(line.mid(8).trimmed()));
    } else if (const QRegularExpressionMatch m = pre.match(line); m.hasMatch()) {
        m_pre = m.captured(1).toInt();
    } else if (const QRegularExpressionMatch m = removed.match(line); m.hasMatch()) {
        m_removed = m.captured(1).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    } else {
        Q_EMIT progressChanged();
    }
}

void SpaceCleanup::runPerson(const QString &output)
{
    setNow(QStringLiteral("Measuring what came free"));
    run(asPerson(m_freeScript), 15000, [this, output](int, const QString &free) {
        bool ok = false;
        const qint64 after = free.trimmed().toLongLong(&ok);
        const qint64 kb = ok && m_look.freeKb >= 0 && after > m_look.freeKb ? after - m_look.freeKb : chosenKb();
        m_freed = size(kb);
        // Restore points keep their own copy of what they saw, so what was
        // cleared under them comes free only as the computer clears them.
        m_heldBack = ok && kb * 10 < chosenKb() * 7
            ? QStringLiteral("Some of it is still held by restore points, which keep their own copy of what they saw. "
                             "It comes free as the computer clears old ones by itself.")
            : QString();
        if (ok)
            m_look.freeKb = after;
        finish(true, output);
    });
}

void SpaceCleanup::finish(bool ok, const QString &output)
{
    setNow(QString());
    setStage(ok ? QStringLiteral("done") : QStringLiteral("failed"));
    Q_EMIT changed();
    QString tail = output;
    if (tail.size() > 2000)
        tail = QStringLiteral("...") + tail.right(2000);
    report(std::exchange(m_id, QString()), ok,
           ok ? QStringLiteral("Freed up %1").arg(m_freed) : QStringLiteral("Freeing up space didn't finish"),
           QStringLiteral("%1 ran its own Free up space card, which the person started, clearing: %2. %3\n"
                          "Removed packages: %4.\nThe end of the output:\n%5\n\n"
                          "Tell the person in a sentence how it went. Don't propose clearing these again.")
               .arg(productName(), m_chosenTitles.join(QStringLiteral(", ")),
                    ok ? QStringLiteral("It finished; %1 came free.").arg(m_freed) : QStringLiteral("It failed: %1").arg(m_why),
                    m_removed.isEmpty() ? QStringLiteral("none") : m_removed.join(QStringLiteral(", ")),
                    tail.isEmpty() ? QStringLiteral("(none)") : tail));
}

} // namespace splitrock
