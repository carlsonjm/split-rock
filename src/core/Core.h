// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "core/Attention.h"
#include "core/Storage.h"

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
//   attention.ini  what it has said, so it never says it twice
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
        // An ordinary notification, for the ticker.
        std::function<void(const QString &summary, const QString &body)> notify;
        // Opens the card the fact's action names, such as "space".
        std::function<void(const QString &card)> openCard;
    };

    // folder: where its files are kept. settings: the person's settings file,
    // where each topic can be switched off.
    Core(const QString &folder, const QString &settings, Hooks hooks, QObject *parent = nullptr);

    // Looks now, and speaks if a fact got worse.
    void check();

    // Every fact Core has, for the surfaces; none for a topic switched off.
    QList<QVariantMap> facts() const;
    // The person answered a fact: "open", "not-now" or "dismiss".
    bool act(const QString &topic, const QString &action);

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
    void write(const QString &line);
    void loadSamples();
    void saveSamples(bool all);
    void loadAttention();
    void saveAttention() const;
    void trimLogbook();

    QString m_folder;
    QString m_settings;
    Hooks m_hooks;
    QList<Sample> m_samples;
    StorageFact m_storage;
    Attention m_attention;
};

} // namespace splitrock::core
