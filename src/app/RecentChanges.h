// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "app/ChangeRunner.h"
#include "app/Chore.h"

#include <QDate>
#include <QDateTime>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

namespace splitrock {

// The What changed card: what changed on this computer in the last two weeks,
// newest first, a day at a time, each with the way back that fits it.
//
// Changes made from a Do it card come first in their day, each with Undo. The
// rest is read, as the person: what the package manager updated, installed and
// removed, Flatpak apps, the desktop's settings files that were written,
// background services that stopped working, and a new kernel waiting for a
// restart. An update or an install can be gone back from through a restore
// point, where the computer takes them; anything else is asked about.
//
// It is where "undo that", "something changed" and "why did this stop
// working?" go (docs/DECISIONS.md, Recovery starts from what changed). It
// changes nothing itself: Undo is the Changes list's own, and going back is
// the Restore points card's.
//
// Its stages: looking, ready and nothing.
class RecentChanges : public Chore
{
    Q_OBJECT
    // Each: id, day (in words), kind (own, update, install, remove, apps,
    // settings, service or restart), title, detail, action (undo, restore,
    // ask, restart or empty), actionLabel, change (for undo) and question.
    Q_PROPERTY(QVariantList items READ itemList NOTIFY changed)
    // The newest change made from a card that can still be undone, as an
    // item; empty when there is none.
    Q_PROPERTY(QVariantMap latest READ latestMap NOTIFY changed)
    // A question for the assistant that carries the whole list.
    Q_PROPERTY(QString question READ question NOTIFY changed)

public:
    static constexpr int Days = 14;

    struct Item {
        QString id;
        QDate day;
        // When it happened, for the order within a day.
        QDateTime when;
        QString kind;
        QString title;
        QString detail;
        QString action;
        QString actionLabel;
        QString change;
        QString question;
        QVariantMap toMap(const QDate &today) const;
    };

    struct Look {
        QList<Item> items;
        // The computer takes restore points.
        bool restorePoints = false;
    };

    explicit RecentChanges(QObject *parent = nullptr);

    bool busy() const override;
    QList<Item> items() const { return m_look.items; }
    QVariantList itemList() const;
    QVariantMap latestMap() const;
    QString question() const;

    Q_INVOKABLE void check();
    // The changes made from cards, read again after one is made or undone,
    // without looking at the rest again.
    void refreshOwn();

    // Where the changes made from cards come from: the Changes list.
    void setRecords(std::function<QList<ChangeRunner::Record>()> records) { m_records = std::move(records); }

    // What the look printed and the changes made from cards, read into the
    // card's list, newest first.
    static Look readLook(const QString &output, const QList<ChangeRunner::Record> &records,
                         const QDateTime &now = QDateTime::currentDateTime());
    // The parts a person notices among packages that were updated, as
    // "the Plasma desktop 6.7.4", most noticeable first.
    static QStringList noticeable(const QMap<QString, QString> &updated);
    // A settings file's name in the person's config folder, said as what it
    // holds, or empty for one a person wouldn't know or that changes all the
    // time by itself.
    static QString settingsName(const QString &file);
    // "Today", "Yesterday", "Thursday", or "1 October" from a week back.
    static QString dayWords(const QDate &day, const QDate &today);

    void setLookScript(const QString &script) { m_lookScript = script; }

private:
    void settle();

    Look m_look;
    QString m_lastOutput;
    std::function<QList<ChangeRunner::Record>()> m_records;
    QString m_lookScript;
};

} // namespace splitrock
