// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "app/Chore.h"

#include <QDateTime>
#include <QVariantList>

namespace splitrock {

// The Health check card: a look over the computer that only reads. Whether
// every service started, the space left, what the drive and the battery
// report, when it was last updated, the newest restore point, settings files
// waiting beside the person's own, and apps that crashed this week. Each is
// good, worth a look, or a problem, with the card that helps, or a question
// for the assistant.
//
// It runs by itself once a day, quietly, while Split Rock runs; the notebook
// shows what it found, Genie's tiles say what needs the person, and the card
// opens only when the person asks.
//
// Its stages: looking and done.
class HealthCheck : public Chore
{
    Q_OBJECT
    // Each: id, title, state (good, warn or bad), detail, action (update,
    // space, restore, community, ask or empty), actionLabel and question.
    Q_PROPERTY(QVariantList items READ itemList NOTIFY changed)
    // How many are worth a look or a problem.
    Q_PROPERTY(int attention READ attention NOTIFY changed)
    Q_PROPERTY(QString lastChecked READ lastChecked NOTIFY changed)

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

    // Opens the card and looks.
    Q_INVOKABLE void check();
    // Looks without opening the card, when the last look is a day old.
    void checkIfDue();
    // Looks without opening the card, now.
    void lookQuietly() { look(false); }

    // A few words for one of Genie's tiles saying what the check noticed,
    // or empty when the item needs nothing.
    static QString tile(const Item &item);

    static QList<Item> readLook(const QString &output, const QDateTime &now = QDateTime::currentDateTime());

    void setLookScript(const QString &script) { m_lookScript = script; }

private:
    void look(bool open);

    QList<Item> m_items;
    QDateTime m_checked;
    int m_lastAttention = -1;
    bool m_quiet = false;
    QString m_lookScript;
};

} // namespace splitrock
