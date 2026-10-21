// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "app/ChangeRunner.h"
#include "app/Chore.h"
#include "app/SettingRows.h"

#include <QDate>
#include <QDateTime>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <optional>

namespace splitrock {

// The What changed card: what changed on this computer in the last two weeks,
// newest first, a day at a time, each with the way back that fits it.
//
// Changes made from a Do it card come first in their day, each with Undo. The
// rest is read, as the person: what the package manager updated, installed and
// removed, Flatpak apps, the desktop's settings files that were written,
// background services that stopped working, and a new kernel waiting for a
// restart. Each says what changed, exactly, and has its way back on the row:
// a restore point for an update or an install, where the computer takes
// them; Put back for a setting, from the copies Core keeps of the desktop's
// settings (app/SettingRows.h); Start it again for a service that stopped;
// Go back for a Flathub app. What has no way back here is copied, with every
// version, what it said and how it might come back, to ask someone.
//
// It is where "undo that", "something changed" and "why did this stop
// working?" go (docs/DECISIONS.md, Recovery starts from what changed). It
// changes nothing until the person says yes on the card: Undo is the Changes
// list's own, going back is the Restore points card's, Put back is a change
// from a card with its own Undo, and starting a service again or going back
// to a Flathub app's last version are this card's own jobs, through the
// system's password prompt where they touch the system.
//
// Its stages: looking, ready, nothing, and working while a job runs.
class RecentChanges : public Chore
{
    Q_OBJECT
    // Each: id, day (in words), kind (own, update, install, remove, apps,
    // settings, service or restart), title, detail, action (undo, restore,
    // putback, start, goback, settings, ask, restart or empty), actionLabel,
    // change (for undo), question, help (the whole text to ask someone),
    // page (KDE's settings page, for settings and Show me how), exact (each
    // change as it is written, for Show details), and for the actions asked
    // first: confirmTitle, confirmSub, confirmLines and password.
    Q_PROPERTY(QVariantList items READ itemList NOTIFY changed)
    // The newest change made from a card that can still be undone, as an
    // item; empty when there is none.
    Q_PROPERTY(QVariantMap latest READ latestMap NOTIFY changed)
    // A question for the assistant that carries the whole list.
    Q_PROPERTY(QString question READ question NOTIFY changed)
    // What the last job on the card came to, in a line, or empty.
    Q_PROPERTY(QString notice READ notice NOTIFY changed)

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
        QString help;
        QString page;
        QStringList exact;
        QString confirmTitle;
        QString confirmSub;
        QStringList confirmLines;
        bool password = false;
        // What the way back acts on: the setting's change, the service and
        // whether it is the system's, the Flatpak app, its installation and
        // the commit it came from.
        ChangeProposal putBack;
        QString unit;
        bool systemUnit = false;
        QString app;
        QString installation;
        QString commit;
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
    QString notice() const { return m_notice; }

    Q_INVOKABLE void check();
    // The change that puts a setting's row back, for the Changes list to
    // make; empty when the row has none.
    std::optional<ChangeProposal> putBack(const QString &id) const;
    // A Put back being made, from the Changes list, and how it went.
    void puttingBack(const QString &id);
    void putBackDone(const ChangeRunner::Record &record);
    // Starts a service that stopped working again, or takes a Flatpak app
    // back to the version it had before; the card shows the result, and
    // looks again.
    Q_INVOKABLE void startAgain(const QString &id);
    Q_INVOKABLE void goBack(const QString &id);
    // Opens KDE's settings page by its name, such as kcm_keys.
    Q_INVOKABLE void openSettings(const QString &page);
    // The changes made from cards, read again after one is made or undone,
    // without looking at the rest again.
    void refreshOwn();

    // Where the changes made from cards come from: the Changes list.
    void setRecords(std::function<QList<ChangeRunner::Record>()> records) { m_records = std::move(records); }

    // What the look printed and the changes made from cards, read into the
    // card's list, newest first.
    // settings: the settings that changed, as rows, since Core began keeping
    // copies at keptSince; before then a settings file can only be named.
    static Look readLook(const QString &output, const QList<ChangeRunner::Record> &records,
                         const QDateTime &now = QDateTime::currentDateTime(), const QList<SettingRow> &settings = {},
                         const QDateTime &keptSince = {});
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
    // Where Core keeps its copies of the settings, and the settings folder;
    // tests give their own.
    void setSettingsHistory(const QString &folder, const QString &configHome)
    {
        m_historyFolder = folder;
        m_configHome = configHome;
    }
    // Split Rock itself, which puts a keyboard shortcut back.
    void setProgram(const QString &program) { m_program = program; }

private:
    void settle();
    // The settings rows from Core's copies, after a look of their own.
    void readSettings();
    std::optional<Item> find(const QString &id) const;
    // Ends a job: what it came to, then a fresh look.
    void finishJob(const QString &said);

    Look m_look;
    QList<SettingRow> m_settings;
    QDateTime m_keptSince;
    QString m_historyFolder;
    QString m_configHome;
    QString m_program;
    QString m_notice;
    QString m_lastOutput;
    std::function<QList<ChangeRunner::Record>()> m_records;
    QString m_lookScript;
};

} // namespace splitrock
