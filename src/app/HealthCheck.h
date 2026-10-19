// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "app/Chore.h"

#include <QDateTime>
#include <QVariantList>

namespace splitrock {

// The Health check card: a look over the computer that only reads. Whether
// every service started, the space left, what the drive and the battery
// report, when it was last updated, the newest restore point, settings files
// waiting beside the person's own, apps that crashed this week, a restart an
// update waits for, the package manager's list of what is installed, and
// downloads left unopened for months. Each is
// good, worth a look, or a problem, with the card that helps, or a description
// of the problem the person can copy to ask someone for help.
//
// Core looks the same way once a week (docs/CORE.md § Health) and speaks of
// new problems; the card itself looks each time it opens.
//
// Its stages: looking and done.
class HealthCheck : public Chore
{
    Q_OBJECT
    // Each: id, title, state (good, warn or bad), detail, action (update,
    // space, restore, community, restart, downloads, ask or empty),
    // actionLabel and question.
    Q_PROPERTY(QVariantList items READ itemList NOTIFY changed)
    // How many are worth a look or a problem.
    Q_PROPERTY(int attention READ attention NOTIFY changed)
    Q_PROPERTY(QString lastChecked READ lastChecked NOTIFY changed)
    // Whether Core looks by itself each week: its Health topic is switched on.
    Q_PROPERTY(bool watched READ watched NOTIFY changed)

public:
    struct Item {
        QString id;
        QString title;
        QString state;
        QString detail;
        QString action;
        QString actionLabel;
        QString question;
        QVariantMap toMap() const;
        static Item fromMap(const QVariantMap &map);
    };

    explicit HealthCheck(QObject *parent = nullptr);

    bool busy() const override;
    QList<Item> items() const { return m_items; }
    QVariantList itemList() const;
    int attention() const;
    QString lastChecked() const;
    bool watched() const;

    // What to paste in a forum or send to a friend for one problem: its
    // question, then what this computer is.
    Q_INVOKABLE QString helpText(const QString &question) const;
    static QString helpText(const QString &question, const QString &computer);
    // This computer in a few lines: its maker and model, its system, its
    // Linux kernel and Plasma's version, where each is known.
    static QString aboutThisComputer();

    // Opens the card and looks.
    Q_INVOKABLE void check();
    // Opens the person's Downloads folder in the file manager.
    Q_INVOKABLE void openDownloads();
    // Looks without opening the card, when the last look is a day old.
    void checkIfDue();
    // Looks without opening the card, now.
    void lookQuietly() { look(false); }

    // A few words for one of Genie's tiles saying what the check noticed,
    // or empty when the item needs nothing.
    static QString tile(const Item &item);

    static QList<Item> readLook(const QString &output, const QDateTime &now = QDateTime::currentDateTime());

    // The look itself: a script that only reads, run as the person.
    static QString defaultLookScript();
    // Whether a look's output came through to its end.
    static bool lookFinished(const QString &output);
    // What a look found, as lines of plain text: problems first, each with its
    // state, title and detail.
    static QString report(const QList<Item> &items);
    // The same, for Core: one line per item, its state (good, look or
    // problem), id, title and detail, separated by tabs.
    static QString facts(const QList<Item> &items);

    void setLookScript(const QString &script) { m_lookScript = script; }
    // Core's settings file, where its Health topic is switched off.
    void setCoreSettings(const QString &path) { m_coreSettings = path; }

private:
    void look(bool open);

    QList<Item> m_items;
    QDateTime m_checked;
    int m_lastAttention = -1;
    bool m_quiet = false;
    QString m_lookScript;
    QString m_coreSettings;
};

} // namespace splitrock
