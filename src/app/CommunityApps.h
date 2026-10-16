// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "app/Chore.h"

#include <QDateTime>
#include <QVariantList>
#include <QVariantMap>

namespace splitrock {

// The Community apps card: apps installed from the community shelf (the AUR)
// that have a newer recipe, each updated on its own after a look at what
// changed in its recipe since the version installed.
//
// Anyone can publish a recipe there, and a recipe runs as the person while it
// builds, so the look comes first: whether only the version changed, new
// places it downloads from, a script it runs as the administrator, lines that
// fetch and run something, and whether anyone still looks after it. Then one
// password prompt builds exactly the recipe the person saw, as the person,
// and installs it between two restore points. A whole-system update never
// includes these (docs/DECISIONS.md, The Update card updates the computer).
//
// Its stages: looking, ready, nothing, reviewing, reviewed, updating, done or
// failed.
class CommunityApps : public Chore
{
    Q_OBJECT
    // Each with a newer recipe: name, base, installed, latest, maintainer,
    // orphaned, outOfDate, votes and summary.
    Q_PROPERTY(QVariantList apps READ appList NOTIFY changed)
    // Installed from outside any shelf, so there is nothing to compare.
    Q_PROPERTY(int unlisted READ unlisted NOTIFY changed)
    // The app being looked at: the app's fields, with flags (each tone and
    // text), blockers, helpers (from the system's shelf), diff and firstLook.
    Q_PROPERTY(QVariantMap review READ reviewMap NOTIFY changed)
    Q_PROPERTY(QString why READ why NOTIFY changed)

public:
    struct App {
        QString name;
        QString base;
        QString installed;
        QString latest;
        QString maintainer;
        QString summary;
        bool outOfDate = false;
        int votes = 0;
        QVariantMap toMap() const;
    };

    struct Flag {
        // good, warn or bad.
        QString tone;
        QString text;
    };

    struct Review {
        App app;
        QString head;
        bool firstLook = false;
        bool versionOnly = false;
        QList<Flag> flags;
        QStringList blockers;
        QStringList helpers;
        QStringList newHosts;
        QString diff;
        QVariantMap toMap() const;
    };

    struct Look {
        QList<App> apps;
        int unlisted = 0;
        bool reached = false;
    };

    explicit CommunityApps(QObject *parent = nullptr);

    bool busy() const override;
    QList<App> apps() const { return m_look.apps; }
    QVariantList appList() const;
    int unlisted() const { return m_look.unlisted; }
    Review review() const { return m_review; }
    QVariantMap reviewMap() const { return m_review.toMap(); }
    QString why() const { return m_why; }

    Q_INVOKABLE void check();
    // Fetches an app's recipe and reads what changed.
    Q_INVOKABLE void look(const QString &name);
    Q_INVOKABLE void back();
    // Builds and installs the recipe looked at.
    Q_INVOKABLE void start();

    // pacman -Qm and the community shelf's answer for them.
    static Look readLook(const QString &output);
    static Review readReview(const QString &output, const App &app);
    // pacman's own order of versions: below 0 when a is older than b.
    static int compareVersions(const QString &a, const QString &b);

    void setLookScript(const QString &script) { m_lookScript = script; }
    void setReviewScript(const QString &script) { m_reviewScript = script; }
    // Where recipes are fetched to; the person's cache otherwise.
    void setRecipes(const QString &folder) { m_recipes = folder; }

private:
    void finish(bool ok, const QString &output);

    Look m_look;
    Review m_review;
    QString m_id;
    QString m_why;
    QString m_recipes;

    QString m_lookScript;
    QString m_reviewScript;
};

} // namespace splitrock
