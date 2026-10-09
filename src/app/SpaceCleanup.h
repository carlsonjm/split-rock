// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "app/Chore.h"

#include <QVariantList>
#include <QVariantMap>

namespace splitrock {

// The Free up space card: what takes room that the person doesn't need, each
// with its size, chosen or not; then one tap clears what is chosen and says
// how much came free.
//
// Only leftovers are offered: old copies of packages, logs, crash reports,
// cache files unused for a month, community apps' build files, app parts
// nothing uses, and the trash. Each is cleared by a command fixed here. What
// needs the administrator goes in one password prompt; parts nothing uses are
// removed between two restore points, and are off until the person picks
// them, as is the trash.
//
// Its stages: looking, ready, nothing, clearing, done or failed.
class SpaceCleanup : public Chore
{
    Q_OBJECT
    // Each: id, title, detail, size (in words), kb, count, admin, chosen,
    // list (what it holds, for the fold) and tone, a colour's name.
    Q_PROPERTY(QVariantList items READ itemList NOTIFY changed)
    Q_PROPERTY(QString chosenSize READ chosenSize NOTIFY changed)
    Q_PROPERTY(qint64 chosenKb READ chosenKb NOTIFY changed)
    Q_PROPERTY(qint64 diskKb READ diskKb NOTIFY changed)
    Q_PROPERTY(qint64 freeKb READ freeKb NOTIFY changed)
    Q_PROPERTY(bool needsPassword READ needsPassword NOTIFY changed)
    Q_PROPERTY(QString freed READ freed NOTIFY changed)
    Q_PROPERTY(QString why READ why NOTIFY changed)

public:
    struct Item {
        QString id;
        QString title;
        QString detail;
        qint64 kb = -1;
        int count = 0;
        bool admin = false;
        bool chosen = false;
        QStringList list;
        QString tone;
    };

    struct Look {
        QList<Item> items;
        qint64 diskKb = 0;
        qint64 freeKb = -1;
        bool snapPac = false;
    };

    explicit SpaceCleanup(QObject *parent = nullptr);

    bool busy() const override;
    QList<Item> items() const { return m_look.items; }
    QVariantList itemList() const;
    qint64 chosenKb() const;
    QString chosenSize() const { return size(chosenKb()); }
    qint64 diskKb() const { return m_look.diskKb; }
    qint64 freeKb() const { return m_look.freeKb; }
    bool needsPassword() const;
    QString freed() const { return m_freed; }
    QString why() const { return m_why; }

    Q_INVOKABLE void check();
    Q_INVOKABLE void choose(const QString &id, bool chosen);
    Q_INVOKABLE void start();

    // What the look printed, read into the card's list, biggest first.
    static Look readLook(const QString &output);
    // "1.2G", "340.5M" or "12.3 MiB" as kilobytes; -1 when it's neither.
    static qint64 readSize(const QString &text);

    void setLookScript(const QString &script) { m_lookScript = script; }
    void setAdminScript(const QString &script) { m_adminScript = script; }
    void setPersonScript(const QString &script) { m_personScript = script; }
    void setFreeScript(const QString &script) { m_freeScript = script; }

private:
    void runPerson(const QString &adminOutput);
    void finish(bool ok, const QString &output);
    void eachLine(const QString &line);

    Look m_look;
    QString m_id;
    QString m_freed;
    QString m_why;
    QStringList m_chosenTitles;
    QStringList m_removed;
    int m_pre = -1;

    QString m_lookScript;
    QString m_adminScript;
    QString m_personScript;
    QString m_freeScript;
};

} // namespace splitrock
