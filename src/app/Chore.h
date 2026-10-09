// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QObject>
#include <QStringList>

#include <functional>

class QProcess;

namespace splitrock {

// One of Split Rock's own upkeep cards: updating the computer, freeing up
// space, installing an app, restore points, the health check and community
// apps (docs/DECISIONS.md, Upkeep has cards of its own).
//
// Each looks first, as the person, and changes nothing until the person taps
// the card's button. What it does then is fixed in Split Rock, never written by
// an assistant; what needs the administrator goes through the system's own
// password prompt, once for the whole job. This is the part they share: the
// stage the card shows, the programs it runs, and the line it reports progress
// on.
class Chore : public QObject
{
    Q_OBJECT
    // Empty while the card is closed; otherwise the card's own stages.
    Q_PROPERTY(QString stage READ stage NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(int done READ done NOTIFY progressChanged)
    Q_PROPERTY(int total READ total NOTIFY progressChanged)
    Q_PROPERTY(QString now READ now NOTIFY progressChanged)
    Q_PROPERTY(QString log READ log NOTIFY progressChanged)

public:
    // Called just before the job changes anything, with its title and how to
    // undo it, to write it in the notebook first. False, with why, stops it.
    using WriteDown = std::function<bool(const QString &id, const QString &title, const QString &undo, QString *error)>;
    // Called when the job finished, ok or not, with what to tell the
    // assistant.
    using Report = std::function<void(const QString &id, bool ok, const QString &shown, const QString &report)>;

    explicit Chore(QObject *parent = nullptr);
    ~Chore() override;

    QString stage() const { return m_stage; }
    int done() const { return m_done; }
    int total() const { return m_total; }
    QString now() const { return m_now; }
    QString log() const { return m_log; }
    // Looking, or making a change: the card can't be closed, and no other
    // change starts.
    virtual bool busy() const;

    // Closes the card; never while it is busy.
    Q_INVOKABLE virtual void close();
    // Asks the desktop's own restart prompt.
    Q_INVOKABLE void restart();
    Q_INVOKABLE void openLink(const QString &link);

    void setWriteDown(WriteDown writeDown) { m_writeDown = std::move(writeDown); }
    void setReport(Report report) { m_report = std::move(report); }
    // Tests run with no password prompt: what needs the administrator runs as
    // the test.
    void setElevate(const QStringList &program) { m_elevate = program; }
    // Where what runs as the administrator writes its log; /var/log otherwise.
    void setLogFolder(const QString &folder) { m_logs = folder; }

    // A line of pacman's output: what the card says it is doing now, the
    // count it changes, or nothing when the line says neither.
    struct Step {
        QString now;
        int total = -1;
        bool counts = false;
    };
    static Step pacmanStep(const QString &line, int done, int total);
    // Why a job that needed the administrator failed, in plain words, from
    // its output and exit code. what names the job, as "The update".
    static QString whyFailed(const QString &output, int exitCode, const QString &what);
    // "1.2 GB", "340 MB", from a size in kilobytes.
    static QString size(qint64 kb);
    // An output read as "@@ name" sections, each the lines under it with the
    // blank ones left out.
    static QMap<QString, QStringList> sections(const QString &output);

    // Run as the administrator: a restore point before when $2 is 1, the
    // command given after the first three arguments, a restore point after,
    // each number printed as split-rock-pre= and split-rock-post=. The
    // computer is kept awake, and the lid's switch held, until it finishes.
    // The command writes to the log named in $3, which Split Rock follows,
    // so it carries on to the end even if Split Rock stops partway. $1 is the
    // description the restore points and the hold carry.
    static const QString &transactionScript();

Q_SIGNALS:
    void changed();
    void progressChanged();

protected:
    void setStage(const QString &stage);
    void setNow(const QString &now);
    void setProgress(int done, int total);
    void clearProgress();
    void appendLog(const QString &line);
    // Runs a program; timeLimit in milliseconds, or none when 0 or less.
    // exitCode is -1 when it crashed or was stopped, -2 when it couldn't
    // start.
    void run(const QStringList &program, int timeLimit, std::function<void(int exitCode, const QString &output)> done,
             std::function<void(const QString &line)> eachLine = {});
    // A script run as the person, with its arguments.
    QStringList asPerson(const QString &script, const QStringList &arguments = {}) const;
    // A script run as the administrator, through the system's prompt.
    QStringList asAdmin(const QString &script, const QStringList &arguments = {}) const;
    // The log of that name, in the log folder.
    QString logPath(const QString &name) const { return m_logs + QLatin1Char('/') + name; }
    bool writeDown(const QString &id, const QString &title, const QString &undo, QString *error);
    void report(const QString &id, bool ok, const QString &shown, const QString &report);
    void stopRunning();

    QStringList m_elevate{QStringLiteral("pkexec")};
    QString m_logs{QStringLiteral("/var/log")};

private:
    QString m_stage;
    int m_done = 0;
    int m_total = 0;
    QString m_now;
    QString m_log;
    QProcess *m_process = nullptr;
    WriteDown m_writeDown;
    Report m_report;
};

} // namespace splitrock
