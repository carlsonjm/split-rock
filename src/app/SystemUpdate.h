// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <optional>

#include "app/Chore.h"

#include <QDateTime>
#include <QStringList>
#include <QVariantMap>

namespace splitrock {

// The Update card: Split Rock's own routine for updating the whole computer,
// which the ordinary Do it card never runs (docs/DECISIONS.md, The Update card
// updates the computer).
//
// It looks first, as the person: what is waiting, each package with its old
// and new version and what it costs to download, the big desktop pieces among
// them, the news Arch posted since the last update, the power, the free space,
// and whether restore points are taken. Then, when the person taps Update, it
// looks once more, and only when the list is still the one the person read
// does it run the update through the
// system's own password prompt with a restore point before and after, keeps
// the computer awake, and is never stopped by a timer. It reads pacman's
// output as it goes for the progress the card shows. Afterwards it updates the
// person's Flatpak apps, and looks again for what needs the person: a restart,
// settings files left beside their own, and services that failed.
//
// Its stages: looking, ready, nothing, confirming, rechecking, updating, done
// or failed.
class SystemUpdate : public Chore
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap plan READ planMap NOTIFY changed)
    Q_PROPERTY(QVariantMap result READ resultMap NOTIFY changed)
    // Updates waiting, system and apps together, from the last look; -1 when
    // not looked yet or they can't be listed on this computer.
    Q_PROPERTY(int waiting READ waiting NOTIFY waitingChanged)

public:
    // One package the update installs.
    struct Package {
        QString name;
        // The version installed now; empty for a package the update brings
        // new, such as a new dependency.
        QString from;
        QString to;
        // What it costs to download, in bytes; 0 when already downloaded, -1
        // when not known.
        qint64 size = -1;
        // A big piece of the desktop, such as the kernel, KDE Plasma or
        // KWin, in a person's words; empty for the rest.
        QString piece;
        // Its version moves a step, such as Plasma 6.7 to 6.8, not only a
        // fix.
        bool step = false;
    };

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
        // Every package the update installs, big desktop pieces first, then
        // by name.
        QList<Package> list;
        // The Flatpak apps it updates.
        QStringList appIds;
        // What the system's packages cost to download, in bytes; -1 when not
        // known.
        qint64 download = -1;
        // Shuffle is installed, and KDE Plasma or KWin moves a step: the
        // person confirms once more, as Shuffle may not start until it has
        // its own update.
        bool desktopRisk = false;
        // The list changed between the look the person read and the look
        // just before updating.
        bool changed = false;
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
        // Which packages at which versions, to tell whether two looks found
        // the same update.
        QStringList fingerprint() const;
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

    Plan plan() const { return m_plan; }
    QVariantMap planMap() const { return m_plan.toMap(); }
    Result result() const { return m_result; }
    QVariantMap resultMap() const { return m_result.toMap(); }
    bool busy() const override;

    int waiting() const { return m_waiting; }

    // Opens the card and looks at what is waiting.
    Q_INVOKABLE void check();
    // Looks at what is waiting without opening the card, for Core's tiles.
    // Only reads, as the card's own look does.
    void peek();
    // Updates, after the person tapped Update on a ready card: asks once more
    // first when the update moves the desktop a step under Shuffle, then
    // looks again and updates only when the list is the one the person read.
    Q_INVOKABLE void start();
    // The person read the desktop warning and updates anyway.
    Q_INVOKABLE void confirm();

    // What the look printed, read into the card's plan. now is the time the
    // news is measured against when the last update's date is unknown.
    static Plan readLook(const QString &output, const QDateTime &now = QDateTime::currentDateTime());
    // The packages an Arch news feed lists, newest first.
    static QList<News> readNews(const QString &rss);
    // A line of pacman's output, as Chore::pacmanStep reads it.
    static Step readStep(const QString &line, int done, int total) { return pacmanStep(line, done, total); }
    // Why an update failed, in plain words, from its output and exit code.
    static QString whyFailed(const QString &output, int exitCode)
    {
        return Chore::whyFailed(output, exitCode, QStringLiteral("The update"));
    }
    // The quick look the card opens on, from the lists the last fetch left.
    static QString quickLook();
    // What the look after the update printed.
    static void readAfter(const QString &output, Result &result);

    // Tests run the steps with scripts of their own instead of the
    // computer's: each is a /bin/sh script; the update's runs through the
    // elevate program.
    // A test's own look has no quick look before it unless it sets one.
    void setLookScript(const QString &script)
    {
        m_lookScript = script;
        m_quickScript.clear();
    }
    void setQuickLookScript(const QString &script) { m_quickScript = script; }
    void setAppsScript(const QString &script) { m_appsScript = script; }
    void setAfterScript(const QString &script) { m_afterScript = script; }
    void setSnapshots(bool on) { m_snapshots = on; }

Q_SIGNALS:
    // The update and everything after it finished, or failed.
    void finished(const splitrock::SystemUpdate::Result &result, const QString &output);
    void waitingChanged();

private:
    void look();
    void refresh();
    void afterRecheck(const QStringList &read, Plan now);
    void recheck();
    void update();
    void afterSystem(int exitCode, const QString &output);
    void finish(const QString &output);
    void setWaiting(const Plan &plan);

    Plan m_plan;
    Result m_result;
    QString m_id;
    QString m_output;

    QString m_lookScript;
    QString m_quickScript;
    QString m_appsScript;
    QString m_afterScript;
    bool m_snapshots = true;
    int m_waiting = -1;
    bool m_peeking = false;
    // The fresh look after the quick one is under way; a recheck asked for
    // meanwhile is answered by it, against the list read then.
    bool m_refreshing = false;
    std::optional<QStringList> m_recheckRead;
};

} // namespace splitrock
