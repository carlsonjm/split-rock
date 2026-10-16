// SPDX-License-Identifier: GPL-2.0-or-later
#include "app/CommunityApps.h"

#include "app/Product.h"

#include <QDebug>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>
#include <utility>

namespace splitrock {

namespace {

// Run as the person: what came from outside the system's shelf, and what the
// community shelf says of each.
const QString LookScript = QStringLiteral(R"SH(export LANG=C LC_ALL=C
echo "@@ foreign"
pacman -Qm 2>/dev/null
echo "@@ shelf"
names=$(pacman -Qmq 2>/dev/null)
if [ -n "$names" ] && command -v curl >/dev/null 2>&1; then
    set --
    for n in $names; do set -- "$@" --data-urlencode "arg[]=$n"; done
    curl -fsS --max-time 20 -G "$@" https://aur.archlinux.org/rpc/v5/info 2>/dev/null
    echo
fi
echo "@@ end"
)SH");

// Run as the person: fetches the recipe into $1, finds the version installed
// ($2) in its history, and prints what changed since. The changes come last,
// so nothing in a recipe can pass for the lines before them.
const QString ReviewScript = QStringLiteral(R"SH(export LANG=C LC_ALL=C GIT_TERMINAL_PROMPT=0
dir=$1
installed=$2
base=$(basename "$dir")
if [ -d "$dir/.git" ]; then
    git -C "$dir" fetch -q origin 2>&1 || exit 1
else
    mkdir -p "$(dirname "$dir")" && git clone -q "https://aur.archlinux.org/$base.git" "$dir" 2>&1 || exit 1
fi
head=$(git -C "$dir" rev-parse -q --verify origin/HEAD 2>/dev/null || git -C "$dir" rev-parse -q --verify origin/master)
[ -n "$head" ] || exit 1
version() {
    git -C "$dir" show "$1:.SRCINFO" 2>/dev/null | awk -F' = ' '
        /^pkgname/ { exit }
        $1 == "\tpkgver" { pv = $2 } $1 == "\tpkgrel" { pr = $2 } $1 == "\tepoch" { ep = $2 }
        END { if (ep != "") printf "%s:", ep; print pv "-" pr }'
}
seen=
for c in $(git -C "$dir" log --format=%H "$head" -- .SRCINFO); do
    if [ "$(version "$c")" = "$installed" ]; then seen=$c; break; fi
done
echo "::split-rock:: head"
echo "$head"
echo "::split-rock:: seen"
echo "$seen"
echo "::split-rock:: srcinfo"
git -C "$dir" show "$head:.SRCINFO"
echo "::split-rock:: before"
[ -n "$seen" ] && git -C "$dir" show "$seen:.SRCINFO"
echo "::split-rock:: missing"
for d in $(git -C "$dir" show "$head:.SRCINFO" | awk -F' = ' '$1 ~ /^\t(depends|makedepends|checkdepends)(_x86_64)?$/ { print $2 }'); do
    pacman -T "$d" >/dev/null 2>&1 && continue
    if pacman -Sp --print-format %n "$d" >/dev/null 2>&1; then echo "shelf $d"; else echo "community $d"; fi
done
echo "::split-rock:: diff"
if [ -n "$seen" ]; then
    git -C "$dir" diff --no-color "$seen" "$head" -- . ':(exclude).SRCINFO'
else
    for f in $(git -C "$dir" ls-tree --name-only "$head"); do
        [ "$f" = .SRCINFO ] && continue
        echo "+++ $f"
        git -C "$dir" show "$head:$f" | sed 's/^/+/'
    done
fi
)SH");


const QRegularExpression &packageName()
{
    static const QRegularExpression name(QStringLiteral("^[a-z0-9@_+][a-z0-9@._+-]*$"));
    return name;
}

// A dependency as a recipe names it, perhaps with a version: "glib2>=2.80".
const QRegularExpression &dependency()
{
    static const QRegularExpression dep(QStringLiteral("^[a-z0-9@_+][a-z0-9@._+-]*([<>=]=?[A-Za-z0-9:._+~-]+)?$"));
    return dep;
}

// rpmvercmp, as pacman orders the parts of a version.
int compareParts(const QString &a, const QString &b)
{
    if (a == b)
        return 0;
    qsizetype i = 0;
    qsizetype j = 0;
    const auto alnum = [](QChar c) { return c.isLetterOrNumber() && c.unicode() < 128; };
    while (i < a.size() && j < b.size()) {
        const qsizetype si = i;
        const qsizetype sj = j;
        while (i < a.size() && !alnum(a.at(i)))
            ++i;
        while (j < b.size() && !alnum(b.at(j)))
            ++j;
        if (i >= a.size() || j >= b.size())
            break;
        // More separators is newer.
        if (i - si != j - sj)
            return i - si < j - sj ? -1 : 1;
        const qsizetype start1 = i;
        const qsizetype start2 = j;
        const bool number = a.at(i).isDigit();
        if (number) {
            while (i < a.size() && a.at(i).isDigit())
                ++i;
            while (j < b.size() && b.at(j).isDigit())
                ++j;
        } else {
            while (i < a.size() && a.at(i).isLetter())
                ++i;
            while (j < b.size() && b.at(j).isLetter())
                ++j;
        }
        QString one = a.mid(start1, i - start1);
        QString two = b.mid(start2, j - start2);
        // A number against letters: the number is newer.
        if (two.isEmpty())
            return number ? 1 : -1;
        if (number) {
            while (one.startsWith(QLatin1Char('0')))
                one.remove(0, 1);
            while (two.startsWith(QLatin1Char('0')))
                two.remove(0, 1);
            if (one.size() != two.size())
                return one.size() > two.size() ? 1 : -1;
        }
        const int c = QString::compare(one, two);
        if (c != 0)
            return c < 0 ? -1 : 1;
    }
    const bool endA = i >= a.size();
    const bool endB = j >= b.size();
    if (endA && endB)
        return 0;
    // Letters left never beat nothing left.
    if ((endA && !(j < b.size() && b.at(j).isLetter())) || (!endA && a.at(i).isLetter()))
        return -1;
    return 1;
}

struct Evr {
    QString epoch;
    QString version;
    QString release;
};

Evr split(const QString &full)
{
    Evr evr;
    QString rest = full;
    const qsizetype colon = rest.indexOf(QLatin1Char(':'));
    if (colon >= 0 && std::all_of(rest.cbegin(), rest.cbegin() + colon, [](QChar c) { return c.isDigit(); })) {
        evr.epoch = rest.left(colon);
        rest = rest.mid(colon + 1);
    }
    const qsizetype dash = rest.lastIndexOf(QLatin1Char('-'));
    if (dash >= 0) {
        evr.release = rest.mid(dash + 1);
        rest = rest.left(dash);
    }
    evr.version = rest;
    if (evr.epoch.isEmpty())
        evr.epoch = QStringLiteral("0");
    return evr;
}

// The hosts a .SRCINFO's sources download from.
QSet<QString> hosts(const QStringList &srcinfo)
{
    QSet<QString> found;
    static const QRegularExpression source(QStringLiteral(R"(^source(_\w+)?\s*=\s*(.+)$)"));
    for (const QString &line : srcinfo) {
        const QRegularExpressionMatch m = source.match(line.trimmed());
        if (!m.hasMatch())
            continue;
        QString url = m.captured(2).trimmed();
        if (const qsizetype named = url.indexOf(QLatin1String("::")); named >= 0)
            url = url.mid(named + 2);
        if (url.contains(QLatin1Char('+')) && url.indexOf(QLatin1Char('+')) < url.indexOf(QLatin1String("://")))
            url = url.mid(url.indexOf(QLatin1Char('+')) + 1);
        const QString host = QUrl(url).host();
        if (!host.isEmpty())
            found.insert(host);
    }
    return found;
}

bool hasInstall(const QStringList &srcinfo)
{
    return std::any_of(srcinfo.cbegin(), srcinfo.cend(),
                       [](const QString &l) { return l.trimmed().startsWith(QLatin1String("install =")); });
}

} // namespace

QVariantMap CommunityApps::App::toMap() const
{
    return {{QStringLiteral("name"), name},
            {QStringLiteral("base"), base},
            {QStringLiteral("installed"), installed},
            {QStringLiteral("latest"), latest},
            {QStringLiteral("maintainer"), maintainer},
            {QStringLiteral("orphaned"), maintainer.isEmpty()},
            {QStringLiteral("outOfDate"), outOfDate},
            {QStringLiteral("votes"), votes},
            {QStringLiteral("summary"), summary}};
}

QVariantMap CommunityApps::Review::toMap() const
{
    QVariantMap map = app.toMap();
    QVariantList flagList;
    for (const Flag &f : flags)
        flagList.append(QVariantMap{{QStringLiteral("tone"), f.tone}, {QStringLiteral("text"), f.text}});
    map.insert(QStringLiteral("flags"), flagList);
    map.insert(QStringLiteral("blockers"), blockers);
    map.insert(QStringLiteral("helpers"), helpers);
    map.insert(QStringLiteral("diff"), diff);
    map.insert(QStringLiteral("firstLook"), firstLook);
    map.insert(QStringLiteral("versionOnly"), versionOnly);
    return map;
}

CommunityApps::CommunityApps(QObject *parent)
    : Chore(parent)
    , m_recipes(QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/split-rock/aur"))
    , m_lookScript(LookScript)
    , m_reviewScript(ReviewScript)
{
}

bool CommunityApps::busy() const
{
    return stage() == QLatin1String("looking") || stage() == QLatin1String("reviewing")
        || stage() == QLatin1String("updating");
}

int CommunityApps::compareVersions(const QString &a, const QString &b)
{
    if (a == b)
        return 0;
    const Evr one = split(a);
    const Evr two = split(b);
    int c = compareParts(one.epoch, two.epoch);
    if (c == 0)
        c = compareParts(one.version, two.version);
    if (c == 0 && !one.release.isEmpty() && !two.release.isEmpty())
        c = compareParts(one.release, two.release);
    return c;
}

CommunityApps::Look CommunityApps::readLook(const QString &output)
{
    Look look;
    QMap<QString, QString> installed;
    QString section;
    QString json;
    for (const QString &line : output.split(QLatin1Char('\n'))) {
        if (line.startsWith(QLatin1String("@@ "))) {
            section = line.mid(3).trimmed();
            continue;
        }
        if (section == QLatin1String("foreign")) {
            const QStringList parts = line.trimmed().split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (parts.size() == 2)
                installed.insert(parts.at(0), parts.at(1));
        } else if (section == QLatin1String("shelf")) {
            json += line;
        }
    }
    const QJsonObject answer = QJsonDocument::fromJson(json.toUtf8()).object();
    look.reached = answer.contains(QStringLiteral("results"));
    QSet<QString> listed;
    for (const QJsonValue &v : answer.value(QStringLiteral("results")).toArray()) {
        const QJsonObject r = v.toObject();
        App app;
        app.name = r.value(QStringLiteral("Name")).toString();
        app.base = r.value(QStringLiteral("PackageBase")).toString();
        app.latest = r.value(QStringLiteral("Version")).toString();
        app.maintainer = r.value(QStringLiteral("Maintainer")).toString();
        app.summary = r.value(QStringLiteral("Description")).toString();
        app.outOfDate = !r.value(QStringLiteral("OutOfDate")).isNull() && !r.value(QStringLiteral("OutOfDate")).isUndefined();
        app.votes = r.value(QStringLiteral("NumVotes")).toInt();
        if (!installed.contains(app.name) || !packageName().match(app.base).hasMatch())
            continue;
        listed.insert(app.name);
        app.installed = installed.value(app.name);
        if (compareVersions(app.installed, app.latest) < 0)
            look.apps.append(app);
    }
    if (look.reached)
        look.unlisted = int(installed.size() - listed.size());
    std::sort(look.apps.begin(), look.apps.end(), [](const App &a, const App &b) { return a.name < b.name; });
    return look;
}

CommunityApps::Review CommunityApps::readReview(const QString &output, const App &app)
{
    Review review;
    review.app = app;
    QMap<QString, QStringList> found;
    QString section;
    QStringList diff;
    for (const QString &line : output.split(QLatin1Char('\n'))) {
        // From the changes on, every line is the recipe's.
        if (section == QLatin1String("diff")) {
            diff.append(line);
            continue;
        }
        if (line.startsWith(QLatin1String("::split-rock:: "))) {
            section = line.mid(15).trimmed();
            continue;
        }
        if (!line.trimmed().isEmpty())
            found[section].append(line);
    }
    static const QRegularExpression commit(QStringLiteral("^[0-9a-f]{40}$"));
    review.head = found.value(QStringLiteral("head")).value(0).trimmed();
    if (!commit.match(review.head).hasMatch())
        review.head.clear();
    review.firstLook = !commit.match(found.value(QStringLiteral("seen")).value(0).trimmed()).hasMatch();
    review.diff = diff.join(QLatin1Char('\n')).trimmed();

    const QStringList after = found.value(QStringLiteral("srcinfo"));
    const QStringList before = found.value(QStringLiteral("before"));

    // What it needs that isn't here.
    for (const QString &line : found.value(QStringLiteral("missing"))) {
        const QString dep = line.section(QLatin1Char(' '), 1).trimmed();
        if (!dependency().match(dep).hasMatch())
            continue;
        if (line.startsWith(QLatin1String("shelf ")))
            review.helpers.append(dep);
        else
            review.blockers.append(QStringLiteral("It needs %1 from the community shelf first, which this card doesn't "
                                                  "install for it.").arg(dep.section(QRegularExpression(QStringLiteral("[<>=]")), 0, 0)));
    }
    if (review.head.isEmpty())
        review.blockers.append(QStringLiteral("Its recipe couldn't be fetched. Check the internet connection and try again."));

    // What changed, in the recipe's own lines.
    static const QRegularExpression versionLine(QStringLiteral(R"(^[+-]\s*(pkgver|pkgrel|epoch|_[A-Za-z0-9_]*ver[A-Za-z0-9_]*)=)"));
    static const QRegularExpression sumsLine(QStringLiteral(R"(^[+-]\s*((sha\d+|md5|b2|ck)sums(_\w+)?=\(?)?\s*'?[0-9a-fA-F]{8,128}'?\s*\)?\s*$)"));
    static const QRegularExpression risky(QStringLiteral(
        R"((curl|wget)[^|#]*\|\s*(ba)?sh\b|base64\s+(-d|--decode)|\beval\s|/dev/tcp/|\bsudo\s|chmod\s+[0-7]*[4-7][0-7]{3}\b|\bnc\s+-e\b)"));
    bool onlyVersion = !review.firstLook;
    bool riskyLine = false;
    for (const QString &line : std::as_const(diff)) {
        if (line.startsWith(QLatin1String("+++")) || line.startsWith(QLatin1String("---"))
            || (!line.startsWith(QLatin1Char('+')) && !line.startsWith(QLatin1Char('-'))))
            continue;
        if (line.trimmed() == QLatin1String("+") || line.trimmed() == QLatin1String("-"))
            continue;
        if (!versionLine.match(line).hasMatch() && !sumsLine.match(line).hasMatch())
            onlyVersion = false;
        if (line.startsWith(QLatin1Char('+')) && risky.match(line).hasMatch())
            riskyLine = true;
    }
    review.versionOnly = onlyVersion;

    if (review.firstLook)
        review.flags.append({QStringLiteral("warn"), QStringLiteral("A first look: the version installed isn't in its "
                                                                    "history, so here is the whole recipe.")});
    else if (onlyVersion)
        review.flags.append({QStringLiteral("good"), QStringLiteral("Only the version and its checksums changed.")});
    else
        review.flags.append({QStringLiteral("warn"), QStringLiteral("The recipe changed beyond the version. Read the "
                                                                    "changes before updating.")});
    if (riskyLine)
        review.flags.append({QStringLiteral("bad"), QStringLiteral("A line it adds downloads and runs something, or "
                                                                   "asks for more rights. Don't update unless you know why.")});
    const QSet<QString> newHosts = hosts(after) - hosts(before);
    if (!review.firstLook && !newHosts.isEmpty()) {
        review.newHosts = QStringList(newHosts.cbegin(), newHosts.cend());
        review.newHosts.sort();
        review.flags.append({QStringLiteral("warn"), QStringLiteral("It now downloads from somewhere new: %1.")
                                                         .arg(review.newHosts.join(QStringLiteral(", ")))});
    }
    if (hasInstall(after) && (review.firstLook || !hasInstall(before)))
        review.flags.append({QStringLiteral("warn"), QStringLiteral("It runs a script as the administrator when it installs.")});
    else if (hasInstall(after) && std::any_of(diff.cbegin(), diff.cend(), [](const QString &l) {
                 return (l.startsWith(QLatin1String("+++")) || l.startsWith(QLatin1String("diff --git"))) && l.contains(QLatin1String(".install"));
             }))
        review.flags.append({QStringLiteral("warn"), QStringLiteral("The script it runs as the administrator changed.")});
    if (app.maintainer.isEmpty())
        review.flags.append({QStringLiteral("warn"), QStringLiteral("Nobody looks after it now.")});
    if (app.outOfDate)
        review.flags.append({QStringLiteral("warn"), QStringLiteral("People have marked it out of date.")});
    if (!review.helpers.isEmpty())
        review.flags.append({QStringLiteral("good"), QStringLiteral("It also installs %1 from the system's shelf.")
                                                         .arg(review.helpers.join(QStringLiteral(", ")))});
    return review;
}

QVariantList CommunityApps::appList() const
{
    QVariantList list;
    for (const App &app : m_look.apps)
        list.append(app.toMap());
    return list;
}

void CommunityApps::check()
{
    if (busy())
        return;
    m_look = Look();
    m_review = Review();
    m_why.clear();
    clearProgress();
    setStage(QStringLiteral("looking"));
    run(asPerson(m_lookScript), 60000, [this](int, const QString &output) {
        m_look = readLook(output);
        qInfo().noquote() << QStringLiteral("%1: %2 community apps have newer recipes").arg(productName()).arg(m_look.apps.size());
        if (!m_look.reached) {
            m_why = QStringLiteral("The community shelf couldn't be reached. Check the internet connection and try again.");
            setStage(QStringLiteral("failed"));
        } else {
            setStage(m_look.apps.isEmpty() ? QStringLiteral("nothing") : QStringLiteral("ready"));
        }
        Q_EMIT changed();
    });
}

void CommunityApps::look(const QString &name)
{
    if (stage() != QLatin1String("ready"))
        return;
    const auto found = std::find_if(m_look.apps.cbegin(), m_look.apps.cend(), [&name](const App &a) { return a.name == name; });
    if (found == m_look.apps.cend())
        return;
    const App app = *found;
    m_review = Review();
    m_review.app = app;
    clearProgress();
    setNow(QStringLiteral("Fetching its recipe"));
    setStage(QStringLiteral("reviewing"));
    Q_EMIT changed();
    run(asPerson(m_reviewScript, {QDir(m_recipes).filePath(app.base), app.installed}), 120000,
        [this, app](int, const QString &output) {
            m_review = readReview(output, app);
            setNow(QString());
            setStage(QStringLiteral("reviewed"));
            Q_EMIT changed();
        });
}

void CommunityApps::back()
{
    if (stage() != QLatin1String("reviewed") && stage() != QLatin1String("done") && stage() != QLatin1String("failed"))
        return;
    if (m_look.apps.isEmpty()) {
        check();
        return;
    }
    m_review = Review();
    m_why.clear();
    setStage(QStringLiteral("ready"));
    Q_EMIT changed();
}

void CommunityApps::start()
{
    if (stage() != QLatin1String("reviewed") || !m_review.blockers.isEmpty() || m_review.head.isEmpty())
        return;
    const App app = m_review.app;
    m_id = QStringLiteral("community-%1").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    QString unrecorded;
    if (!writeDown(m_id, QStringLiteral("Updated %1 from the community shelf (%2)").arg(app.name, app.latest),
                   QStringLiteral("Go back to the restore point taken just before, from the Restore points card"),
                   &unrecorded)) {
        m_why = QStringLiteral("Nothing was changed: it couldn't be written in the notebook first. %1").arg(unrecorded);
        m_id.clear();
        setStage(QStringLiteral("failed"));
        Q_EMIT changed();
        return;
    }
    clearProgress();
    setProgress(0, 2 + (m_review.helpers.isEmpty() ? 0 : 1));
    setNow(QStringLiteral("Waiting for your password"));
    setStage(QStringLiteral("updating"));
    Q_EMIT changed();

    // The recipe is built as the person the password prompt was for, exactly
    // at the commit looked at, and only what it built is installed.
    QStringList arguments{productName(), QStringLiteral("1"), QDir(m_recipes).filePath(app.base), m_review.head};
    arguments << m_review.helpers << QStringLiteral("--") << app.name;
    static const QMap<QString, QString> doing{{QStringLiteral("helpers"), QStringLiteral("Installing what it needs")},
                                              {QStringLiteral("build"), QStringLiteral("Building it from its recipe")},
                                              {QStringLiteral("install"), QStringLiteral("Installing it")}};
    // Never stopped by a timer: building can take a long while, and pacman
    // may be installing.
    run(asAdmin(QStringLiteral("community"), arguments), -1,
        [this, app](int exitCode, const QString &output) {
            if (exitCode != 0) {
                if (output.contains(QLatin1String("unknown public key")) || output.contains(QLatin1String("One or more PGP signatures could not be verified")))
                    m_why = QStringLiteral("Its maker signs the download, and this computer doesn't have their key yet. "
                                           "Nothing was installed; %1 can help.").arg(productName());
                else if (output.contains(QLatin1String("==> ERROR:")))
                    m_why = QStringLiteral("It didn't build. Nothing was installed; %1 can read what happened.").arg(productName());
                else
                    m_why = whyFailed(output, exitCode, QStringLiteral("Updating %1").arg(app.name));
            }
            finish(exitCode == 0, output);
        },
        [this](const QString &line) {
            appendLog(line);
            if (line.startsWith(QLatin1String("@@ step "))) {
                setProgress(done() + 1, total());
                setNow(doing.value(line.mid(8).trimmed()));
            } else if (line.startsWith(QLatin1String("==> "))) {
                setNow(line.mid(4).trimmed().left(80));
            } else {
                Q_EMIT progressChanged();
            }
        });
}

void CommunityApps::finish(bool ok, const QString &output)
{
    const App app = m_review.app;
    if (ok)
        m_look.apps.erase(std::remove_if(m_look.apps.begin(), m_look.apps.end(), [&app](const App &a) { return a.name == app.name; }),
                          m_look.apps.end());
    setNow(QString());
    setStage(ok ? QStringLiteral("done") : QStringLiteral("failed"));
    Q_EMIT changed();
    QString tail = output;
    if (tail.size() > 3000)
        tail = QStringLiteral("...") + tail.right(3000);
    report(std::exchange(m_id, QString()), ok,
           ok ? QStringLiteral("Updated %1").arg(app.name) : QStringLiteral("Updating %1 didn't finish").arg(app.name),
           QStringLiteral("%1 ran its own Community apps card: after reading what changed in its recipe, the person "
                          "updated %2 from %3 to %4. %5\nThe end of the output:\n%6\n\nTell the person in a sentence "
                          "how it went.")
               .arg(productName(), app.name, app.installed, app.latest,
                    ok ? QStringLiteral("It installed.") : QStringLiteral("It failed: %1").arg(m_why),
                    tail.isEmpty() ? QStringLiteral("(none)") : tail));
}

} // namespace splitrock
