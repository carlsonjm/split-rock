// SPDX-License-Identifier: GPL-2.0-or-later
#include "app/AppInstall.h"

#include "app/Product.h"

#include <QDateTime>
#include <QDebug>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <utility>

namespace splitrock {

namespace {

// Run as the person, with what they asked for as $1: the apps the system's
// shelf and Flathub describe for it, and what is installed already.
const QString SearchScript = QStringLiteral(R"SH(export LANG=C LC_ALL=C
echo "@@ shelf"
command -v appstreamcli >/dev/null 2>&1 && appstreamcli search -- "$1" 2>/dev/null
echo "@@ flathub"
command -v flatpak >/dev/null 2>&1 && flatpak search --columns=application,name,description,remotes -- "$1" 2>/dev/null
echo "@@ installed"
pacman -Qq 2>/dev/null
command -v flatpak >/dev/null 2>&1 && flatpak list --app --columns=application 2>/dev/null
echo "@@ owned"
pacman -Qqe 2>/dev/null
command -v flatpak >/dev/null 2>&1 && flatpak list --app --columns=application,name 2>/dev/null
echo "@@ end"
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
// "shelf<TAB>package<TAB>name" or "flathub<TAB>id<TAB>name", or nothing. The
// first copy of the file on the desktop's own search path is the one the app
// menu shows; one of the person's own that no package owns, such as a menu
// entry they edited, is passed over for the one beneath it.
const QString OwnerScript = QStringLiteral(R"SH(export LANG=C LC_ALL=C
IFS=:
for dir in "${XDG_DATA_HOME:-$HOME/.local/share}" ${XDG_DATA_DIRS:-/usr/local/share:/usr/share}; do
    file=$dir/applications/$1
    [ -f "$file" ] || continue
    name=$(sed -n 's/^Name=//p' "$file" | head -n 1)
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
// id, $1 shelf or flathub and $2 the name.
const QString OpenScript = QStringLiteral(R"SH(if [ "$1" = flathub ]; then
    exec flatpak run "$2"
fi
desktop=$(pacman -Qlq "$2" 2>/dev/null | grep '^/usr/share/applications/.*\.desktop$' | head -n 1)
[ -n "$desktop" ] || exit 1
id=$(basename "$desktop" .desktop)
if command -v kstart >/dev/null 2>&1; then exec kstart --application "$id"; fi
exec gtk-launch "$id"
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

} // namespace

QVariantMap AppInstall::App::toMap() const
{
    return {{QStringLiteral("key"), key},
            {QStringLiteral("name"), name},
            {QStringLiteral("summary"), summary},
            {QStringLiteral("source"), source},
            {QStringLiteral("sourceName"), source == QLatin1String("flathub") ? QStringLiteral("Flathub")
                                                                               : QStringLiteral("This system's shelf")},
            {QStringLiteral("package"), package},
            {QStringLiteral("installed"), installed},
            {QStringLiteral("alsoFlathub"), alsoFlathub}};
}

AppInstall::AppInstall(QObject *parent)
    : Chore(parent)
    , m_searchScript(SearchScript)
    , m_flathubScript(FlathubScript)
    , m_openScript(OpenScript)
    , m_flathubRemoveScript(FlathubRemoveScript)
    , m_ownerScript(OwnerScript)
{
}

bool AppInstall::busy() const
{
    return stage() == QLatin1String("searching") || stage() == QLatin1String("installing")
        || stage() == QLatin1String("removing");
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
    static const QRegularExpression shape(QStringLiteral("^[A-Za-z0-9_][A-Za-z0-9._+-]{0,200}\\.desktop$"));
    if (!shape.match(desktopId).hasMatch()) {
        askRemove();
        return;
    }
    m_removing = true;
    m_query = desktopId.chopped(8);
    m_results.clear();
    m_picked = App();
    m_why.clear();
    clearProgress();
    setStage(QStringLiteral("searching"));
    Q_EMIT changed();
    run(asPerson(m_ownerScript, {desktopId}), 60000, [this](int, const QString &output) {
        const QStringList columns = output.trimmed().split(QLatin1Char('\n')).value(0).split(QLatin1Char('\t'));
        App app;
        if (columns.size() >= 2) {
            app.source = columns.at(0);
            app.package = columns.at(1).trimmed();
            app.name = columns.value(2).trimmed();
        }
        const bool shelf = app.source == QLatin1String("shelf") && packageName().match(app.package).hasMatch();
        const bool flathub = app.source == QLatin1String("flathub") && flatpakId().match(app.package).hasMatch();
        if (app.name.isEmpty())
            app.name = shelf || flathub ? app.package : m_query;
        if (!shelf && !flathub) {
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
    m_removing = false;
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
    m_removing = removing;
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
            m_results.removeIf([](const App &a) { return !a.installed || keptBySystem(a.package); });
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
    m_id = QStringLiteral("remove-%1").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    QString unrecorded;
    const QString undo = shelf ? QStringLiteral("Install %1 again, or go back to the restore point taken before").arg(m_picked.package)
                               : QStringLiteral("Install %1 again from Flathub").arg(m_picked.name);
    if (!writeDown(m_id, QStringLiteral("Removed %1").arg(m_picked.name), undo, &unrecorded)) {
        m_why = QStringLiteral("Nothing was changed: it couldn't be written in the notebook first. %1").arg(unrecorded);
        m_id.clear();
        setStage(QStringLiteral("failed"));
        Q_EMIT changed();
        return;
    }
    clearProgress();
    setNow(shelf ? QStringLiteral("Waiting for your password") : QStringLiteral("Removing it"));
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

} // namespace splitrock
