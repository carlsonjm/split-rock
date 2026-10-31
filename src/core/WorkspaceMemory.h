// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QDBusConnection>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <optional>

namespace splitrock::core {

// Workspace Memory and Automations, Core's side (docs/CORE.md § Workspace
// Memory, § Automations). Cards, from Kadunce, remembers where every window
// was and which apps the person opens together after sign-in; Core asks the
// person, launches the apps after their yes, and Cards puts each back in its
// place. The calls are Kadunce's Workspace Memory contract, version 1, on
// the session bus at org.kde.KWin, /Kadunce, co.goodinput.Kadunce.

// The session Cards offers for bringing back: savedSession().
struct SavedSession {
    bool available = false;
    int count = 0;
    // Desktop file ids, without .desktop, in the order they stood.
    QStringList apps;
    QDateTime when;
    // Written at sign-out; false when it is the last save before a crash.
    bool clean = true;
    int windows = 0;
    // Every app it remembers is running already, as when the desktop's own
    // session restore reopened them: nothing to bring back.
    bool allOpen = false;
};
SavedSession savedSessionFrom(const QVariantMap &answer);

// What bringBack(token) armed.
struct BringBackPlan {
    bool accepted = false;
    QStringList launch;
    QStringList alreadyOpen;
    // "nothing" or "busy" when not accepted.
    QString reason;
};
BringBackPlan bringBackPlanFrom(const QVariantMap &answer);

// One set of apps opened together after sign-in: openTogether().
struct OpenSet {
    // Sorted desktop ids.
    QStringList apps;
    int count = 0;
    QDateTime lastSeen;
};
QList<OpenSet> openSetsFrom(const QVariantList &answer);

// How a bring-back went.
struct BringBackRun {
    bool accepted = false;
    QString reason;
    QStringList launched;
    QStringList alreadyOpen;
    int placed = 0;
    int missed = 0;
    // False when Cards never said the run was over, and Core stopped waiting.
    bool finished = false;
};

// "from yesterday", "from earlier today", "from Tuesday", "from 12 October".
QString fromWhen(const QDateTime &when, const QDateTime &now);
// "Firefox", "Firefox and Notes", "Firefox, Notes and Mail", and past three
// names "Firefox, Notes, Mail and 2 more".
QString namesList(const QStringList &names);
// What the app menu calls a desktop file id, or a name made from the id
// where none is installed: "org.kde.dolphin" is "Dolphin".
QString appName(const QString &desktopId);

// Kadunce's Workspace Memory calls, asked without waiting: an answer comes
// back to its callback, and nothing when Kadunce isn't there or speaks a
// version Core doesn't know. A Kadunce that isn't running is never started
// for it.
class KadunceMemory : public QObject
{
    Q_OBJECT

public:
    static constexpr int Version = 1;
    // How long Core waits for Cards to say a bring-back is over.
    static constexpr int RunLimitSeconds = 90;

    explicit KadunceMemory(QDBusConnection bus, const QString &service = QStringLiteral("org.kde.KWin"),
                           QObject *parent = nullptr);

    void savedSession(std::function<void(std::optional<SavedSession>)> done);
    void openTogether(std::function<void(std::optional<QList<OpenSet>>)> done);
    void dismissSavedSession();
    // Arms the return of every remembered app, then launches each one Cards
    // names, once; done when Cards says the run is over, or after
    // RunLimitSeconds.
    void bringBack(std::function<void(const QString &desktopId)> launch, std::function<void(const BringBackRun &)> done);
    bool running() const { return !m_token.isEmpty(); }

private Q_SLOTS:
    void appBroughtBack(const QString &token, const QString &appId, const QString &windowId, bool placed);
    void bringBackFinished(const QString &token, int placed, int missed);

private:
    // Asks for the version once; then runs next, or answers nothing.
    void whenKnown(std::function<void(bool)> next);
    void finish(bool finished);

    QDBusConnection m_bus;
    QString m_service;
    int m_version = 0;
    QString m_token;
    BringBackRun m_run;
    std::function<void(const BringBackRun &)> m_done;
    QTimer m_limit;
};

// Launches an app by its desktop file id as the app menu would, each in a
// scope of its own: kstart where KDE's is installed, gtk-launch otherwise.
// False when neither is there.
bool launchApp(const QString &desktopId);

} // namespace splitrock::core
