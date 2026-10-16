// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "core/Attention.h"
#include "core/Storage.h"
#include "core/Updates.h"

#include <QDateTime>
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
//                  last look for updates
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

    // How readily Core speaks, the person's Notice setting: "quiet",
    // "balanced" or "eager". The limits each topic holds to follow it.
    QString notice() const;
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
};

} // namespace splitrock::core
