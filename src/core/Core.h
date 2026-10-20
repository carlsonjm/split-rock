// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "core/Attention.h"
#include "core/Devices.h"
#include "core/Health.h"
#include "core/Storage.h"
#include "core/Updates.h"

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QObject>
#include <QVariantMap>

#include <functional>

namespace splitrock::core {

// Core, the front desk (docs/CORE.md): it looks at the computer on a schedule,
// writes what it saw in a logbook the person can read, and tells the desktop's
// surfaces when a fact deserves attention. It never calls a language model and
// never changes the computer: the most it does is open a card, where the
// person decides.
//
// Everything it keeps is a plain file in its folder:
//   logbook.txt    what it saw and said, and what the person did, by day
//   storage.tsv    its looks at the drive: time, size and free, in kilobytes
//   attention.ini  what it has said, so it never says it twice, and its
//                  last looks for updates and at the computer's health
// Each is cut back to the last 90 days.
class Core : public QObject
{
    Q_OBJECT

public:
    struct Hooks {
        // The drive, looked at now.
        std::function<Sample(const QDateTime &now)> look;
        std::function<QDateTime()> now = [] { return QDateTime::currentDateTimeUtc(); };
        // Whether Do Not Disturb is on.
        std::function<bool()> doNotDisturb = [] { return false; };
        // An ordinary notification, for the ticker, from one topic; tapping
        // it answers that topic with "open".
        std::function<void(const QString &topic, const QString &summary, const QString &body)> notify;
        // Opens an upkeep card by its name, such as "space", with what the
        // person typed for the cards that take it.
        std::function<void(const QString &card, const QString &query)> openCard;
        // Whether KDE's settings page by that name, such as kcm_bluetooth,
        // is installed, and opening it.
        std::function<bool(const QString &page)> hasSettings = [](const QString &) { return true; };
        std::function<void(const QString &page)> openSettings;
    };

    // folder: where its files are kept. settings: the person's settings file,
    // where each topic can be switched off.
    Core(const QString &folder, const QString &settings, Hooks hooks, QObject *parent = nullptr);

    // Looks at the drive now, and speaks if a fact got worse.
    void check();
    // A look for updates, taken outside because it waits on the network; it
    // speaks if the fact got worse. A failed look leaves the last fact standing.
    void seeUpdates(const UpdatesLook &look);
    // The person's settings changed: every fact is judged again by them now,
    // the drive with a fresh look and updates from their last.
    void reconsider();
    // Whether the Updates topic is switched on, so the program knows to look.
    bool watchesUpdates() const;
    // A look at the computer's health, taken outside because it runs the
    // Health check's look; it speaks if a new problem appeared. A failed
    // look leaves the last fact standing.
    void seeHealth(const HealthLook &look);
    // Whether the Health topic is switched on and its weekly look is due.
    bool healthDue() const;

    // A Windows installer arrived in the Downloads folder: Core offers, once,
    // to find the Linux version.
    void seeInstaller(const QString &fileName);
    // Whether the Windows programs topic is on and was not told to stop.
    bool watchesDownloads() const;
    // What the Downloads folder and the Desktop hold, in kilobytes: past
    // 5 GB Core says so once on the ticker, and again only 5 GB later.
    void seeTidy(qint64 kb);
    bool watchesTidy() const;
    static constexpr qint64 TidyStepKb = 5LL * 1024 * 1024;

    // Every device of one kind there is now. One Core has not seen before is
    // news once, at the ticker; the first time Core looks at a kind, what is
    // already there is known without a word.
    void seeDevices(DeviceKind kind, const QList<Device> &present);
    // Whether the Devices topic is switched on.
    bool watchesDevices() const;

    // How readily Core speaks, the person's Notice setting, from 0 (Less) to
    // 100 (More), 50 by default; the words "quiet" and "eager" read as 0 and
    // 100. The limits each topic holds follow it.
    int notice() const;
    StorageLimits storageLimits() const;
    UpdatesLimits updatesLimits() const;

    // Every fact Core has, for the surfaces; none for a topic switched off.
    QList<QVariantMap> facts() const;
    // The person answered a fact: "open", "not-now" or "dismiss".
    bool act(const QString &topic, const QString &action);

    // The upkeep cards Search can open by their words: id, title, sub,
    // icon and keywords each. They act only after the person's yes, on the
    // card.
    static QList<QVariantMap> actions();
    // Opens one; query is passed on only to the card that takes it, Install
    // an app. False for an action not listed.
    bool openAction(const QString &id, const QString &query);
    // Opens Install an app straight to removing the app with that desktop
    // file id, as Search's Uninstall does: the card finds the package that
    // owns it and waits for a yes. False for anything not shaped like one.
    bool removeApp(const QString &desktopId);

    QString folder() const { return m_folder; }
    static QString defaultFolder();
    static QString defaultSettings();

    // How long the logbook and the looks are kept.
    static constexpr int KeepDays = 90;

Q_SIGNALS:
    void factChanged(const QVariantMap &fact);

private:
    bool enabled(const QString &topic) const;
    QVariantMap storageFact() const;
    QVariantMap updatesFact() const;
    QVariantMap healthFact() const;

    QVariantMap windowsFact() const;
    // Judges the drive from the looks so far, and speaks if it got worse.
    void judgeStorage(const QDateTime &now);
    // Logbook lines and the ticker for what one decision said.
    void tell(const QString &topic, const QString &title, const Spoken &spoken, const QString &line,
              const QString &detail);
    void write(const QString &line);
    void loadSamples();
    void saveSamples(bool all);
    void loadAttention();
    void saveAttention(const QString &topic) const;
    void trimLogbook();

    QString m_folder;
    QString m_settings;
    Hooks m_hooks;
    QList<Sample> m_samples;
    StorageFact m_storage;
    Attention m_attention;
    bool m_looked = false;
    UpdatesLook m_updatesLook;
    QDateTime m_waitingSince;
    UpdatesFact m_updates;
    Attention m_updatesAttention;
    bool m_updatesLooked = false;
    // What Downloads and the Desktop held when the ticker last said so, or
    // less since, when they were tidied.
    qint64 m_tidySpokenKb = 0;
    HealthLook m_healthLook;
    HealthFact m_health;
    Attention m_healthAttention;
    bool m_healthLooked = false;

    // The installer last seen, the program it is for, and how many have come.
    QString m_installer;
    QString m_program;
    int m_installers = 0;
    // The person said not to tell them again.
    bool m_windowsNever = false;
    Attention m_windowsAttention;

    // The devices of each kind Core knows, by id, and the kinds it has
    // looked at; the kind it spoke of last, which a tap on the ticker opens.
    QHash<QString, QStringList> m_devicesKnown;
    QString m_deviceSpoken;
};

} // namespace splitrock::core
