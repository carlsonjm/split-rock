// SPDX-License-Identifier: GPL-2.0-or-later
#include "app/TidyUp.h"

#include "app/Product.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QVariantMap>

#include <algorithm>
#include <utility>

namespace splitrock {

namespace {

// Run as the person, with the folders as its arguments; each section starts
// with a line "@@ name". It changes nothing. A copy is "name (N).ext" beside
// "name.ext", the same size and the same all the way through.
const QString LookScript = QStringLiteral(R"SH(export LANG=C LC_ALL=C
echo "@@ held"
for d in "$@"; do
    [ -d "$d" ] && du -sk -- "$d" 2>/dev/null | cut -f1
done
echo "@@ files"
for d in "$@"; do
    [ -d "$d" ] || continue
    find "$d" -mindepth 1 -maxdepth 1 -type f ! -name '.*' -printf '%s\t%T@\t%A@\t%p\n' 2>/dev/null
done
echo "@@ folders"
for d in "$@"; do
    [ -d "$d" ] || continue
    find "$d" -mindepth 1 -maxdepth 1 -type d ! -name '.*' 2>/dev/null | while IFS= read -r f; do
        printf '%s\t%s\t%s\n' "$(du -sk -- "$f" 2>/dev/null | cut -f1)" "$(stat -c %Y -- "$f")" "$f"
    done
done
echo "@@ same"
for d in "$@"; do
    [ -d "$d" ] || continue
    find "$d" -mindepth 1 -maxdepth 1 -type f -name '* ([0-9]*)*' 2>/dev/null | while IFS= read -r f; do
        name=${f##*/}
        orig=$(printf '%s\n' "$name" | sed -E 's/ \([0-9]+\)((\.[A-Za-z0-9]+)*)$/\1/')
        [ "$orig" = "$name" ] && continue
        o=${f%/*}/$orig
        [ -f "$o" ] || continue
        [ "$(stat -c %s -- "$f")" = "$(stat -c %s -- "$o")" ] && cmp -s -- "$f" "$o" && printf '%s\n' "$f"
    done
done
echo "@@ installed"
command -v pacman >/dev/null 2>&1 && pacman -Qq 2>/dev/null
command -v flatpak >/dev/null 2>&1 && flatpak list --app --columns=application 2>/dev/null
echo "@@ end"
)SH");

constexpr qint64 Month = 30 * 86400;

// "report.pdf" from "report (1).pdf"; empty when the name isn't a copy's.
QString originalOf(const QString &name)
{
    static const QRegularExpression copy(QStringLiteral(R"( \(\d+\)((\.[A-Za-z0-9]+)*)$)"));
    const QRegularExpressionMatch m = copy.match(name);
    if (!m.hasMatch())
        return QString();
    return name.left(m.capturedStart()) + m.captured(1);
}

bool windowsProgram(const QString &name)
{
    return name.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive) || name.endsWith(QLatin1String(".msi"), Qt::CaseInsensitive);
}

bool appImage(const QString &name)
{
    return name.endsWith(QLatin1String(".appimage"), Qt::CaseInsensitive);
}

bool screenshot(const QString &name)
{
    static const QRegularExpression shot(QStringLiteral(R"(^screen ?shot.*\.(png|jpe?g|webp)$)"),
                                         QRegularExpression::CaseInsensitiveOption);
    return shot.match(name).hasMatch();
}

// "Oct 15" from seconds since 1970.
QString day(qint64 seconds)
{
    return QLocale(QLocale::English).toString(QDateTime::fromSecsSinceEpoch(seconds).date(), QStringLiteral("MMM d"));
}

// "Zoom" from "zoom".
QString named(const QString &app)
{
    return app.left(1).toUpper() + app.mid(1);
}

// "in Downloads and on the Desktop", from where the files are.
QString where(const QList<TidyUp::File> &files)
{
    QStringList places;
    for (const TidyUp::File &file : files) {
        const QString folder = QFileInfo(file.path).absoluteDir().dirName();
        const QString place = folder == QLatin1String("Desktop") ? QStringLiteral("on the Desktop")
                                                                  : QStringLiteral("in %1").arg(folder);
        if (!places.contains(place))
            places.append(place);
    }
    places.sort();
    return places.join(QStringLiteral(" and "));
}

QString files(int count)
{
    return count == 1 ? QStringLiteral("1 file") : QStringLiteral("%1 files").arg(count);
}

} // namespace

TidyUp::TidyUp(QObject *parent)
    : Chore(parent)
    , m_lookScript(LookScript)
{
    // Downloads and the Desktop, where they are folders of their own.
    const QString home = QDir::homePath();
    for (const auto location : {QStandardPaths::DownloadLocation, QStandardPaths::DesktopLocation}) {
        const QString folder = QStandardPaths::writableLocation(location);
        if (!folder.isEmpty() && QDir(folder) != QDir(home) && !m_folders.contains(folder))
            m_folders.append(folder);
    }
}

bool TidyUp::busy() const
{
    return stage() == QLatin1String("looking") || stage() == QLatin1String("moving");
}

QString TidyUp::installerFor(const QString &fileName)
{
    const QString name = fileName.toLower();
    // pacman's own: name-version-release-arch.pkg.tar.zst
    if (name.contains(QLatin1String(".pkg.tar"))) {
        const QStringList parts = name.section(QStringLiteral(".pkg.tar"), 0, 0).split(QLatin1Char('-'));
        if (parts.size() >= 4)
            return parts.mid(0, parts.size() - 3).join(QLatin1Char('-'));
        return parts.value(0).section(QLatin1Char('_'), 0, 0);
    }
    // Debian's: name_version_arch.deb
    if (name.endsWith(QLatin1String(".deb")))
        return name.chopped(4).section(QLatin1Char('_'), 0, 0);
    // Fedora's: name-version-release.arch.rpm
    if (name.endsWith(QLatin1String(".rpm"))) {
        QStringList kept;
        for (const QString &part : name.chopped(4).split(QLatin1Char('-'))) {
            if (!part.isEmpty() && part.at(0).isDigit())
                break;
            kept.append(part);
        }
        return kept.join(QLatin1Char('-'));
    }
    // Flathub's: the app's id.
    if (name.endsWith(QLatin1String(".flatpakref")))
        return name.chopped(11);
    if (name.endsWith(QLatin1String(".flatpak")))
        return name.chopped(8);
    return QString();
}

TidyUp::Look TidyUp::readLook(const QString &output, qint64 now)
{
    const QMap<QString, QStringList> found = sections(output);
    Look look;
    for (const QString &line : found.value(QStringLiteral("held")))
        look.heldKb += std::max<qint64>(0, line.trimmed().toLongLong());

    // What is installed, by package name and by Flathub id and its last part.
    QSet<QString> installed;
    for (const QString &line : found.value(QStringLiteral("installed"))) {
        const QString name = line.trimmed().toLower();
        if (name.isEmpty())
            continue;
        installed.insert(name);
        if (name.contains(QLatin1Char('.')))
            installed.insert(name.section(QLatin1Char('.'), -1));
    }
    const QStringList sameList = found.value(QStringLiteral("same"));
    const QSet<QString> same(sameList.cbegin(), sameList.cend());

    Group installers{QStringLiteral("installers"), QStringLiteral("Installers you've used"), QString(), QStringLiteral("teal"), {}};
    Group copies{QStringLiteral("copies"), QStringLiteral("Copies"), QString(), QStringLiteral("blue"), {}};
    Group old{QStringLiteral("old"), QStringLiteral("Not opened in 30 days"), QString(), QStringLiteral("amber"), {}};
    Group shots{QStringLiteral("screenshots"), QStringLiteral("Screenshots"), QString(), QStringLiteral("pink"), {}};
    Group folders{QStringLiteral("folders"), QStringLiteral("Folders"), QString(), QStringLiteral("violet"), {}};
    bool forWindows = false;
    bool forLinux = false;

    // "BYTES<tab>MODIFIED<tab>READ<tab>PATH", the times in seconds since 1970.
    for (const QString &line : found.value(QStringLiteral("files"))) {
        const QStringList cells = line.split(QLatin1Char('\t'));
        if (cells.size() < 4)
            continue;
        const QString path = line.section(QLatin1Char('\t'), 3);
        const QString name = path.section(QLatin1Char('/'), -1);
        // A download still arriving.
        if (name.isEmpty() || name.endsWith(QLatin1String(".part")) || name.endsWith(QLatin1String(".crdownload"))
            || name.endsWith(QLatin1String(".!qB")))
            continue;
        File file;
        file.path = path;
        file.name = name;
        file.kb = (cells.at(0).toLongLong() + 1023) / 1024;
        const qint64 used = std::max<qint64>(static_cast<qint64>(cells.at(1).toDouble()), static_cast<qint64>(cells.at(2).toDouble()));
        const bool unused = used > 0 && now - used > Month;

        if (same.contains(path)) {
            file.detail = QStringLiteral("The same as %1").arg(originalOf(name));
            file.chosen = true;
            copies.files.append(file);
            continue;
        }
        if (windowsProgram(name)) {
            file.detail = QStringLiteral("Windows only");
            file.chosen = true;
            forWindows = true;
            installers.files.append(file);
            continue;
        }
        if (appImage(name) && !unused) {
            file.detail = QStringLiteral("Kept: you run it from here");
            file.kept = true;
            installers.files.append(file);
            continue;
        }
        const QString app = installerFor(name);
        if (!app.isEmpty() && (installed.contains(app) || installed.contains(app.section(QLatin1Char('.'), -1)))) {
            file.detail = QStringLiteral("%1 is installed").arg(named(app.section(QLatin1Char('.'), -1)));
            file.chosen = true;
            forLinux = true;
            installers.files.append(file);
            continue;
        }
        if (screenshot(name)) {
            file.detail = day(static_cast<qint64>(cells.at(1).toDouble()));
            shots.files.append(file);
            continue;
        }
        if (unused) {
            file.detail = QStringLiteral("Last opened %1").arg(day(used));
            old.files.append(file);
        }
    }

    // Folders inside, whole: "KB<tab>CHANGED<tab>PATH". Never ticked by
    // themselves: what is in one can't be told from its name.
    for (const QString &line : found.value(QStringLiteral("folders"))) {
        const QStringList cells = line.split(QLatin1Char('\t'));
        if (cells.size() < 3)
            continue;
        File folder;
        folder.path = line.section(QLatin1Char('\t'), 2);
        folder.name = folder.path.section(QLatin1Char('/'), -1);
        if (folder.name.isEmpty())
            continue;
        folder.kb = std::max<qint64>(0, cells.at(0).toLongLong());
        folder.detail = QStringLiteral("Folder · changed %1").arg(day(cells.at(1).toLongLong()));
        folders.files.append(folder);
    }

    // Biggest first, what is kept last.
    for (Group *group : {&installers, &copies, &old, &shots, &folders}) {
        std::stable_sort(group->files.begin(), group->files.end(), [](const File &a, const File &b) {
            return a.kept != b.kept ? !a.kept : a.kb > b.kb;
        });
    }
    const auto offered = [](const Group &group) {
        return static_cast<int>(std::count_if(group.files.cbegin(), group.files.cend(), [](const File &f) { return !f.kept; }));
    };
    {
        const int n = offered(installers);
        installers.detail = forWindows && !forLinux
            ? (n == 1 ? QStringLiteral("1 Windows program, which doesn't run here")
                      : QStringLiteral("%1 Windows programs, which don't run here").arg(n))
            : forWindows ? QStringLiteral("%1 for apps already installed, or for Windows").arg(files(n))
                         : QStringLiteral("%1 for apps already installed").arg(files(n));
        if (n == 0)
            installers.detail = QStringLiteral("Kept: apps you run from here");
    }
    if (!copies.files.isEmpty()) {
        const QString first = copies.files.first().name;
        copies.detail = copies.files.size() == 1
            ? QStringLiteral("\"%1\", the same as the one it copies").arg(first)
            : QStringLiteral("%1 like \"%2\", the same as the one they copy").arg(files(int(copies.files.size())), first);
    }
    old.detail = QStringLiteral("%1. Look before you tick").arg(files(int(old.files.size())));
    shots.detail = QStringLiteral("%1 %2").arg(shots.files.size()).arg(where(shots.files));
    folders.detail = folders.files.size() == 1 ? QStringLiteral("1 folder, with all it holds. Look before you tick")
                                               : QStringLiteral("%1 folders, each with all it holds. Look before you tick")
                                                     .arg(folders.files.size());

    for (Group *group : {&installers, &copies, &old, &shots, &folders}) {
        if (!group->files.isEmpty())
            look.groups.append(*group);
    }
    return look;
}

QVariantList TidyUp::groupList() const
{
    QVariantList list;
    for (const Group &group : m_look.groups) {
        qint64 kb = 0;
        int count = 0;
        bool chosen = false;
        QVariantList fileList;
        for (const File &file : group.files) {
            if (!file.kept) {
                kb += file.kb;
                ++count;
            }
            chosen = chosen || file.chosen;
            fileList.append(QVariantMap{{QStringLiteral("path"), file.path},
                                        {QStringLiteral("name"), file.name},
                                        {QStringLiteral("detail"), file.detail},
                                        {QStringLiteral("size"), size(file.kb)},
                                        {QStringLiteral("kb"), file.kb},
                                        {QStringLiteral("chosen"), file.chosen},
                                        {QStringLiteral("kept"), file.kept}});
        }
        list.append(QVariantMap{{QStringLiteral("id"), group.id},
                                {QStringLiteral("title"), group.title},
                                {QStringLiteral("detail"), group.detail},
                                {QStringLiteral("size"), size(kb)},
                                {QStringLiteral("kb"), kb},
                                {QStringLiteral("count"), count},
                                {QStringLiteral("chosen"), chosen},
                                {QStringLiteral("tone"), group.tone},
                                {QStringLiteral("files"), fileList}});
    }
    return list;
}

qint64 TidyUp::chosenKb() const
{
    qint64 kb = 0;
    for (const Group &group : m_look.groups) {
        for (const File &file : group.files) {
            if (file.chosen)
                kb += file.kb;
        }
    }
    return kb;
}

int TidyUp::chosenCount() const
{
    int count = 0;
    for (const Group &group : m_look.groups)
        count += static_cast<int>(std::count_if(group.files.cbegin(), group.files.cend(), [](const File &f) { return f.chosen; }));
    return count;
}

QString TidyUp::heldWords() const
{
    QStringList names;
    for (const QString &folder : m_folders) {
        if (QFileInfo(folder).isDir())
            names.append(QDir(folder).dirName());
    }
    if (names.isEmpty())
        return QString();
    return QStringLiteral("%1 %2 %3.").arg(names.join(QStringLiteral(" and ")),
                                          names.size() == 1 ? QStringLiteral("holds") : QStringLiteral("hold"), heldSize());
}

void TidyUp::check()
{
    if (busy())
        return;
    m_look = Look();
    m_moved = 0;
    m_movedKb = 0;
    m_missed.clear();
    m_why.clear();
    clearProgress();
    setStage(QStringLiteral("looking"));
    run(asPerson(m_lookScript, m_folders), 5 * 60000, [this](int, const QString &output) { looked(output); });
}

void TidyUp::looked(const QString &output)
{
    m_look = readLook(output, QDateTime::currentSecsSinceEpoch());
    qInfo().noquote() << QStringLiteral("%1: %2 groups to tidy up, %3 ticked").arg(productName()).arg(m_look.groups.size())
                             .arg(size(chosenKb()));
    setStage(m_look.groups.isEmpty() ? QStringLiteral("nothing") : QStringLiteral("ready"));
    Q_EMIT changed();
}

void TidyUp::choose(const QString &id, bool chosen)
{
    if (stage() != QLatin1String("ready"))
        return;
    for (Group &group : m_look.groups) {
        if (group.id != id)
            continue;
        for (File &file : group.files) {
            if (!file.kept || !chosen)
                file.chosen = chosen;
        }
    }
    Q_EMIT changed();
}

void TidyUp::chooseFile(const QString &path, bool chosen)
{
    if (stage() != QLatin1String("ready"))
        return;
    for (Group &group : m_look.groups) {
        for (File &file : group.files) {
            if (file.path == path)
                file.chosen = chosen;
        }
    }
    Q_EMIT changed();
}

void TidyUp::start()
{
    if (stage() != QLatin1String("ready"))
        return;
    QList<File> chosen;
    for (const Group &group : std::as_const(m_look.groups)) {
        for (const File &file : group.files) {
            if (file.chosen)
                chosen.append(file);
        }
    }
    if (chosen.isEmpty())
        return;

    QStringList names;
    for (const File &file : std::as_const(chosen))
        names.append(file.name);
    const QString id = QStringLiteral("tidy-%1").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    QString unrecorded;
    if (!writeDown(id, QStringLiteral("Tidied up Downloads: %1 to the Trash").arg(files(int(chosen.size()))),
                   QStringLiteral("Open the Trash and put back what was moved: %1").arg(names.join(QStringLiteral(", "))),
                   &unrecorded)) {
        m_why = QStringLiteral("Nothing was moved: it couldn't be written in the notebook first. %1").arg(unrecorded);
        setStage(QStringLiteral("failed"));
        return;
    }
    setStage(QStringLiteral("moving"));
    setProgress(0, int(chosen.size()));
    QStringList missed;
    for (const File &file : std::as_const(chosen)) {
        setNow(file.name);
        if (QFile::moveToTrash(file.path)) {
            ++m_moved;
            m_movedKb += file.kb;
            appendLog(QStringLiteral("Moved to the Trash: %1").arg(file.path));
        } else {
            missed.append(file.name);
            appendLog(QStringLiteral("Couldn't move: %1").arg(file.path));
        }
        setProgress(done() + 1, total());
    }
    setNow(QString());
    const bool ok = m_moved > 0;
    if (!missed.isEmpty())
        m_missed = missed.size() == 1 ? QStringLiteral("%1 couldn't be moved.").arg(missed.first())
                                      : QStringLiteral("%1 couldn't be moved.").arg(files(int(missed.size())));
    if (!ok)
        m_why = QStringLiteral("Nothing could be moved to the Trash. The files may have moved, or the Trash may be on another drive.");
    setStage(ok ? QStringLiteral("done") : QStringLiteral("failed"));
    Q_EMIT changed();
    report(id, ok, ok ? QStringLiteral("%1 in the Trash").arg(files(m_moved)) : QStringLiteral("Tidying up didn't finish"),
           QStringLiteral("%1 ran its own Tidy up Downloads card, which the person started. It moved %2 (%3) to the Trash: %4.%5\n\n"
                          "Tell the person in a sentence how it went, and that the Trash can put them back.")
               .arg(productName(), files(m_moved), size(m_movedKb), names.join(QStringLiteral(", ")),
                    missed.isEmpty() ? QString() : QStringLiteral(" Couldn't move: %1.").arg(missed.join(QStringLiteral(", ")))));
}

QStringList TidyUp::trashOpener()
{
    // Asked straight, the file manager shows it at once; xdg-open goes
    // through the desktop's lookup first. One still starting is waited for,
    // and one that took the request but hasn't answered has it, so only a
    // desktop with no file manager on the bus goes on to xdg-open.
    return {QStringLiteral("/bin/sh"), QStringLiteral("-c"),
            QStringLiteral("why=$(dbus-send --session --print-reply --reply-timeout=20000 "
                           "--dest=org.freedesktop.FileManager1 /org/freedesktop/FileManager1 "
                           "org.freedesktop.FileManager1.ShowFolders array:string:trash:/ string: 2>&1) && exit 0; "
                           "case $why in *NoReply*) exit 0;; esac; exec xdg-open trash:/")};
}

void TidyUp::openTrash()
{
    if (!m_trashOpener.isEmpty())
        QProcess::startDetached(m_trashOpener.first(), m_trashOpener.mid(1));
}

} // namespace splitrock
