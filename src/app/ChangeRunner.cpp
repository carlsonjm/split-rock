// SPDX-License-Identifier: GPL-2.0-or-later
#include "app/ChangeRunner.h"

#include "app/Product.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>

namespace splitrock {

namespace {

const QString Shell = QStringLiteral("/bin/sh");

// Run as the administrator: a snapshot before, the change, a snapshot after,
// each snapshot number printed for Split Rock to keep. $1 is the description,
// $2 the change's command.
const QString SystemChange = QStringLiteral(R"(snapshots=$3
pre=
if [ "$snapshots" = 1 ] && command -v snapper >/dev/null 2>&1 && [ -f /etc/snapper/configs/root ]; then
    pre=$(snapper -c root create --type pre --cleanup-algorithm number --print-number --description "$1") || pre=
fi
[ -n "$pre" ] && echo "split-rock-pre=$pre"
/bin/sh -c "$2"
status=$?
if [ -n "$pre" ]; then
    post=$(snapper -c root create --type post --pre-number "$pre" --cleanup-algorithm number --print-number --description "$1") \
        && echo "split-rock-post=$post"
fi
exit $status
)");

// The T keeps the digits in short runs, so the notebook's check for card
// numbers never mistakes an ID for one.
QString newId()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd'T'HHmmss"))
        + QStringLiteral("-%1").arg(QRandomGenerator::global()->bounded(0x10000), 4, 16, QLatin1Char('0'));
}

int marked(const QString &output, const QString &name)
{
    const QRegularExpression line(QStringLiteral("^%1=(\\d+)$").arg(name), QRegularExpression::MultilineOption);
    const QRegularExpressionMatch match = line.match(output);
    return match.hasMatch() ? match.captured(1).toInt() : -1;
}

QString expandHome(const QString &path)
{
    if (path == QLatin1String("~"))
        return QDir::homePath();
    if (path.startsWith(QLatin1String("~/")))
        return QDir::homePath() + path.mid(1);
    return path;
}

} // namespace

QString ChangeRunner::Record::undoWay() const
{
    if (pre >= 0 && post >= 0)
        return QStringLiteral("Put back the system files it changed, from the snapshot taken before");
    // The assistant's way back applies the old setting to the running
    // desktop, which putting a file back does not; the copies are kept for
    // when it fails.
    if (!proposal.undo.isEmpty())
        return QStringLiteral("Run: %1").arg(proposal.undo);
    if (!copies.isEmpty())
        return QStringLiteral("Put back the copies of your files made before");
    return {};
}

QJsonObject ChangeRunner::Record::toJson() const
{
    QJsonArray copied;
    for (const Copy &c : copies)
        copied.append(QJsonObject{{QStringLiteral("original"), c.original},
                                  {QStringLiteral("copy"), c.copy},
                                  {QStringLiteral("absent"), c.absent}});
    return {{QStringLiteral("id"), id},
            {QStringLiteral("made"), made.toString(Qt::ISODate)},
            {QStringLiteral("proposal"), proposal.toJson()},
            {QStringLiteral("status"), status},
            {QStringLiteral("exitCode"), exitCode},
            {QStringLiteral("output"), output},
            {QStringLiteral("checked"), checked},
            {QStringLiteral("pre"), pre},
            {QStringLiteral("post"), post},
            {QStringLiteral("copies"), copied},
            {QStringLiteral("undone"), undone.isValid() ? undone.toString(Qt::ISODate) : QString()}};
}

ChangeRunner::Record ChangeRunner::Record::fromJson(const QJsonObject &object)
{
    Record r;
    r.id = object.value(QStringLiteral("id")).toString();
    r.made = QDateTime::fromString(object.value(QStringLiteral("made")).toString(), Qt::ISODate);
    r.proposal = ChangeProposal::fromJson(QJsonDocument(object.value(QStringLiteral("proposal")).toObject()).toJson())
                     .value_or(ChangeProposal{});
    r.status = object.value(QStringLiteral("status")).toString();
    r.exitCode = object.value(QStringLiteral("exitCode")).toInt(-1);
    r.output = object.value(QStringLiteral("output")).toString();
    r.checked = object.value(QStringLiteral("checked")).toString();
    r.pre = object.value(QStringLiteral("pre")).toInt(-1);
    r.post = object.value(QStringLiteral("post")).toInt(-1);
    for (const QJsonValue &value : object.value(QStringLiteral("copies")).toArray()) {
        const QJsonObject c = value.toObject();
        r.copies.append({c.value(QStringLiteral("original")).toString(), c.value(QStringLiteral("copy")).toString(),
                         c.value(QStringLiteral("absent")).toBool()});
    }
    const QString undone = object.value(QStringLiteral("undone")).toString();
    if (!undone.isEmpty())
        r.undone = QDateTime::fromString(undone, Qt::ISODate);
    return r;
}

ChangeRunner::ChangeRunner(const QString &store, QObject *parent)
    : QObject(parent)
    , m_store(store)
{
}

QString ChangeRunner::defaultStore()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
        .filePath(QStringLiteral("split-rock/changes"));
}

bool ChangeRunner::systemSnapshots()
{
    return !QStandardPaths::findExecutable(QStringLiteral("snapper")).isEmpty()
        && QFileInfo::exists(QStringLiteral("/etc/snapper/configs/root"));
}

QString ChangeRunner::reloadFor(const ChangeProposal &change)
{
    // KWin reads kwinrc only when told to, so a setting written there, Night
    // Light among them, changes nothing until it is.
    const QString touched = change.command + QLatin1Char(' ') + change.undo + QLatin1Char(' ') + change.files.join(QLatin1Char(' '));
    if (!change.admin && touched.contains(QLatin1String("kwinrc")))
        return QStringLiteral("qdbus6 org.kde.KWin /KWin reconfigure || qdbus org.kde.KWin /KWin reconfigure");
    return {};
}

ChangeProposal ChangeRunner::withKnownUndo(const ChangeProposal &change, const Appearance &now)
{
    ChangeProposal known = change;
    if (change.admin || !Appearance::touchedBy(change.command))
        return known;
    const QString restore = now.restoreCommand();
    if (!restore.isEmpty())
        known.undo = restore;
    if (known.check.isEmpty())
        known.check = Appearance::checkCommand();
    return known;
}

QString ChangeRunner::undoPromise(const ChangeProposal &change)
{
    if (change.admin && systemSnapshots())
        return QStringLiteral("%1 takes a system snapshot first. Undo, in the notebook, puts back every system file "
                              "this changes.").arg(productName());
    if (!change.admin && Appearance::touchedBy(change.command) && !change.undo.isEmpty())
        return QStringLiteral("Undo, in the notebook, puts back the whole look you have now.");
    if (!change.undo.isEmpty())
        return QStringLiteral("Undo, in the notebook, runs the way back the assistant gave.");
    if (!change.admin && !change.files.isEmpty())
        return QStringLiteral("%1 copies your files aside first. Undo, in the notebook, puts them back.").arg(productName());
    return QStringLiteral("This can't be undone automatically. It is written in the notebook.");
}

QString ChangeRunner::unattended(const QString &command)
{
    // Each pacman or flatpak the command runs, up to the end of its own
    // words: a separator, a closing bracket or the end of the line.
    static const QRegularExpression tool(QStringLiteral(R"((^|[\s;&|(`])((?:[^\s;&|()`]*/)?(pacman|flatpak))(?=\s))"));
    static const QRegularExpression end(QStringLiteral(R"([;&|)`\n])"));
    QString result;
    qsizetype from = 0;
    for (auto it = tool.globalMatch(command); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        if (m.capturedStart(2) < from)
            continue;
        const qsizetype after = m.capturedEnd(2);
        const qsizetype stop = [&] {
            const qsizetype at = command.indexOf(end, after);
            return at < 0 ? command.size() : at;
        }();
        const QStringList words = command.mid(after, stop - after).split(QRegularExpression(QStringLiteral("\\s+")),
                                                                           Qt::SkipEmptyParts);
        QString insert;
        qsizetype at = after;
        if (m.captured(3) == QLatin1String("pacman")) {
            // Its operation, S, R or U for the ones that change packages, and
            // whether a sync only searches or lists, which asks nothing.
            QChar operation;
            bool looks = false;
            bool answered = false;
            for (const QString &w : words) {
                if (w == QLatin1String("--"))
                    break;
                if (w == QLatin1String("--noconfirm"))
                    answered = true;
                else if (w == QLatin1String("--sync"))
                    operation = QLatin1Char('S');
                else if (w == QLatin1String("--remove"))
                    operation = QLatin1Char('R');
                else if (w == QLatin1String("--upgrade"))
                    operation = QLatin1Char('U');
                else if (w.startsWith(QLatin1String("--")))
                    looks = looks || w == QLatin1String("--search") || w == QLatin1String("--info")
                        || w == QLatin1String("--list") || w == QLatin1String("--print") || w == QLatin1String("--groups");
                else if (w.size() > 1 && w.at(0) == QLatin1Char('-')) {
                    for (const QChar c : QStringView(w).mid(1)) {
                        if (c.isUpper())
                            operation = c;
                        else if (QStringLiteral("silpg").contains(c))
                            looks = true;
                    }
                }
            }
            const bool changes = operation == QLatin1Char('R') || operation == QLatin1Char('U')
                || (operation == QLatin1Char('S') && !looks);
            if (changes && !answered)
                insert = QStringLiteral(" --noconfirm");
        } else {
            // flatpak's options for a subcommand follow the subcommand.
            static const QStringList asking{QStringLiteral("install"), QStringLiteral("uninstall"), QStringLiteral("remove"),
                                            QStringLiteral("update"), QStringLiteral("upgrade")};
            const qsizetype sub = std::find_if(words.cbegin(), words.cend(), [](const QString &w) { return !w.startsWith(QLatin1Char('-')); })
                                  - words.cbegin();
            if (sub < words.size() && asking.contains(words.at(sub))
                && !words.contains(QStringLiteral("-y")) && !words.contains(QStringLiteral("--assumeyes"))) {
                at = command.indexOf(words.at(sub), after) + words.at(sub).size();
                insert = words.contains(QStringLiteral("--noninteractive")) ? QStringLiteral(" -y")
                                                                            : QStringLiteral(" -y --noninteractive");
            }
        }
        result += command.mid(from, at - from) + insert;
        from = at;
    }
    return result + command.mid(from);
}

QString ChangeRunner::make(const ChangeProposal &change, const std::function<bool(const Record &)> &before)
{
    if (busy())
        return {};
    Record record;
    record.id = newId();
    record.made = QDateTime::currentDateTime();
    record.proposal = change;
    record.status = QStringLiteral("making");
    const QDir folder(QDir(m_store).filePath(record.id));
    QDir().mkpath(folder.path());

    // The person's own files, copied aside before anything changes them.
    if (!change.admin) {
        int n = 0;
        for (const QString &named : change.files) {
            const QString original = QFileInfo(expandHome(named)).absoluteFilePath();
            const QString copy = folder.filePath(QStringLiteral("%1-%2").arg(n++).arg(QFileInfo(original).fileName()));
            if (QFileInfo(original).isFile()) {
                if (QFile::copy(original, copy))
                    record.copies.append({original, copy, false});
            } else if (!QFileInfo::exists(original)) {
                record.copies.append({original, {}, true});
            }
        }
    }
    save(record);
    if (before && !before(record)) {
        record.status = QStringLiteral("failed");
        record.output = QStringLiteral("Not made: it could not be written down first.");
        save(record);
        return {};
    }

    QStringList program;
    if (change.admin) {
        program = m_elevate;
        program << Shell << QStringLiteral("-c") << SystemChange << productName()
                << QStringLiteral("%1: %2").arg(productName(), change.title) << unattended(change.command)
                << (m_snapshots ? QStringLiteral("1") : QStringLiteral("0"));
    } else {
        program = {Shell, QStringLiteral("-c"), unattended(change.command)};
    }
    qInfo().noquote() << QStringLiteral("%1: making change %2 \"%3\" as %4: %5")
                             .arg(productName(), record.id, change.title,
                                  change.admin ? QStringLiteral("administrator") : QStringLiteral("the person"),
                                  unattended(change.command));
    run(program, change.admin ? m_timeLimit * 3 : m_timeLimit, [this, record](int exitCode, const QString &output) mutable {
        record.exitCode = exitCode;
        record.output = output;
        record.pre = marked(output, QStringLiteral("split-rock-pre"));
        record.post = marked(output, QStringLiteral("split-rock-post"));
        record.status = exitCode == 0 ? QStringLiteral("done") : QStringLiteral("failed");
        save(record);
        qInfo().noquote() << QStringLiteral("%1: change %2 %3, exit code %4: %5")
                                 .arg(productName(), record.id, record.status).arg(exitCode).arg(output.right(500));
        // The process is finished, so the reload and check run with the
        // change still counted as being made.
        m_finishing = true;
        afterChange(record.proposal, true, [this, record](const QString &checked) mutable {
            m_finishing = false;
            record.checked = checked;
            save(record);
            if (!checked.isEmpty())
                qInfo().noquote() << QStringLiteral("%1: change %2 read back: %3").arg(productName(), record.id, checked.right(500));
            Q_EMIT made(record);
        });
    });
    return record.id;
}

bool ChangeRunner::undo(const QString &id, QString *error)
{
    const auto fail = [error](const QString &why) {
        if (error)
            *error = why;
        return false;
    };
    if (busy())
        return fail(QStringLiteral("Another change is still being made."));
    std::optional<Record> found = record(id);
    if (!found)
        return fail(QStringLiteral("That change is not in the record."));
    Record r = *found;
    if (!r.canUndo())
        return fail(QStringLiteral("That change can't be undone automatically."));

    qInfo().noquote() << QStringLiteral("%1: undoing change %2 \"%3\": %4").arg(productName(), r.id, r.proposal.title, r.undoWay());
    const auto finish = [this, r](bool ok, const QString &output) {
        m_finishing = true;
        afterChange(r.proposal, false, [this, r, ok, output](const QString &) {
            m_finishing = false;
            Record done = r;
            done.status = ok ? QStringLiteral("undone") : QStringLiteral("undoFailed");
            done.undone = QDateTime::currentDateTime();
            if (!output.isEmpty())
                done.output += QStringLiteral("\n--- undo ---\n") + output;
            save(done);
            qInfo().noquote() << QStringLiteral("%1: change %2 %3").arg(productName(), done.id, done.status);
            Q_EMIT undone(done);
        });
    };
    const auto putBackCopies = [r] {
        bool ok = true;
        for (const Copy &c : r.copies) {
            if (c.absent) {
                if (QFileInfo::exists(c.original))
                    ok = QFile::remove(c.original) && ok;
                continue;
            }
            QFile::remove(c.original);
            ok = QFile::copy(c.copy, c.original) && ok;
        }
        return ok;
    };

    if (r.pre >= 0 && r.post >= 0) {
        QStringList program = m_elevate;
        program << QStringLiteral("snapper") << QStringLiteral("-c") << QStringLiteral("root")
                << QStringLiteral("undochange") << QStringLiteral("%1..%2").arg(r.pre).arg(r.post);
        run(program, m_timeLimit * 3, [finish](int exitCode, const QString &output) { finish(exitCode == 0, output); });
        return true;
    }
    if (r.proposal.undo.isEmpty()) {
        finish(putBackCopies(), QString());
        return true;
    }
    QStringList program = r.proposal.admin ? m_elevate : QStringList{};
    program << Shell << QStringLiteral("-c") << unattended(r.proposal.undo);
    run(program, r.proposal.admin ? m_timeLimit * 3 : m_timeLimit,
        [finish, putBackCopies, hasCopies = !r.copies.isEmpty()](int exitCode, const QString &output) {
            // The way back failed: the copies, where there are any, still
            // put the files back.
            if (exitCode != 0 && hasCopies)
                finish(putBackCopies(), output + QStringLiteral("\nPut back the copies of the files instead."));
            else
                finish(exitCode == 0, output);
        });
    return true;
}

QList<ChangeRunner::Record> ChangeRunner::records() const
{
    QList<Record> list;
    const QDir store(m_store);
    for (const QString &name : store.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed)) {
        if (std::optional<Record> r = record(name))
            list.append(*r);
    }
    return list;
}

std::optional<ChangeRunner::Record> ChangeRunner::record(const QString &id) const
{
    if (id.isEmpty() || id.contains(QLatin1Char('/')))
        return std::nullopt;
    QFile file(QDir(m_store).filePath(id + QStringLiteral("/change.json")));
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject())
        return std::nullopt;
    return Record::fromJson(document.object());
}

void ChangeRunner::save(const Record &record) const
{
    QDir().mkpath(QDir(m_store).filePath(record.id));
    QFile file(QDir(m_store).filePath(record.id + QStringLiteral("/change.json")));
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(QJsonDocument(record.toJson()).toJson());
}

void ChangeRunner::afterChange(const ChangeProposal &change, bool withCheck, std::function<void(const QString &)> done)
{
    const QString reload = reloadFor(change);
    const QString check = withCheck ? change.check : QString();
    const auto checking = [this, check, done] {
        if (check.isEmpty()) {
            done(QString());
            return;
        }
        run({Shell, QStringLiteral("-c"), check}, 20000, [done](int, const QString &output) {
            done(output.isEmpty() ? QStringLiteral("(nothing)") : output);
        });
    };
    if (reload.isEmpty()) {
        checking();
        return;
    }
    run({Shell, QStringLiteral("-c"), reload}, 20000, [checking](int, const QString &) { checking(); });
}

void ChangeRunner::run(const QStringList &program, int timeLimit, std::function<void(int, const QString &)> done)
{
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    m_process->setProgram(program.first());
    m_process->setArguments(program.mid(1));
    QProcess *process = m_process;
    connect(process, &QProcess::finished, this, [this, process, done](int exitCode, QProcess::ExitStatus status) {
        QString output = QString::fromUtf8(process->readAll()).trimmed();
        if (process->property("timedOut").toBool())
            output += QStringLiteral("%1Stopped: it was still running when its time was up.")
                          .arg(output.isEmpty() ? QString() : QStringLiteral("\n"));
        m_process = nullptr;
        process->deleteLater();
        done(status == QProcess::NormalExit ? exitCode : -1, output);
    });
    connect(process, &QProcess::errorOccurred, this, [this, process, done](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        m_process = nullptr;
        process->deleteLater();
        done(-1, QStringLiteral("%1 could not be started.").arg(process->program()));
    });
    // A change that is still running when its time is up is stopped, so Do it
    // never waits for ever.
    QTimer *limit = new QTimer(process);
    limit->setSingleShot(true);
    connect(limit, &QTimer::timeout, process, [process] {
        process->setProperty("timedOut", true);
        process->kill();
    });
    connect(process, &QProcess::started, limit, [limit, timeLimit] { limit->start(timeLimit); });
    process->start();
    process->closeWriteChannel();
}

} // namespace splitrock
