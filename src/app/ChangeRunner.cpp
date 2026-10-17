// SPDX-License-Identifier: GPL-2.0-or-later
#include "app/ChangeRunner.h"

#include "app/Admin.h"
#include "app/Product.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>

#include <algorithm>

namespace splitrock {

namespace {

const QString Shell = QStringLiteral("/bin/sh");

// The T keeps the digits in short runs, so the notebook's check for card
// numbers never mistakes an ID for one.
QString newId()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd'T'HHmmss"))
        + QStringLiteral("-%1").arg(QRandomGenerator::global()->bounded(0x10000), 4, 16, QLatin1Char('0'));
}

// A file's contents as a SHA-256, "-" when there is no file, or empty when it
// can't be read.
QString stateOf(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists() && !info.isSymLink())
        return QStringLiteral("-");
    QFile file(path);
    if (!info.isFile() || !file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        return {};
    return QString::fromLatin1(hash.result().toHex());
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
    if (pre > 0 && post > pre)
        return QStringLiteral("Put back the system files it changed, from the snapshot taken before");
    // A system change from before cards stopped running as the administrator
    // has no way back other than its snapshots: its own undo would need the
    // administrator too.
    if (!proposal.elevation().isEmpty())
        return {};
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
                                  {QStringLiteral("absent"), c.absent},
                                  {QStringLiteral("after"), c.after}});
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
                         c.value(QStringLiteral("absent")).toBool(), c.value(QStringLiteral("after")).toString()});
    }
    const QString undone = object.value(QStringLiteral("undone")).toString();
    if (!undone.isEmpty())
        r.undone = QDateTime::fromString(undone, Qt::ISODate);
    return r;
}

ChangeRunner::ChangeRunner(const QString &store, QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_admin{adminHelper()}
    , m_bwrap(bubblewrap())
{
}

ChangeRunner::~ChangeRunner()
{
    stopSystemBus();
}

QString ChangeRunner::defaultStore()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
        .filePath(QStringLiteral("split-rock/changes"));
}

QString ChangeRunner::reloadFor(const ChangeProposal &change)
{
    // KWin reads kwinrc only when told to, so a setting written there, Night
    // Light among them, changes nothing until it is.
    const QString touched = change.command + QLatin1Char(' ') + change.undo + QLatin1Char(' ') + change.files.join(QLatin1Char(' '));
    if (change.elevation().isEmpty() && touched.contains(QLatin1String("kwinrc")))
        return QStringLiteral("qdbus6 org.kde.KWin /KWin reconfigure || qdbus org.kde.KWin /KWin reconfigure");
    return {};
}

ChangeProposal ChangeRunner::withKnownUndo(const ChangeProposal &change, const Appearance &now)
{
    ChangeProposal known = change;
    if (!change.elevation().isEmpty() || !Appearance::touchedBy(change.command))
        return known;
    const QString restore = now.restoreCommand();
    if (!restore.isEmpty())
        known.undo = restore;
    if (known.check.isEmpty())
        known.check = Appearance::checkCommand();
    return known;
}

QString ChangeRunner::bubblewrap()
{
    return QStandardPaths::findExecutable(QStringLiteral("bwrap"), {QStringLiteral("/usr/bin"), QStringLiteral("/bin")});
}

QStringList ChangeRunner::guard(const QString &bwrap, const QString &systemBus)
{
    // bubblewrap sets no_new_privs, so sudo, pkexec, su and every other
    // set-user-ID program run without their rights. The system bus, where
    // run0, systemd-run and the other ways to the administrator are asked
    // for, is hidden, or replaced by a filtered one that reaches only the
    // services a person's own setting needs. The rest of the computer is the
    // person's own, as it is outside.
    if (bwrap.isEmpty())
        return {};
    QStringList program{bwrap, QStringLiteral("--die-with-parent"), QStringLiteral("--dev-bind"), QStringLiteral("/"),
                        QStringLiteral("/")};
    if (QFileInfo::exists(QStringLiteral("/run/dbus")))
        program << QStringLiteral("--tmpfs") << QStringLiteral("/run/dbus");
    if (!systemBus.isEmpty())
        program << QStringLiteral("--bind") << systemBus << QStringLiteral("/run/dbus/system_bus_socket");
    program << QStringLiteral("--");
    return program;
}

QStringList ChangeRunner::systemServices()
{
    // The fans and power profiles, which the person may set without the
    // administrator.
    return {QStringLiteral("net.hadess.PowerProfiles"), QStringLiteral("org.freedesktop.UPower.PowerProfiles"),
            QStringLiteral("org.freedesktop.UPower"), QStringLiteral("xyz.ljones.Asusd"),
            QStringLiteral("org.asuslinux.Daemon")};
}

QString ChangeRunner::startSystemBus()
{
    if (m_proxy)
        return m_proxySocket;
    const QString socket = QStringLiteral("/run/dbus/system_bus_socket");
    if (!QFileInfo::exists(socket))
        return {};
    const QString proxy = QStandardPaths::findExecutable(QStringLiteral("xdg-dbus-proxy"),
                                                         {QStringLiteral("/usr/bin"), QStringLiteral("/bin")});
    const QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (proxy.isEmpty() || runtime.isEmpty())
        return {};
    m_proxySocket = QDir(runtime).filePath(QStringLiteral("split-rock-system-bus-%1")
                                               .arg(QRandomGenerator::global()->generate64(), 16, 16, QLatin1Char('0')));
    QStringList arguments{QStringLiteral("unix:path=") + socket, m_proxySocket, QStringLiteral("--filter")};
    for (const QString &name : systemServices())
        arguments << QStringLiteral("--talk=") + name;
    m_proxy = new QProcess(this);
    m_proxy->setProcessChannelMode(QProcess::ForwardedErrorChannel);
    m_proxy->start(proxy, arguments);
    // The proxy is ready once its socket is there.
    QElapsedTimer waited;
    waited.start();
    while (m_proxy->waitForStarted(0) && !QFileInfo::exists(m_proxySocket) && waited.elapsed() < 3000)
        QThread::msleep(20);
    if (!QFileInfo::exists(m_proxySocket)) {
        stopSystemBus();
        return {};
    }
    return m_proxySocket;
}

void ChangeRunner::stopSystemBus()
{
    if (m_proxy) {
        m_proxy->kill();
        m_proxy->waitForFinished(1000);
        delete m_proxy;
        m_proxy = nullptr;
    }
    if (!m_proxySocket.isEmpty())
        QFile::remove(m_proxySocket);
    m_proxySocket.clear();
}

QStringList ChangeRunner::guarded(const QString &command)
{
    if (m_bwrap.isEmpty())
        return {};
    return guard(m_bwrap, startSystemBus()) + QStringList{Shell, QStringLiteral("-c"), command};
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
    // A card never runs a command as the administrator, whatever brought the
    // proposal here (docs/DECISIONS.md, No card runs a command as the
    // administrator).
    const QString elevation = change.elevation();
    if (!elevation.isEmpty()) {
        qWarning().noquote() << QStringLiteral("%1: refused to make \"%2\": it reaches for the administrator (%3)")
                                    .arg(productName(), change.title, elevation);
        return {};
    }
    if (m_bwrap.isEmpty()) {
        qWarning().noquote() << QStringLiteral("%1: refused to make \"%2\": bubblewrap, which keeps a change from "
                                               "reaching the administrator, is missing").arg(productName(), change.title);
        return {};
    }
    Record record;
    record.id = newId();
    record.made = QDateTime::currentDateTime();
    record.proposal = change;
    record.status = QStringLiteral("making");
    const QDir folder(QDir(m_store).filePath(record.id));
    QDir().mkpath(folder.path());

    // The person's own files, copied aside before anything changes them.
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
    // Written down, with its way back, before anything changes, or not made.
    if (!save(record)) {
        qWarning().noquote() << QStringLiteral("%1: refused to make \"%2\": it couldn't be written down first")
                                    .arg(productName(), change.title);
        return {};
    }
    if (before && !before(record)) {
        record.status = QStringLiteral("failed");
        record.output = QStringLiteral("Not made: it could not be written down first.");
        save(record);
        return {};
    }

    const QStringList program = guarded(unattended(change.command));
    qInfo().noquote() << QStringLiteral("%1: making change %2 \"%3\" as the person: %4")
                             .arg(productName(), record.id, change.title, unattended(change.command));
    run(program, m_timeLimit, [this, record](int exitCode, const QString &output) mutable {
        record.exitCode = exitCode;
        record.output = output;
        for (Copy &c : record.copies)
            c.after = stateOf(c.original);
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
            stopSystemBus();
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
            stopSystemBus();
            Q_EMIT undone(done);
        });
    };
    const auto putBackCopies = [this, r](QString *output) {
        QString why;
        const bool ok = this->putBackCopies(r, &why);
        if (!why.isEmpty())
            *output += (output->isEmpty() ? QString() : QStringLiteral("\n")) + why;
        return ok;
    };

    // A system change made before cards stopped running as the
    // administrator: its snapshots are put back by the administrator's own
    // job for it, given only their two numbers.
    if (r.pre > 0 && r.post > r.pre) {
        QString why;
        const QStringList program = adminJob(m_elevate, m_admin, QStringLiteral("restore-undo"),
                                             {QString::number(r.pre), QString::number(r.post)}, {}, &why);
        if (program.isEmpty())
            return fail(QStringLiteral("That change can't be undone here: %1.").arg(why));
        run(program, m_timeLimit * 3, [finish](int exitCode, const QString &output) { finish(exitCode == 0, output); });
        return true;
    }
    if (r.proposal.undo.isEmpty()) {
        QString output;
        const bool ok = putBackCopies(&output);
        finish(ok, output);
        return true;
    }
    // The record is the person's own file, so its way back is held to what a
    // card may run, as the change was.
    if (!r.proposal.elevation().isEmpty() || m_bwrap.isEmpty())
        return fail(QStringLiteral("That change can't be undone automatically."));
    const QStringList program = guarded(unattended(r.proposal.undo));
    run(program, m_timeLimit,
        [finish, putBackCopies, hasCopies = !r.copies.isEmpty()](int exitCode, QString output) {
            // The way back failed: the copies, where there are any, still
            // put the files back.
            if (exitCode != 0 && hasCopies) {
                output += QStringLiteral("\nPut back the copies of the files instead.");
                const bool ok = putBackCopies(&output);
                finish(ok, output);
            } else {
                finish(exitCode == 0, output);
            }
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

bool ChangeRunner::save(const Record &record) const
{
    QDir().mkpath(QDir(m_store).filePath(record.id));
    QSaveFile file(QDir(m_store).filePath(record.id + QStringLiteral("/change.json")));
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(record.toJson()).toJson()) < 0 || !file.commit()) {
        qWarning().noquote() << QStringLiteral("%1: couldn't write down change %2: %3").arg(productName(), record.id, file.errorString());
        return false;
    }
    return true;
}

bool ChangeRunner::putBackCopies(const Record &record, QString *why) const
{
    const QDir folder(QDir(m_store).filePath(record.id));
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd'T'HHmmsszzz"));
    // Changed since the change was made, by the person or anything else:
    // putting the old copies back would lose that, so none is.
    QStringList changed;
    for (const Copy &c : record.copies) {
        if (!c.after.isEmpty() && stateOf(c.original) != c.after)
            changed.append(c.original);
    }
    if (!changed.isEmpty()) {
        if (why)
            *why = QStringLiteral("Nothing was put back: %1 has changed since.").arg(changed.join(QStringLiteral(", ")));
        return false;
    }
    QStringList left;
    int n = 0;
    for (const Copy &c : record.copies) {
        const QString name = QFileInfo(c.original).fileName();
        const QString aside = folder.filePath(QStringLiteral("%1-%2.before-undo-%3").arg(n++).arg(name, stamp));
        const QString now = stateOf(c.original);
        if (c.absent) {
            // The change made it; kept aside rather than deleted.
            if (now != QLatin1String("-") && !QFile::rename(c.original, aside))
                left.append(QStringLiteral("%1 couldn't be moved aside.").arg(c.original));
            continue;
        }
        QFile copy(c.copy);
        if (!copy.open(QIODevice::ReadOnly)) {
            left.append(QStringLiteral("The copy of %1 couldn't be read, so it was left as it is.").arg(c.original));
            continue;
        }
        if (now != QLatin1String("-") && !QFile::copy(c.original, aside)) {
            left.append(QStringLiteral("%1 couldn't be kept aside first, so it was left as it is.").arg(c.original));
            continue;
        }
        // Replaced whole in one step, so a failure leaves the file as it was.
        QSaveFile original(c.original);
        if (!original.open(QIODevice::WriteOnly) || original.write(copy.readAll()) < 0 || !original.commit())
            left.append(QStringLiteral("%1 couldn't be put back: %2").arg(c.original, original.errorString()));
    }
    if (why)
        *why = left.join(QLatin1Char('\n'));
    return left.isEmpty();
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
        ChangeProposal checking;
        checking.check = check;
        if (m_bwrap.isEmpty() || !checking.elevation().isEmpty()) {
            done(QString());
            return;
        }
        run(guarded(check), 20000, [done](int, const QString &output) {
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
