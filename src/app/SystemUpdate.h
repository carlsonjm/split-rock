// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QDateTime>
#include <QObject>
#include <QStringList>
#include <QVariantMap>

#include <functional>

class QProcess;

namespace splitrock {

// The Update card: Split Rock's own routine for updating the whole computer,
// which the ordinary Do it card never runs (docs/DECISIONS.md, The Update card
// updates the computer).
//
// It looks first, as the person: what is waiting, the news Arch posted since
// the last update, the power, the free space, and whether restore points are
// taken. Then, when the person taps Update, it runs the update through the
// system's own password prompt with a restore point before and after, keeps
// the computer awake, and is never stopped by a timer. It reads pacman's
// output as it goes for the progress the card shows. Afterwards it updates the
// person's Flatpak apps, and looks again for what needs the person: a restart,
// settings files left beside their own, and services that failed.
class SystemUpdate : public QObject
{
    Q_OBJECT
    // Empty while the card is closed; then looking, ready, nothing, updating,
    // done or failed.
    Q_PROPERTY(QString stage READ stage NOTIFY changed)
    Q_PROPERTY(QVariantMap plan READ planMap NOTIFY changed)
    Q_PROPERTY(int done READ done NOTIFY progressChanged)
    Q_PROPERTY(int total READ total NOTIFY progressChanged)
    Q_PROPERTY(QString now READ now NOTIFY progressChanged)
    Q_PROPERTY(QString log READ log NOTIFY progressChanged)
    Q_PROPERTY(QVariantMap result READ resultMap NOTIFY changed)

public:
    struct News {
        QString title;
        QString link;
        QDateTime posted;
    };

    // What the look found, in the card's words.
    struct Plan {
        // Updates waiting from the system's own packages; -1 when they can't
        // be listed ahead on this computer.
        int system = 0;
        // Community (AUR) packages with a newer recipe; never part of the
        // update, each is offered on its own.
        int community = 0;
        // Flatpak apps with an update.
        int apps = 0;
        QStringList highlights;
        QStringList packages;
        bool restartLikely = false;
        bool snapshots = false;
        // The restore points come from pacman's own snap-pac hook, so Split
        // Rock takes none of its own.
        bool snapPac = false;
        QList<News> news;
        // Read before updating, but no reason to wait.
        QStringList warnings;
        // Reasons the update can't start now.
        QStringList blockers;
        QString plasmaFrom;
        QString plasmaTo;

        bool nothing() const { return system == 0 && apps == 0; }
        QVariantMap toMap() const;
    };

    struct Result {
        bool ok = false;
        // What went wrong, in plain words, when it failed.
        QString why;
        int installed = 0;
        int pre = -1;
        int post = -1;
        bool restart = false;
        bool appsUpdated = false;
        QStringList settingsFiles;
        QStringList failedServices;
        QVariantMap toMap() const;
    };

    explicit SystemUpdate(QObject *parent = nullptr);
    ~SystemUpdate() override;

    QString stage() const { return m_stage; }
    Plan plan() const { return m_plan; }
    QVariantMap planMap() const { return m_plan.toMap(); }
    int done() const { return m_done; }
    int total() const { return m_total; }
    QString now() const { return m_now; }
    QString log() const { return m_log; }
    Result result() const { return m_result; }
    QVariantMap resultMap() const { return m_result.toMap(); }
    bool busy() const { return m_stage == QLatin1String("looking") || m_stage == QLatin1String("updating"); }

    // Opens the card and looks at what is waiting.
    Q_INVOKABLE void check();
    // Updates, after the person tapped Update on a ready card.
    Q_INVOKABLE void start();
    // Closes the card; never while the update runs.
    Q_INVOKABLE void close();
    // Asks the desktop's own restart prompt.
    Q_INVOKABLE void restart();
    Q_INVOKABLE void openLink(const QString &link);

    // What the look printed, read into the card's plan. now is the time the
    // news is measured against when the last update's date is unknown.
    static Plan readLook(const QString &output, const QDateTime &now = QDateTime::currentDateTime());
    // The packages an Arch news feed lists, newest first.
    static QList<News> readNews(const QString &rss);
    // A line of pacman's output: what the card says it is doing now, the
    // count it changes, or nothing when the line says neither.
    struct Step {
        QString now;
        int total = -1;
        bool counts = false;
    };
    static Step readStep(const QString &line, int done, int total);
    // Why an update failed, in plain words, from its output and exit code.
    static QString whyFailed(const QString &output, int exitCode);
    // What the look after the update printed.
    static void readAfter(const QString &output, Result &result);

    // Tests run the steps with scripts of their own instead of the
    // computer's: each is a /bin/sh script; the update's runs through
    // elevate.
    void setLookScript(const QString &script) { m_lookScript = script; }
    void setUpdateScript(const QString &script) { m_updateScript = script; }
    void setAppsScript(const QString &script) { m_appsScript = script; }
    void setAfterScript(const QString &script) { m_afterScript = script; }
    void setElevate(const QStringList &program) { m_elevate = program; }
    void setSnapshots(bool on) { m_snapshots = on; }

Q_SIGNALS:
    void changed();
    void progressChanged();
    // The update and everything after it finished, or failed.
    void finished(const splitrock::SystemUpdate::Result &result, const QString &output);

private:
    void setStage(const QString &stage);
    void run(const QStringList &program, int timeLimit, std::function<void(int exitCode, const QString &output)> done,
             std::function<void(const QString &line)> eachLine = {});
    void afterSystem(int exitCode, const QString &output);
    void finish(const QString &output);
    void appendLog(const QString &line);

    QString m_stage;
    Plan m_plan;
    Result m_result;
    int m_done = 0;
    int m_total = 0;
    QString m_now;
    QString m_log;
    QString m_output;

    QString m_lookScript;
    QString m_updateScript;
    QString m_appsScript;
    QString m_afterScript;
    QStringList m_elevate{QStringLiteral("pkexec")};
    bool m_snapshots = true;
    QProcess *m_process = nullptr;
};

} // namespace splitrock
