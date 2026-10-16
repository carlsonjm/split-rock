// SPDX-License-Identifier: GPL-2.0-or-later
#include "app/Chore.h"

#include "app/Admin.h"
#include "app/Product.h"

#include <QDebug>
#include <QMap>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>

#include <memory>

namespace splitrock {

namespace {

const QString Shell = QStringLiteral("/bin/sh");

} // namespace

Chore::Chore(QObject *parent)
    : QObject(parent)
    , m_admin{adminHelper()}
{
}

Chore::~Chore()
{
    // What runs as the administrator keeps going without Split Rock: it
    // writes to its own log, and its process is out of Split Rock's reach.
    stopRunning();
}

bool Chore::busy() const
{
    return false;
}

void Chore::setStage(const QString &stage)
{
    if (m_stage == stage)
        return;
    m_stage = stage;
    Q_EMIT changed();
}

void Chore::setNow(const QString &now)
{
    m_now = now;
    Q_EMIT progressChanged();
}

void Chore::setProgress(int done, int total)
{
    m_done = done;
    m_total = total;
    Q_EMIT progressChanged();
}

void Chore::clearProgress()
{
    m_done = 0;
    m_total = 0;
    m_now.clear();
    m_log.clear();
    Q_EMIT progressChanged();
}

void Chore::appendLog(const QString &line)
{
    m_log += line + QLatin1Char('\n');
    // The fold holds the end of it.
    if (m_log.size() > 20000)
        m_log = m_log.right(16000);
}

void Chore::close()
{
    if (busy())
        return;
    stopRunning();
    setStage(QString());
}

void Chore::stopRunning()
{
    if (!m_process)
        return;
    m_process->disconnect(this);
    m_process->kill();
    m_process->waitForFinished(1000);
    m_process->deleteLater();
    m_process = nullptr;
}

void Chore::restart()
{
    // The desktop's own prompt, which counts down and can be cancelled.
    QProcess::startDetached(QStringLiteral("qdbus6"), {QStringLiteral("org.kde.LogoutPrompt"),
                                                       QStringLiteral("/LogoutPrompt"),
                                                       QStringLiteral("promptReboot")});
}

void Chore::openLink(const QString &link)
{
    const QUrl url(link);
    if (url.scheme() == QLatin1String("https"))
        QProcess::startDetached(QStringLiteral("xdg-open"), {url.toString()});
}

QStringList Chore::asPerson(const QString &script, const QStringList &arguments) const
{
    return QStringList{Shell, QStringLiteral("-c"), script, productName()} + arguments;
}

QStringList Chore::asAdmin(const QString &job, const QStringList &arguments) const
{
    QString why;
    const QStringList program = adminJob(m_elevate, m_admin, job, arguments, m_logs, &why);
    if (program.isEmpty())
        qWarning().noquote() << QStringLiteral("%1: won't ask for the administrator: %2").arg(productName(), why);
    return program;
}

bool Chore::writeDown(const QString &id, const QString &title, const QString &undo, QString *error)
{
    return !m_writeDown || m_writeDown(id, title, undo, error);
}

void Chore::report(const QString &id, bool ok, const QString &shown, const QString &text)
{
    if (m_report)
        m_report(id, ok, shown, text);
}

Chore::Step Chore::pacmanStep(const QString &line, int done, int total)
{
    static const QRegularExpression packages(QStringLiteral(R"(^Packages? \((\d+)\))"));
    static const QRegularExpression working(
        QStringLiteral(R"(^(upgrading|installing|reinstalling|downgrading|removing) (\S+?)\.\.\.)"));
    Step step;
    const QString trimmed = line.trimmed();
    QRegularExpressionMatch m = packages.match(trimmed);
    if (m.hasMatch()) {
        step.total = m.captured(1).toInt();
        step.now = QStringLiteral("Getting ready: %1 packages").arg(step.total);
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
        step.now = QStringLiteral("Downloading");
    else if (trimmed.startsWith(QLatin1String("checking keyring")) || trimmed.startsWith(QLatin1String("checking package integrity")))
        step.now = QStringLiteral("Checking it's genuine");
    else if (trimmed.startsWith(QLatin1String("checking for file conflicts")) || trimmed.startsWith(QLatin1String("checking available disk space")))
        step.now = QStringLiteral("Checking there's room");
    else if (trimmed.startsWith(QLatin1String(":: Running post-transaction hooks")))
        step.now = QStringLiteral("Finishing up");
    return step;
}

QString Chore::whyFailed(const QString &output, int exitCode, const QString &what)
{
    // pkexec's own: the prompt was dismissed, or not allowed.
    if (exitCode == 126 || exitCode == 127)
        return QStringLiteral("The password prompt was closed, so nothing was changed.");
    if (exitCode == -2)
        return QStringLiteral("%1 couldn't start: this computer has no password prompt for it.").arg(what);
    const QString text = output.toLower();
    if (text.contains(QLatin1String("unable to lock database")))
        return QStringLiteral("Something else was installing, so nothing was changed. Try again when it has finished.");
    if (text.contains(QLatin1String("could not resolve host")) || text.contains(QLatin1String("failed to retrieve some files"))
        || text.contains(QLatin1String("failed to synchronize")))
        return QStringLiteral("Nothing could be downloaded. Check the internet connection and try again; "
                              "nothing was changed.");
    if (text.contains(QLatin1String("not enough free disk space")))
        return QStringLiteral("There wasn't enough free space, so nothing was changed.");
    if (text.contains(QLatin1String("signature from")) || text.contains(QLatin1String("invalid or corrupted package"))
        || text.contains(QLatin1String("unknown trust")))
        return QStringLiteral("Some packages couldn't be checked as genuine, so nothing was installed. This is usually "
                              "the security keys being out of date; %1 can help.").arg(productName());
    if (text.contains(QLatin1String("conflicting files")))
        return QStringLiteral("Two packages wanted the same file, so nothing was installed. %1 can help find which.").arg(productName());
    if (text.contains(QLatin1String("breaks dependency")))
        return QStringLiteral("Other apps on this computer need it, so nothing was removed.");
    if (text.contains(QLatin1String("unable to satisfy dependency")) || text.contains(QLatin1String("could not satisfy dependencies"))
        || text.contains(QLatin1String("conflicting dependencies")))
        return QStringLiteral("Something installed on this computer is in the way, often an app from outside "
                              "the official shelf. Nothing was installed; %1 can help find it.").arg(productName());
    if (text.contains(QLatin1String("stopped:")))
        return QStringLiteral("%1 was stopped.").arg(what);
    return QStringLiteral("%1 didn't finish. %2 can read what happened and help.").arg(what, productName());
}

QString Chore::size(qint64 kb)
{
    if (kb >= 1024 * 1024)
        return QStringLiteral("%1 GB").arg(kb / 1024.0 / 1024.0, 0, 'f', kb >= 10 * 1024 * 1024 ? 0 : 1);
    if (kb >= 1024)
        return QStringLiteral("%1 MB").arg(qRound(kb / 1024.0));
    return QStringLiteral("%1 KB").arg(std::max<qint64>(kb, 0));
}

QMap<QString, QStringList> Chore::sections(const QString &output)
{
    QMap<QString, QStringList> found;
    QString section;
    for (const QString &raw : output.split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line.startsWith(QLatin1String("@@ "))) {
            section = line.mid(3).trimmed();
            found[section];
            continue;
        }
        if (!line.isEmpty())
            found[section].append(line);
    }
    return found;
}

void Chore::run(const QStringList &program, int timeLimit, std::function<void(int, const QString &)> done,
                std::function<void(const QString &)> eachLine)
{
    // Nothing to start, as when the administrator's program can't be
    // trusted: told as a program that couldn't start would be, once the
    // caller has set the card's stage.
    if (program.isEmpty()) {
        QTimer::singleShot(0, this, [done] { done(-2, QStringLiteral("Nothing could be started.")); });
        return;
    }
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
