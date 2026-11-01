// SPDX-License-Identifier: GPL-2.0-or-later
#include "app/AppInstall.h"

#include "app/Product.h"
#include "app/SpaceCleanup.h"
#include "core/AppStore.h"

#include <QDateTime>
#include <QDebug>
#include <QHash>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <utility>

namespace splitrock {

namespace {

// Shell, run as the person: the web apps and Android apps in their own app
// menu, as "web<TAB>desktop file id<TAB>name" and
// "android<TAB>package<TAB>name". A Chromium-family browser writes a web
// app's entry with --app-id= in its command; Waydroid writes an Android app's
// as "waydroid app launch" and its package.
#define SPLITROCK_LOCAL_APPS R"SH(for f in "${XDG_DATA_HOME:-$HOME/.local/share}"/applications/*.desktop; do
    [ -f "$f" ] || continue
    grep -qiE '^(NoDisplay|Hidden)=true' "$f" && continue
    name=$(sed -n 's/^Name=//p' "$f" | head -n 1)
    command=$(sed -n 's/^Exec=//p' "$f" | head -n 1)
    case $command in
    *--app-id=*) printf 'web\t%s\t%s\n' "${f##*/}" "$name" ;;
    *"waydroid app launch "*) printf 'android\t%s\t%s\n' "${command##*waydroid app launch }" "$name" ;;
    esac
done
)SH"

// Run as the person, with what they asked for as $1: the apps the system's
// shelf and Flathub describe for it, and what is installed already.
const QString SearchScript = QStringLiteral(R"SH(export LANG=C LC_ALL=C
echo "@@ shelf"
command -v appstreamcli >/dev/null 2>&1 && appstreamcli search -- "$1" 2>/dev/null
# A package asked for by its exact name, such as llama.cpp, which no app
# description lists, as a block shaped like appstreamcli's.
case $1 in
*[!A-Za-z0-9.+_-]*) ;;
*)
    for name in "$1" "$(printf '%s' "$1" | tr . -)"; do
        summary=$(pacman -Si -- "$name" 2>/dev/null | sed -n 's/^Description *: //p' | head -n 1)
        [ -n "$summary" ] || continue
        printf 'Identifier: %s\nName: %s\nSummary: %s\nPackage: %s\n---\n' "$name" "$name" "$summary" "$name"
        break
    done
    ;;
esac
echo "@@ flathub"
command -v flatpak >/dev/null 2>&1 && flatpak search --columns=application,name,description,remotes -- "$1" 2>/dev/null
echo "@@ installed"
pacman -Qq 2>/dev/null
command -v flatpak >/dev/null 2>&1 && flatpak list --app --columns=application 2>/dev/null
echo "@@ owned"
pacman -Qqe 2>/dev/null
command -v flatpak >/dev/null 2>&1 && flatpak list --app --columns=application,name 2>/dev/null
echo "@@ local"
)SH" SPLITROCK_LOCAL_APPS R"SH(echo "@@ end"
)SH");

// Run as the person: removes a web app, $1 its desktop file id, through the
// browser that installed it, as the browser's own uninstall does; deleting
// only the entry would see the browser write it again. Chromium asks its own
// "Remove?" in a window of its own and may stay open after, so it is left
// running, and the entry going is what says it's done.
const QString WebRemoveScript = QStringLiteral(R"SH(export LANG=C LC_ALL=C
file=${XDG_DATA_HOME:-$HOME/.local/share}/applications/$1
[ -f "$file" ] || exit 0
command=$(sed -n 's/^Exec=//p' "$file" | head -n 1)
app=$(printf '%s\n' "$command" | sed -n 's/.*--app-id=\([a-p]\{32\}\).*/\1/p')
profile=$(printf '%s\n' "$command" | sed -n 's/.*--profile-directory="\([^"]*\)".*/\1/p; t; s/.*--profile-directory=\([^ "]*\).*/\1/p')
browser=${command%% --profile-directory=*}
browser=${browser%% --app-id=*}
case $browser in
''|*[!A-Za-z0-9._/=\ -]*) browser= ;;
esac
if [ -z "$app" ] || [ -z "$browser" ]; then
    echo "The browser that installed it couldn't be told from its entry."
    exit 1
fi
set -f
set -- $browser
command -v "$1" >/dev/null 2>&1 || { echo "$1 isn't on this computer any more."; exit 1; }
echo "Asking $1 to remove it"
if [ -n "$profile" ]; then
    "$@" --profile-directory="$profile" --uninstall-app-id="$app" >/dev/null 2>&1 &
else
    "$@" --uninstall-app-id="$app" >/dev/null 2>&1 &
fi
i=0
while [ -f "$file" ] && [ $i -lt 240 ]; do sleep 1; i=$((i + 1)); done
if [ -f "$file" ]; then
    echo "The browser didn't remove it: its own window wasn't answered yes in time."
    exit 1
fi
echo "Removed"
)SH");

// Run as the person: removes an Android app, $1 its package, from Waydroid.
// Waydroid only removes one while Android runs, so Android is started for it
// if it wasn't, and stopped again after.
const QString AndroidRemoveScript = QStringLiteral(R"SH(export LANG=C LC_ALL=C
command -v waydroid >/dev/null 2>&1 || { echo "Waydroid isn't on this computer any more."; exit 1; }
running() { waydroid status 2>/dev/null | grep -q '^Session:.*RUNNING'; }
installed() { waydroid app list 2>/dev/null | grep -qx "packageName: $1"; }
started=
if ! running; then
    echo "Starting Android"
    waydroid session start >/dev/null 2>&1 &
    started=1
    i=0
    while ! running && [ $i -lt 90 ]; do sleep 1; i=$((i + 1)); done
fi
gone=
i=0
while [ $i -lt 12 ]; do
    waydroid app remove "$1" 2>&1
    sleep 2
    if ! installed "$1"; then gone=1; break; fi
    sleep 5
    i=$((i + 1))
done
entry=${XDG_DATA_HOME:-$HOME/.local/share}/applications/waydroid.$1.desktop
[ -n "$gone" ] && rm -f -- "$entry"
[ -n "$started" ] && waydroid session stop >/dev/null 2>&1
if [ -z "$gone" ]; then
    echo "Android didn't remove it."
    exit 1
fi
echo "Removed"
)SH");

// Run as the person: removes a Flatpak app, $1 its id. Flatpak asks for the
// password itself when the system's apps need it.
const QString FlathubRemoveScript = QStringLiteral(R"SH(export LANG=C LC_ALL=C
flatpak uninstall -y --noninteractive "$1" 2>&1
)SH");

// Run as the person: a Flathub app, $1 the remote and $2 its id. Flatpak asks
// for the password itself when the system's apps need it.
const QString FlathubScript = QStringLiteral(R"SH(export LANG=C LC_ALL=C
flatpak install -y --noninteractive "$1" "$2" 2>&1
)SH");

// Run as the person: what owns the app with the desktop file id $1, as
// "shelf<TAB>package<TAB>name", "flathub<TAB>id<TAB>name",
// "web<TAB>desktop file id<TAB>name" or "android<TAB>package<TAB>name", or
// nothing. The
// first copy of the file on the desktop's own search path is the one the app
// menu shows; one of the person's own that no package owns, such as a menu
// entry they edited, is passed over for the one beneath it.
const QString OwnerScript = QStringLiteral(R"SH(export LANG=C LC_ALL=C
IFS=:
for dir in "${XDG_DATA_HOME:-$HOME/.local/share}" ${XDG_DATA_DIRS:-/usr/local/share:/usr/share}; do
    file=$dir/applications/$1
    [ -f "$file" ] || continue
    name=$(sed -n 's/^Name=//p' "$file" | head -n 1)
    if [ "$dir" = "${XDG_DATA_HOME:-$HOME/.local/share}" ]; then
        command=$(sed -n 's/^Exec=//p' "$file" | head -n 1)
        case $command in
        *--app-id=*)
            printf 'web\t%s\t%s\n' "$1" "$name"
            exit 0
            ;;
        *"waydroid app launch "*)
            printf 'android\t%s\t%s\n' "${command##*waydroid app launch }" "$name"
            exit 0
            ;;
        esac
    fi
    case $file in
    */flatpak/exports/share/applications/*)
        app=${1%.desktop}
        flatpak info "$app" >/dev/null 2>&1 && printf 'flathub	%s	%s
' "$app" "$name"
        exit 0
        ;;
    esac
    package=$(pacman -Qqo "$(readlink -f "$file")" 2>/dev/null | head -n 1)
    if [ -n "$package" ]; then
        printf 'shelf	%s	%s
' "$package" "$name"
        exit 0
    fi
done
)SH");

// Run as the person: opens an installed app by its package or its Flatpak
// id, its web app's desktop file id or its Android package: $1 shelf,
// flathub, web or android, and $2 the name.
const QString OpenScript = QStringLiteral(R"SH(if [ "$1" = flathub ]; then
    exec flatpak run "$2"
fi
case $1 in
web) id=${2%.desktop} ;;
android) id=waydroid.$2 ;;
*)
    desktop=$(pacman -Qlq "$2" 2>/dev/null | grep '^/usr/share/applications/.*\.desktop$' | head -n 1)
    [ -n "$desktop" ] || exit 1
    id=$(basename "$desktop" .desktop)
    ;;
esac
if command -v kstart >/dev/null 2>&1; then exec kstart --application "$id"; fi
exec gtk-launch "$id"
)SH");

// Run as the person: what Your apps lists. Each section starts with a line
// "@@ name": the packages installed on purpose, the app menu's entries as
// "file<TAB>Name", who owns each entry, each package's size, Flathub's apps
// as "id<TAB>name<TAB>size", and the person's web apps and Android apps.
// Nothing here changes anything.
const QString YoursScript = QStringLiteral(R"SH(export LANG=C LC_ALL=C
echo "@@ explicit"
pacman -Qqe 2>/dev/null
echo "@@ menu"
for f in /usr/share/applications/*.desktop; do
    [ -f "$f" ] || continue
    grep -qiE '^(NoDisplay|Hidden)=true' "$f" && continue
    printf '%s\t%s\n' "${f##*/}" "$(sed -n 's/^Name=//p' "$f" | head -n 1)"
done
echo "@@ owners"
pacman -Qo /usr/share/applications/*.desktop 2>/dev/null
echo "@@ sizes"
pacman -Qqe 2>/dev/null | xargs -r pacman -Qi 2>/dev/null | grep -E '^(Name|Installed Size) '
echo "@@ flathub"
command -v flatpak >/dev/null 2>&1 && flatpak list --app --columns=application,name,size 2>/dev/null
echo "@@ local"
)SH" SPLITROCK_LOCAL_APPS R"SH(echo "@@ end"
)SH");

const QRegularExpression &packageName()
{
    static const QRegularExpression name(QStringLiteral("^[a-z0-9@_+][a-z0-9@._+-]*$"));
    return name;
}

const QRegularExpression &flatpakId()
{
    static const QRegularExpression id(QStringLiteral("^[A-Za-z0-9_-]+(\\.[A-Za-z0-9_-]+)+$"));
    return id;
}

const QRegularExpression &desktopFileId()
{
    static const QRegularExpression id(QStringLiteral("^[A-Za-z0-9_][A-Za-z0-9._+-]{0,200}\\.desktop$"));
    return id;
}

// A web app or an Android app, from its "web|android<TAB>id<TAB>name" line,
// or an empty App when the line is neither or its id is shaped otherwise.
AppInstall::App localApp(const QString &line)
{
    const QStringList columns = line.split(QLatin1Char('\t'));
    AppInstall::App app;
    if (columns.size() < 2)
        return app;
    const QString source = columns.at(0).trimmed();
    const QString id = columns.at(1).trimmed();
    const bool web = source == QLatin1String("web") && desktopFileId().match(id).hasMatch();
    const bool android = source == QLatin1String("android") && flatpakId().match(id).hasMatch();
    if (!web && !android)
        return app;
    app.source = source;
    app.package = id;
    app.name = columns.value(2).trimmed();
    if (app.name.isEmpty())
        app.name = web ? id.chopped(8) : id;
    app.key = source + QLatin1Char(':') + id;
    app.installed = true;
    return app;
}

} // namespace

QVariantMap AppInstall::App::toMap() const
{
    return {{QStringLiteral("key"), key},
            {QStringLiteral("name"), name},
            {QStringLiteral("summary"), summary},
            {QStringLiteral("source"), source},
            {QStringLiteral("sourceName"), source == QLatin1String("flathub")  ? QStringLiteral("Flathub")
                                           : source == QLatin1String("web")     ? QStringLiteral("Web app")
                                           : source == QLatin1String("android") ? QStringLiteral("Android app")
                                                                                : QStringLiteral("This system's shelf")},
            {QStringLiteral("package"), package},
            {QStringLiteral("installed"), installed},
            {QStringLiteral("alsoFlathub"), alsoFlathub},
            {QStringLiteral("kb"), kb},
            {QStringLiteral("size"), kb < 0 ? QString() : kb < 1024 ? QStringLiteral("Under 1 MB") : Chore::size(kb)}};
}

AppInstall::AppInstall(QObject *parent)
    : Chore(parent)
    , m_searchScript(SearchScript)
    , m_flathubScript(FlathubScript)
    , m_openScript(OpenScript)
    , m_flathubRemoveScript(FlathubRemoveScript)
    , m_webRemoveScript(WebRemoveScript)
    , m_androidRemoveScript(AndroidRemoveScript)
    , m_ownerScript(OwnerScript)
    , m_yoursScript(YoursScript)
    , m_storeProbe([] { return core::AppStore::look().missing(); })
{
}

bool AppInstall::busy() const
{
    return stage() == QLatin1String("searching") || stage() == QLatin1String("installing")
        || stage() == QLatin1String("removing") || stage() == QLatin1String("adding");
}

QVariantList AppInstall::resultList() const
{
    QVariantList list;
    for (const App &app : m_results)
        list.append(app.toMap());
    return list;
}

QList<AppInstall::App> AppInstall::readSearch(const QString &output, const QString &query)
{
    QString section;
    QStringList shelf;
    QStringList flathub;
    QStringList owned;
    QStringList local;
    QSet<QString> installed;
    for (const QString &line : output.split(QLatin1Char('\n'))) {
        if (line.startsWith(QLatin1String("@@ "))) {
            section = line.mid(3).trimmed();
            continue;
        }
        if (section == QLatin1String("shelf"))
            shelf.append(line);
        else if (section == QLatin1String("flathub"))
            flathub.append(line);
        else if (section == QLatin1String("installed") && !line.trimmed().isEmpty())
            installed.insert(line.trimmed());
        else if (section == QLatin1String("owned") && !line.trimmed().isEmpty())
            owned.append(line);
        else if (section == QLatin1String("local") && !line.trimmed().isEmpty())
            local.append(line);
    }

    QList<App> apps;
    // appstreamcli's blocks: "Identifier: org.kde.okular [desktop-application]",
    // "Name:", "Summary:", "Package:", each ended by "---".
    App app;
    bool desktopApp = false;
    const auto endBlock = [&] {
        if (desktopApp && !app.name.isEmpty() && packageName().match(app.package).hasMatch()) {
            app.source = QStringLiteral("shelf");
            app.key = QStringLiteral("shelf:") + app.package;
            app.installed = installed.contains(app.package);
            if (std::none_of(apps.cbegin(), apps.cend(), [&](const App &a) { return a.key == app.key; }))
                apps.append(app);
        }
        app = App();
        desktopApp = false;
    };
    for (const QString &line : std::as_const(shelf)) {
        const QString trimmed = line.trimmed();
        if (trimmed == QLatin1String("---")) {
            endBlock();
            continue;
        }
        const qsizetype colon = trimmed.indexOf(QLatin1Char(':'));
        if (colon < 0)
            continue;
        const QString key = trimmed.left(colon);
        const QString value = trimmed.mid(colon + 1).trimmed();
        if (key == QLatin1String("Identifier")) {
            if (!app.name.isEmpty())
                endBlock();
            desktopApp = value.contains(QLatin1String("[desktop-application]")) || !value.contains(QLatin1Char('['));
        } else if (key == QLatin1String("Name")) {
            app.name = value;
        } else if (key == QLatin1String("Summary")) {
            app.summary = value;
        } else if (key == QLatin1String("Package")) {
            app.package = value;
        }
    }
    endBlock();

    // flatpak's columns, separated by tabs: id, name, description, remotes.
    for (const QString &line : std::as_const(flathub)) {
        const QStringList columns = line.split(QLatin1Char('\t'));
        if (columns.size() < 4)
            continue;
        App f;
        f.package = columns.at(0).trimmed();
        f.name = columns.at(1).trimmed();
        f.summary = columns.at(2).trimmed();
        f.remote = columns.at(3).split(QLatin1Char(',')).value(0).trimmed();
        static const QRegularExpression remote(QStringLiteral("^[A-Za-z0-9_-]+$"));
        if (!flatpakId().match(f.package).hasMatch() || !remote.match(f.remote).hasMatch() || f.name.isEmpty())
            continue;
        f.source = QStringLiteral("flathub");
        f.key = QStringLiteral("flathub:") + f.package;
        f.installed = installed.contains(f.package);
        // The shelf's copy is kept when both have it.
        const auto same = std::find_if(apps.begin(), apps.end(), [&f](const App &a) {
            return a.source == QLatin1String("shelf") && a.name.compare(f.name, Qt::CaseInsensitive) == 0;
        });
        if (same != apps.end()) {
            same->alsoFlathub = true;
            continue;
        }
        if (std::none_of(apps.cbegin(), apps.cend(), [&f](const App &a) { return a.key == f.key; }))
            apps.append(f);
    }

    // What the person installed themselves, by its own name, where the words
    // asked for fit it: an app from outside the shelf, such as
    // main-keyboard-git, has no description to be found by.
    const QStringList words = withoutRemoveWords(query).toLower().split(QRegularExpression(QStringLiteral("\\W+")),
                                                                        Qt::SkipEmptyParts);
    const auto squeeze = [](const QString &text) {
        QString squeezed;
        for (const QChar c : text.toLower()) {
            if (c.isLetterOrNumber())
                squeezed += c;
        }
        return squeezed;
    };
    const QString asked = squeeze(words.join(QString()));
    const auto fits = [&](const QString &name) {
        if (asked.size() < 3)
            return false;
        const QString squeezed = squeeze(name);
        return squeezed.contains(asked)
            || std::all_of(words.cbegin(), words.cend(), [&](const QString &w) { return name.toLower().contains(w); });
    };
    for (const QString &line : std::as_const(owned)) {
        const QStringList columns = line.split(QLatin1Char('\t'));
        App o;
        o.installed = true;
        if (columns.size() >= 2) {
            o.package = columns.at(0).trimmed();
            o.name = columns.at(1).trimmed();
            o.source = QStringLiteral("flathub");
            if (!flatpakId().match(o.package).hasMatch() || !(fits(o.name) || fits(o.package)))
                continue;
        } else {
            o.package = line.trimmed();
            o.name = o.package;
            o.source = QStringLiteral("shelf");
            if (!packageName().match(o.package).hasMatch() || !fits(o.package))
                continue;
        }
        o.key = o.source + QLatin1Char(':') + o.package;
        o.summary = QStringLiteral("Installed on this computer");
        if (std::none_of(apps.cbegin(), apps.cend(), [&o](const App &a) { return a.key == o.key; }))
            apps.append(o);
    }
    // The person's web apps and Android apps, by the name the app menu shows.
    for (const QString &line : std::as_const(local)) {
        App o = localApp(line);
        if (o.key.isEmpty() || !fits(o.name))
            continue;
        o.summary = QStringLiteral("Installed on this computer");
        if (std::none_of(apps.cbegin(), apps.cend(), [&o](const App &a) { return a.key == o.key; }))
            apps.append(o);
    }

    // Best first: the words asked for in its name, then in its summary, then
    // the system's shelf ahead of Flathub.
    const auto score = [&words](const App &a) {
        int s = 0;
        for (const QString &w : words) {
            if (a.name.toLower().contains(w))
                s += 4;
            if (a.summary.toLower().contains(w))
                s += 2;
        }
        return s + (a.source == QLatin1String("shelf") ? 1 : 0);
    };
    std::stable_sort(apps.begin(), apps.end(), [&score](const App &a, const App &b) { return score(a) > score(b); });
    if (apps.size() > 8)
        apps.resize(8);
    return apps;
}

QString AppInstall::yoursScript()
{
    return YoursScript;
}

qint64 AppInstall::totalKb(const QList<App> &apps)
{
    qint64 kb = 0;
    for (const App &app : apps)
        kb += std::max<qint64>(app.kb, 0);
    return kb;
}

QString AppInstall::yoursSize() const
{
    return size(totalKb(m_results));
}

QList<AppInstall::App> AppInstall::readYours(const QString &output)
{
    const QMap<QString, QStringList> found = sections(output);
    const auto kbOf = [](QString text) {
        text.replace(QChar(0xA0), QLatin1Char(' ')).replace(QLatin1String("kB"), QLatin1String("KB"));
        const qint64 kb = SpaceCleanup::readSize(text);
        return kb >= 0 ? kb : text.contains(QLatin1String("byte")) ? qint64(0) : qint64(-1);
    };

    QSet<QString> explicitly;
    for (const QString &line : found.value(QStringLiteral("explicit")))
        explicitly.insert(line);
    QHash<QString, QString> menuNames;
    for (const QString &line : found.value(QStringLiteral("menu"))) {
        const qsizetype tab = line.indexOf(QLatin1Char('\t'));
        if (tab > 0)
            menuNames.insert(line.left(tab), line.mid(tab + 1).trimmed());
    }
    QHash<QString, qint64> sizes;
    QString named;
    for (const QString &line : found.value(QStringLiteral("sizes"))) {
        const QString value = line.section(QLatin1Char(':'), 1).trimmed();
        if (line.startsWith(QLatin1String("Name ")))
            named = value;
        else if (line.startsWith(QLatin1String("Installed Size ")) && !named.isEmpty())
            sizes.insert(named, kbOf(value));
    }

    QList<App> apps;
    // "/usr/share/applications/org.kde.okular.desktop is owned by okular 25.08.1-1"
    static const QRegularExpression owner(QStringLiteral(R"(^/usr/share/applications/(\S+\.desktop) is owned by (\S+) )"));
    for (const QString &line : found.value(QStringLiteral("owners"))) {
        const QRegularExpressionMatch m = owner.match(line);
        if (!m.hasMatch())
            continue;
        const QString file = m.captured(1);
        App app;
        app.package = m.captured(2);
        app.name = menuNames.value(file);
        // An entry the menu hides, or a package that came with another
        // rather than on purpose, isn't one the person added.
        if (app.name.isEmpty() || !explicitly.contains(app.package) || !packageName().match(app.package).hasMatch()
            || keptBySystem(app.package))
            continue;
        app.source = QStringLiteral("shelf");
        app.key = QStringLiteral("shelf:") + app.package;
        if (std::any_of(apps.cbegin(), apps.cend(), [&app](const App &a) { return a.key == app.key; }))
            continue;
        app.installed = true;
        app.kb = sizes.value(app.package, -1);
        apps.append(app);
    }
    for (const QString &line : found.value(QStringLiteral("flathub"))) {
        const QStringList columns = line.split(QLatin1Char('\t'));
        if (columns.size() < 2 || !flatpakId().match(columns.at(0).trimmed()).hasMatch())
            continue;
        App app;
        app.package = columns.at(0).trimmed();
        app.name = columns.at(1).trimmed();
        if (app.name.isEmpty())
            app.name = app.package;
        app.source = QStringLiteral("flathub");
        app.key = QStringLiteral("flathub:") + app.package;
        app.installed = true;
        app.kb = kbOf(columns.value(2));
        if (std::none_of(apps.cbegin(), apps.cend(), [&app](const App &a) { return a.key == app.key; }))
            apps.append(app);
    }
    // Web apps and Android apps take room the list doesn't know; they sort
    // with the other unknown sizes, by name.
    for (const QString &line : found.value(QStringLiteral("local"))) {
        const App app = localApp(line);
        if (app.key.isEmpty() || (app.source == QLatin1String("android") && keptByAndroid(app.package)))
            continue;
        if (std::none_of(apps.cbegin(), apps.cend(), [&app](const App &a) { return a.key == app.key; }))
            apps.append(app);
    }
    // Biggest first; an unknown size last, then by name.
    std::stable_sort(apps.begin(), apps.end(), [](const App &a, const App &b) {
        if (a.kb != b.kb)
            return a.kb > b.kb;
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    return apps;
}

void AppInstall::listYours()
{
    if (busy())
        return;
    leaveStore();
    m_removing = true;
    m_yours = true;
    m_query.clear();
    m_results.clear();
    m_picked = App();
    m_why.clear();
    clearProgress();
    setStage(QStringLiteral("searching"));
    Q_EMIT changed();
    run(asPerson(m_yoursScript), 120000, [this](int, const QString &output) {
        m_results = readYours(output);
        qInfo().noquote() << QStringLiteral("%1: %2 apps added, %3").arg(productName()).arg(m_results.size()).arg(yoursSize());
        setStage(QStringLiteral("results"));
        Q_EMIT changed();
    });
}

bool AppInstall::asksToRemove(const QString &words)
{
    static const QRegularExpression remove(QStringLiteral(
        "\\b(uninstall|remove|delete|get rid of|take off|deinstall)\\b"), QRegularExpression::CaseInsensitiveOption);
    return remove.match(words).hasMatch();
}

QString AppInstall::withoutRemoveWords(const QString &words)
{
    static const QRegularExpression filler(QStringLiteral(
        "\\b((from|on|off) (this|my|the) (computer|laptop|tablet|device|pc|system|machine)|"
        "please|pls|kindly|can you|could you|would you|will you|go ahead and|i want to|i'd like to|i need to|help me|"
        "uninstall|remove|delete|get rid of|take off|deinstall|completely|entirely|for me|for us|for good|now|just|"
        "thanks|thank you|the|an|a|my|this|that|it|app|apps|program|programs|application)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    QString left = words;
    left.replace(filler, QStringLiteral(" "));
    left.remove(QRegularExpression(QStringLiteral("[?!.,]")));
    return left.simplified();
}

bool AppInstall::keptBySystem(const QString &package)
{
    static const QSet<QString> kept{
        QStringLiteral("base"), QStringLiteral("filesystem"), QStringLiteral("glibc"), QStringLiteral("bash"),
        QStringLiteral("coreutils"), QStringLiteral("pacman"), QStringLiteral("sudo"), QStringLiteral("dbus"),
        QStringLiteral("polkit"), QStringLiteral("mesa"), QStringLiteral("snapper"), QStringLiteral("btrfs-progs"),
        QStringLiteral("networkmanager"), QStringLiteral("grub"), QStringLiteral("plasma-desktop"),
        QStringLiteral("plasma-workspace"), QStringLiteral("plasma-meta"), QStringLiteral("plasma"),
        QStringLiteral("kwin"), QStringLiteral("sddm"), QStringLiteral("plasma-login-manager"), QStringLiteral("pipewire"),
        QStringLiteral("wireplumber"), QStringLiteral("flatpak"), QStringLiteral("kadunce"), QStringLiteral("tettegouche"),
        QStringLiteral("temperance"), QStringLiteral("gooseberry"), QStringLiteral("konsole"), QStringLiteral("dolphin"),
        QStringLiteral("systemsettings"), QStringLiteral("kinfocenter"), QStringLiteral("plasma-systemmonitor")};
    static const QRegularExpression family(QStringLiteral(
        "^(linux|linux-.*|systemd.*|limine.*|cachyos-.*|split-rock.*|shuffle.*|pipewire-.*|xorg-server.*|"
        "xdg-desktop-portal.*|qt6-.*|kf6.*|nvidia.*|.*-firmware|base-.*|plasma.*|kwin.*|breeze.*|kde-.*|kscreen.*|"
        "libplasma.*|polkit-.*|sddm-.*)$"));
    return kept.contains(package) || family.match(package).hasMatch();
}

bool AppInstall::keptByAndroid(const QString &package)
{
    static const QRegularExpression own(QStringLiteral(
        "^(android|com\\.android\\..*|org\\.lineageos\\..*|com\\.lineageos\\..*|lineageos\\..*|id\\.waydro\\..*)$"));
    return own.match(package).hasMatch();
}

void AppInstall::askRemove()
{
    if (busy())
        return;
    ask();
    m_removing = true;
    Q_EMIT changed();
}

void AppInstall::removeApp(const QString &desktopId)
{
    if (busy())
        return;
    leaveStore();
    static const QRegularExpression shape(QStringLiteral("^[A-Za-z0-9_][A-Za-z0-9._+-]{0,200}\\.desktop$"));
    if (!shape.match(desktopId).hasMatch()) {
        askRemove();
        return;
    }
    m_removing = true;
    m_yours = false;
    m_query = desktopId.chopped(8);
    m_results.clear();
    m_picked = App();
    m_why.clear();
    clearProgress();
    setStage(QStringLiteral("searching"));
    Q_EMIT changed();
    run(asPerson(m_ownerScript, {desktopId}), 60000, [this, desktopId](int, const QString &output) {
        const QStringList columns = output.trimmed().split(QLatin1Char('\n')).value(0).split(QLatin1Char('\t'));
        App app;
        if (columns.size() >= 2) {
            app.source = columns.at(0);
            app.package = columns.at(1).trimmed();
            app.name = columns.value(2).trimmed();
        }
        const bool shelf = app.source == QLatin1String("shelf") && packageName().match(app.package).hasMatch();
        const bool flathub = app.source == QLatin1String("flathub") && flatpakId().match(app.package).hasMatch();
        const bool web = app.source == QLatin1String("web") && app.package == desktopId;
        const bool android = app.source == QLatin1String("android") && flatpakId().match(app.package).hasMatch();
        if (app.name.isEmpty())
            app.name = shelf || flathub || android ? app.package : m_query;
        if (android && keptByAndroid(app.package)) {
            m_picked.name = app.name;
            m_why = QStringLiteral("%1 is part of Android, so it stays.").arg(app.name);
            setStage(QStringLiteral("failed"));
        } else if (!shelf && !flathub && !web && !android) {
            m_picked.name = app.name;
            m_why = QStringLiteral("No package on this computer owns %1, so nothing was removed.").arg(app.name);
            setStage(QStringLiteral("failed"));
        } else if (shelf && keptBySystem(app.package)) {
            m_picked.name = app.name;
            m_why = QStringLiteral("%1 is part of what the system runs on, so it stays.").arg(app.name);
            setStage(QStringLiteral("failed"));
        } else {
            app.key = app.source + QLatin1Char(':') + app.package;
            app.installed = true;
            app.summary = QStringLiteral("Installed on this computer");
            m_query = app.name;
            m_results = {app};
            qInfo().noquote() << QStringLiteral("%1: removing %2 asks for %3").arg(productName(), m_query, app.key);
            askToRemove(app);
            return;
        }
        Q_EMIT changed();
    });
}

void AppInstall::askToRemove(const App &app)
{
    m_picked = app;
    if (app.source != QLatin1String("shelf")) {
        setStage(QStringLiteral("confirm"));
        Q_EMIT changed();
        return;
    }
    setStage(QStringLiteral("searching"));
    Q_EMIT changed();
    run(asPersonJob(QStringLiteral("kept"), {app.package}), 30000, [this](int exitCode, const QString &output) {
        if (exitCode != 0) {
            m_why = QStringLiteral("Nothing was removed: it couldn't be checked whether the system runs on %1.").arg(m_picked.name);
            setStage(QStringLiteral("failed"));
        } else if (output.contains(QLatin1String("@@ kept ")) || keptBySystem(m_picked.package)) {
            m_why = QStringLiteral("%1 is part of what the system runs on, so it stays.").arg(m_picked.name);
            m_results.removeIf([this](const App &a) { return a.key == m_picked.key; });
            setStage(QStringLiteral("failed"));
        } else {
            setStage(QStringLiteral("confirm"));
        }
        Q_EMIT changed();
    });
}

void AppInstall::ask()
{
    if (busy())
        return;
    leaveStore();
    m_removing = false;
    m_yours = false;
    m_query.clear();
    m_results.clear();
    m_picked = App();
    m_why.clear();
    clearProgress();
    setStage(QStringLiteral("asking"));
    Q_EMIT changed();
}

void AppInstall::search(const QString &query)
{
    if (busy())
        return;
    const bool removing = m_removing || asksToRemove(query);
    const QString words = (removing ? withoutRemoveWords(query) : query.simplified()).left(80);
    if (words.isEmpty()) {
        removing ? askRemove() : ask();
        return;
    }
    leaveStore();
    // Without the app store, only the system's own shelf is searched; the
    // results say so, and offer it.
    m_storeMissing = m_storeProbe && m_storeProbe();
    m_removing = removing;
    m_yours = false;
    m_query = words;
    m_results.clear();
    m_picked = App();
    m_why.clear();
    clearProgress();
    setStage(QStringLiteral("searching"));
    run(asPerson(m_searchScript, {words}), 60000, [this](int, const QString &output) {
        m_results = readSearch(output, m_query);
        // Only what is installed can be removed, and never what the system
        // runs on.
        if (m_removing)
            m_results.removeIf([](const App &a) {
                return !a.installed || (a.source == QLatin1String("android") ? keptByAndroid(a.package) : keptBySystem(a.package));
            });
        qInfo().noquote() << QStringLiteral("%1: %2 apps found").arg(productName()).arg(m_results.size());
        setStage(QStringLiteral("results"));
        Q_EMIT changed();
    });
}

void AppInstall::install(const QString &key)
{
    if (stage() != QLatin1String("results"))
        return;
    const auto found = std::find_if(m_results.cbegin(), m_results.cend(), [&key](const App &a) { return a.key == key; });
    if (found == m_results.cend() || found->installed)
        return;
    m_picked = *found;
    const bool shelf = m_picked.source == QLatin1String("shelf");
    m_id = QStringLiteral("install-%1").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    QString unrecorded;
    const QString undo = shelf ? QStringLiteral("Remove the package %1, from Discover or with Remove an app").arg(m_picked.package)
                               : QStringLiteral("Remove %1 from Discover's Installed page").arg(m_picked.name);
    if (!writeDown(m_id, QStringLiteral("Installed %1").arg(m_picked.name), undo, &unrecorded)) {
        m_why = QStringLiteral("Nothing was changed: it couldn't be written in the notebook first. %1").arg(unrecorded);
        m_id.clear();
        setStage(QStringLiteral("failed"));
        Q_EMIT changed();
        return;
    }
    clearProgress();
    setNow(shelf ? QStringLiteral("Waiting for your password") : QStringLiteral("Getting it from Flathub"));
    setStage(QStringLiteral("installing"));
    Q_EMIT changed();

    const auto eachLine = [this](const QString &line) {
        appendLog(line);
        const Step step = pacmanStep(line, done(), total());
        setProgress(done() + (step.counts ? 1 : 0), step.total > 0 ? step.total : total());
        if (!step.now.isEmpty())
            setNow(step.now);
        else if (line.contains(QLatin1String("Installing")) || line.contains(QLatin1String("Downloading")))
            setNow(line.trimmed().left(80));
    };
    const auto finished = [this](int exitCode, const QString &output) {
        if (exitCode != 0)
            m_why = whyFailed(output, exitCode, QStringLiteral("Installing %1").arg(m_picked.name));
        finish(exitCode == 0, output);
    };
    if (shelf) {
        run(asAdmin(QStringLiteral("install"), {productName(), QStringLiteral("1"), m_picked.package}), -1, finished,
            eachLine);
    } else {
        run(asPerson(m_flathubScript, {m_picked.remote, m_picked.package}), 60 * 60000, finished, eachLine);
    }
}

void AppInstall::finish(bool ok, const QString &output)
{
    if (ok) {
        m_picked.installed = true;
        for (App &app : m_results) {
            if (app.key == m_picked.key)
                app.installed = true;
        }
    }
    setNow(QString());
    setStage(ok ? QStringLiteral("done") : QStringLiteral("failed"));
    Q_EMIT changed();
    QString tail = output;
    if (tail.size() > 2000)
        tail = QStringLiteral("...") + tail.right(2000);
    report(std::exchange(m_id, QString()), ok,
           ok ? QStringLiteral("Installed %1").arg(m_picked.name) : QStringLiteral("Installing %1 didn't finish").arg(m_picked.name),
           QStringLiteral("%1 ran its own Install an app card: the person asked for \"%2\" and picked %3 from %4 (%5). %6\n"
                          "The end of the output:\n%7\n\nTell the person in a sentence how it went.")
               .arg(productName(), m_query, m_picked.name, m_picked.toMap().value(QStringLiteral("sourceName")).toString(),
                    m_picked.package, ok ? QStringLiteral("It installed.") : QStringLiteral("It failed: %1").arg(m_why),
                    tail.isEmpty() ? QStringLiteral("(none)") : tail));
}

void AppInstall::remove(const QString &key)
{
    if (stage() != QLatin1String("results"))
        return;
    const auto found = std::find_if(m_results.cbegin(), m_results.cend(), [&key](const App &a) { return a.key == key; });
    if (found == m_results.cend() || !found->installed || keptBySystem(found->package))
        return;
    askToRemove(*found);
}

void AppInstall::back()
{
    if (stage() != QLatin1String("confirm"))
        return;
    m_picked = App();
    setStage(QStringLiteral("results"));
    Q_EMIT changed();
}

void AppInstall::confirmRemove()
{
    if (stage() != QLatin1String("confirm"))
        return;
    const bool shelf = m_picked.source == QLatin1String("shelf");
    const bool web = m_picked.source == QLatin1String("web");
    const bool android = m_picked.source == QLatin1String("android");
    m_id = QStringLiteral("remove-%1").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    QString unrecorded;
    const QString undo = shelf     ? QStringLiteral("Install %1 again, or go back to the restore point taken before").arg(m_picked.package)
                         : web     ? QStringLiteral("Install %1 again from its website, in the browser's menu").arg(m_picked.name)
                         : android ? QStringLiteral("Install %1 again from Android's app store").arg(m_picked.name)
                                   : QStringLiteral("Install %1 again from Flathub").arg(m_picked.name);
    if (!writeDown(m_id, QStringLiteral("Removed %1").arg(m_picked.name), undo, &unrecorded)) {
        m_why = QStringLiteral("Nothing was changed: it couldn't be written in the notebook first. %1").arg(unrecorded);
        m_id.clear();
        setStage(QStringLiteral("failed"));
        Q_EMIT changed();
        return;
    }
    clearProgress();
    setNow(shelf ? QStringLiteral("Waiting for your password")
           : web ? QStringLiteral("Say yes in the browser's own window")
                 : QStringLiteral("Removing it"));
    setStage(QStringLiteral("removing"));
    Q_EMIT changed();

    const auto eachLine = [this](const QString &line) {
        appendLog(line);
        const Step step = pacmanStep(line, done(), total());
        setProgress(done() + (step.counts ? 1 : 0), step.total > 0 ? step.total : total());
        if (!step.now.isEmpty())
            setNow(step.now);
    };
    const auto finished = [this](int exitCode, const QString &output) {
        if (exitCode != 0)
            m_why = whyFailed(output, exitCode, QStringLiteral("Removing %1").arg(m_picked.name));
        finishRemoving(exitCode == 0, output);
    };
    if (shelf) {
        // Only the app and its settings files under /etc; what came with it
        // stays, for Free up space to list. Anything another app needs stops
        // it, and nothing is removed.
        run(asAdmin(QStringLiteral("remove"), {productName(), QStringLiteral("1"), m_picked.package}), -1, finished,
            eachLine);
    } else if (web) {
        run(asPerson(m_webRemoveScript, {m_picked.package}), 5 * 60000, finished, eachLine);
    } else if (android) {
        run(asPerson(m_androidRemoveScript, {m_picked.package}), 10 * 60000, finished, eachLine);
    } else {
        run(asPerson(m_flathubRemoveScript, {m_picked.package}), 30 * 60000, finished, eachLine);
    }
}

void AppInstall::finishRemoving(bool ok, const QString &output)
{
    if (ok)
        m_results.removeIf([this](const App &a) { return a.key == m_picked.key; });
    setNow(QString());
    setStage(ok ? QStringLiteral("removed") : QStringLiteral("failed"));
    Q_EMIT changed();
    QString tail = output;
    if (tail.size() > 2000)
        tail = QStringLiteral("...") + tail.right(2000);
    report(std::exchange(m_id, QString()), ok,
           ok ? QStringLiteral("Removed %1").arg(m_picked.name) : QStringLiteral("Removing %1 didn't finish").arg(m_picked.name),
           QStringLiteral("%1 ran its own card to remove an app: the person asked to remove \"%2\" and picked %3 (%4). %5\n"
                          "The end of the output:\n%6\n\nTell the person in a sentence how it went.")
               .arg(productName(), m_query, m_picked.name, m_picked.package,
                    ok ? QStringLiteral("It was removed.") : QStringLiteral("It failed: %1").arg(m_why),
                    tail.isEmpty() ? QStringLiteral("(none)") : tail));
}

void AppInstall::openApp(const QString &key)
{
    App app = m_picked;
    if (!key.isEmpty()) {
        const auto found = std::find_if(m_results.cbegin(), m_results.cend(), [&key](const App &a) { return a.key == key; });
        if (found == m_results.cend())
            return;
        app = *found;
    }
    if (!app.installed || app.package.isEmpty())
        return;
    QStringList program = asPerson(m_openScript, {app.source, app.package});
    QProcess::startDetached(program.takeFirst(), program);
}

void AppInstall::leaveStore()
{
    m_store = false;
}

void AppInstall::askStore()
{
    if (busy())
        return;
    m_store = true;
    m_removing = false;
    m_yours = false;
    m_query.clear();
    m_results.clear();
    m_picked = App();
    m_why.clear();
    clearProgress();
    m_storeMissing = m_storeProbe && m_storeProbe();
    setStage(QStringLiteral("store"));
    Q_EMIT changed();
}

void AppInstall::addStore()
{
    if (!m_store || stage() != QLatin1String("store") || !m_storeMissing)
        return;
    m_id = QStringLiteral("appstore-%1").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    QString unrecorded;
    if (!writeDown(m_id, QStringLiteral("Added the app store"),
                   QStringLiteral("Remove Discover with Remove an app, or go back to the restore point taken before"),
                   &unrecorded)) {
        m_why = QStringLiteral("Nothing was changed: it couldn't be written in the notebook first. %1").arg(unrecorded);
        m_id.clear();
        setStage(QStringLiteral("failed"));
        Q_EMIT changed();
        return;
    }
    clearProgress();
    setNow(QStringLiteral("Waiting for your password"));
    setStage(QStringLiteral("adding"));
    Q_EMIT changed();
    const auto eachLine = [this](const QString &line) {
        appendLog(line);
        const Step step = pacmanStep(line, done(), total());
        setProgress(done() + (step.counts ? 1 : 0), step.total > 0 ? step.total : total());
        if (!step.now.isEmpty())
            setNow(step.now);
        else if (line.startsWith(QLatin1String("Fetching the package lists")))
            setNow(QStringLiteral("Fetching the list of apps"));
        else if (line.contains(QLatin1String("flathub")))
            setNow(QStringLiteral("Adding Flathub's apps"));
    };
    run(asAdmin(QStringLiteral("appstore"), {productName(), QStringLiteral("1")}), -1,
        [this](int exitCode, const QString &output) {
            if (exitCode != 0)
                m_why = whyFailed(output, exitCode, QStringLiteral("Adding the app store"));
            finishStore(exitCode == 0, output);
        },
        eachLine);
}

void AppInstall::finishStore(bool ok, const QString &output)
{
    m_storeMissing = m_storeProbe && m_storeProbe();
    setNow(QString());
    setStage(ok ? QStringLiteral("added") : QStringLiteral("failed"));
    Q_EMIT changed();
    QString tail = output;
    if (tail.size() > 2000)
        tail = QStringLiteral("...") + tail.right(2000);
    report(std::exchange(m_id, QString()), ok,
           ok ? QStringLiteral("Added the app store") : QStringLiteral("Adding the app store didn't finish"),
           QStringLiteral("%1 ran its own Add the app store card: Discover and Flatpak from the system's shelf, then "
                          "Flathub. %2\nThe end of the output:\n%3\n\nTell the person in a sentence how it went.")
               .arg(productName(), ok ? QStringLiteral("It finished.") : QStringLiteral("It failed: %1").arg(m_why),
                    tail.isEmpty() ? QStringLiteral("(none)") : tail));
}

void AppInstall::openStore()
{
    QProcess::startDetached(QStringLiteral("plasma-discover"), {});
}

} // namespace splitrock
