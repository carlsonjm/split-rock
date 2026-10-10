// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "app/Chore.h"

#include <QDateTime>
#include <QVariantList>
#include <QVariantMap>

namespace splitrock {

// The Restore points card: the restore points this computer took, newest
// first, each said as what was about to happen ("Before updating the
// computer"), and the way back to one.
//
// Going back is the computer's own: its startup menu lists restore points
// where it was set up to, and starting one leaves nothing changed until the
// person keeps it. The card says where to tap and restarts; once the computer
// runs on a restore point, it says so and hands keeping it to the computer's
// own restore tool. Split Rock never rolls the system back itself
// (docs/DECISIONS.md, Upkeep has cards of its own).
//
// Its stages: looking, ready, nothing, denied (listing them needs the
// password), chosen and booted.
class RestorePoints : public Chore
{
    Q_OBJECT
    // Each: number, date, when (in words), what, kind (update, install,
    // remove, timeline, boot or other), and own: taken by Split Rock, rather
    // than by the system before each package change.
    Q_PROPERTY(QVariantList points READ pointList NOTIFY changed)
    Q_PROPERTY(QVariantMap chosen READ chosenMap NOTIFY changed)
    // limine, grub or empty: which startup menu lists the restore points.
    Q_PROPERTY(QString menu READ menu NOTIFY changed)
    // The way to keep a restore point, once started: a program's name, or
    // empty when the computer has none.
    Q_PROPERTY(QString keepTool READ keepTool NOTIFY changed)
    // The restore point the computer runs on now, or -1.
    Q_PROPERTY(int bootedFrom READ bootedFrom NOTIFY changed)
    Q_PROPERTY(bool configured READ configured NOTIFY changed)
    // The person's own files are kept apart from the system's, so going back
    // leaves them as they are.
    Q_PROPERTY(bool homeSeparate READ homeSeparate NOTIFY changed)
    Q_PROPERTY(QString newest READ newest NOTIFY changed)
    // How the system clears old restore points by itself, in words; empty
    // when it doesn't.
    Q_PROPERTY(QString keeping READ keeping NOTIFY changed)
    // How many of them Split Rock took.
    Q_PROPERTY(int ownCount READ ownCount NOTIFY changed)
    // The steps to go back to the chosen one.
    Q_PROPERTY(QStringList steps READ steps NOTIFY changed)

public:
    struct Point {
        int number = 0;
        QDateTime date;
        QString what;
        QString kind;
        bool own = false;
        QVariantMap toMap() const;
    };

    struct Look {
        bool configured = false;
        bool denied = false;
        QList<Point> points;
        QString menu;
        QString keepTool;
        int bootedFrom = -1;
        bool homeSeparate = false;
        // snapper's number cleanup: on, and how many it keeps.
        bool cleanup = false;
        int limit = 0;
        int limitImportant = 0;
    };

    explicit RestorePoints(QObject *parent = nullptr);

    bool busy() const override;
    QList<Point> points() const { return m_look.points; }
    QVariantList pointList() const;
    QVariantMap chosenMap() const { return m_chosen.toMap(); }
    QString menu() const { return m_look.menu; }
    QString keepTool() const { return m_look.keepTool; }
    int bootedFrom() const { return m_look.bootedFrom; }
    bool configured() const { return m_look.configured; }
    bool homeSeparate() const { return m_look.homeSeparate; }
    QString newest() const;
    QString keeping() const;
    int ownCount() const;
    QStringList steps() const;

    Q_INVOKABLE void check();
    // Lists them through the password prompt, where the person may not.
    Q_INVOKABLE void checkAsAdmin();
    Q_INVOKABLE void choose(int number);
    Q_INVOKABLE void back();
    // Opens the computer's own tool for keeping the restore point it runs on.
    Q_INVOKABLE void openKeepTool();

    static Look readLook(const QString &output);
    // snapper's JSON list, pairs joined, newest first.
    static QList<Point> readList(const QString &json);
    // What a restore point's description says was about to happen.
    static QString describe(const QString &description, const QString &cleanup, QString *kind = nullptr);
    // Taken by Split Rock, under either of its names.
    static bool isOwn(const QString &description);

    void setLookScript(const QString &script) { m_lookScript = script; }
    void setListScript(const QString &script) { m_listScript = script; }

private:
    void settle();

    Look m_look;
    Point m_chosen;
    QString m_lookScript;
    QString m_listScript;
};

} // namespace splitrock
