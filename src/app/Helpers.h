// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "app/AppInstall.h"
#include "app/CommunityApps.h"
#include "app/HealthCheck.h"
#include "app/RecentChanges.h"
#include "app/RestorePoints.h"
#include "app/SpaceCleanup.h"
#include "app/SystemUpdate.h"
#include "app/TidyUp.h"

#include <QObject>

namespace splitrock {

class ChangeRunner;
class Notebook;

// Split Rock's Helpers, Core's temporary cards (src/app/Chore.h): updating the
// computer, freeing up space, installing or removing an app, restore points,
// the health check, community apps, what changed and tidying up Downloads. One is open at a time,
// in a window of its own (qml/ActionWindow.qml).
//
// Each Helper writes what it is about to change in the notebook, with how to
// undo it, before it changes anything. What changed puts back a change made
// earlier from a card, through the change record (app/ChangeRunner.h).
class Helpers : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject *update READ systemUpdateObject CONSTANT)
    Q_PROPERTY(QObject *space READ spaceObject CONSTANT)
    Q_PROPERTY(QObject *install READ installObject CONSTANT)
    Q_PROPERTY(QObject *restore READ restoreObject CONSTANT)
    Q_PROPERTY(QObject *recent READ recentObject CONSTANT)
    Q_PROPERTY(QObject *health READ healthObject CONSTANT)
    Q_PROPERTY(QObject *community READ communityObject CONSTANT)
    Q_PROPERTY(QObject *tidy READ tidyObject CONSTANT)
    // The Helper open now, by its name, or empty; and whether one is looking
    // or changing something, when no other may start.
    Q_PROPERTY(QString chore READ chore NOTIFY choreChanged)
    Q_PROPERTY(bool choreBusy READ choreBusy NOTIFY choreChanged)
    // A change is being put back.
    Q_PROPERTY(bool changing READ changing NOTIFY changesChanged)

public:
    // store: where the change record is kept; ChangeRunner's own folder
    // otherwise.
    explicit Helpers(const QString &store = QString(), QObject *parent = nullptr);
    ~Helpers() override;

    // The notebook the Helpers write their changes in; none in tests that do
    // not need one. Not owned.
    void setNotebook(Notebook *notebook) { m_notebook = notebook; }

    SystemUpdate *systemUpdate() const { return m_update; }
    SpaceCleanup *spaceCleanup() const { return m_space; }
    AppInstall *appInstall() const { return m_install; }
    RestorePoints *restorePoints() const { return m_restore; }
    HealthCheck *healthCheck() const { return m_health; }
    RecentChanges *recentChanges() const { return m_recent; }
    CommunityApps *communityApps() const { return m_community; }
    TidyUp *tidyUp() const { return m_tidy; }
    ChangeRunner *changeRunner() const { return m_runner; }
    QObject *systemUpdateObject() const { return m_update; }
    QObject *spaceObject() const { return m_space; }
    QObject *installObject() const { return m_install; }
    QObject *restoreObject() const { return m_restore; }
    QObject *recentObject() const { return m_recent; }
    QObject *healthObject() const { return m_health; }
    QObject *communityObject() const { return m_community; }
    QObject *tidyObject() const { return m_tidy; }

    QString chore() const;
    bool choreBusy() const;
    bool changing() const;
    // Opens a Helper by its name, closing the one open; never while one is
    // busy. query is what to look for, on Install an app; "uninstall" opens
    // Install an app to remove the app whose desktop file id query is. False
    // when it didn't open.
    Q_INVOKABLE bool openChore(const QString &name, const QString &query = QString());
    // Undo, from What changed: puts a change back.
    Q_INVOKABLE bool undoChange(const QString &id);

Q_SIGNALS:
    void choreChanged();
    void changesChanged();

private:
    void addChore(const QString &name, Chore *chore);

    Notebook *m_notebook = nullptr;
    ChangeRunner *m_runner = nullptr;
    QList<std::pair<QString, Chore *>> m_chores;
    SystemUpdate *m_update = nullptr;
    SpaceCleanup *m_space = nullptr;
    AppInstall *m_install = nullptr;
    RestorePoints *m_restore = nullptr;
    RecentChanges *m_recent = nullptr;
    HealthCheck *m_health = nullptr;
    CommunityApps *m_community = nullptr;
    TidyUp *m_tidy = nullptr;
};

} // namespace splitrock
