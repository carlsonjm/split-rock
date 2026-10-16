// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "app/Chore.h"

#include <QVariantList>
#include <QVariantMap>

namespace splitrock {

// The Install an app card: the person says what they need, such as "a PDF
// editor"; the card finds apps for it on the system's own shelf and on
// Flathub, the person picks one, and one tap installs it and offers to open
// it.
//
// The system's own shelf comes first when it has the app: it is checked by
// the system's own keys and updated with the system. Community apps are never
// offered here; each needs a look at its recipe first, on its own card.
//
// It removes apps too: asked to remove one ("uninstall VLC"), it finds the
// installed apps the words fit, including ones from outside the shelf, and
// one tap, then a yes, removes the one picked, with a restore point first for
// the system's own. What the system can't run without is never offered.
//
// Its stages: asking, searching, results, installing, done, confirm (removing
// one waits for a yes), removing, removed or failed.
class AppInstall : public Chore
{
    Q_OBJECT
    Q_PROPERTY(QString query READ query NOTIFY changed)
    // Each: key, name, summary, source (shelf or flathub), sourceName,
    // package, installed and alsoFlathub.
    Q_PROPERTY(QVariantList results READ resultList NOTIFY changed)
    Q_PROPERTY(QVariantMap picked READ pickedMap NOTIFY changed)
    Q_PROPERTY(QString why READ why NOTIFY changed)
    // The person asked to remove an app, rather than to find one.
    Q_PROPERTY(bool removing READ removing NOTIFY changed)

public:
    struct App {
        QString key;
        QString name;
        QString summary;
        // "shelf", the system's own packages, or "flathub".
        QString source;
        // The package's name, or the Flatpak app's id.
        QString package;
        QString remote;
        bool installed = false;
        bool alsoFlathub = false;
        QVariantMap toMap() const;
    };

    explicit AppInstall(QObject *parent = nullptr);

    bool busy() const override;
    QString query() const { return m_query; }
    QList<App> results() const { return m_results; }
    QVariantList resultList() const;
    QVariantMap pickedMap() const { return m_picked.toMap(); }
    QString why() const { return m_why; }
    bool removing() const { return m_removing; }

    // Opens the card, empty, for the person to say what they need.
    Q_INVOKABLE void ask();
    Q_INVOKABLE void search(const QString &query);
    // Installs the app with that key from the results.
    Q_INVOKABLE void install(const QString &key);
    // Opens the app just installed, or one already installed.
    Q_INVOKABLE void openApp(const QString &key = QString());
    // Asks to remove the installed app with that key: the card waits for a
    // yes. confirmRemove() removes it; back() returns to the results.
    Q_INVOKABLE void remove(const QString &key);
    Q_INVOKABLE void confirmRemove();
    Q_INVOKABLE void back();
    // Opens the card to say which app to remove.
    Q_INVOKABLE void askRemove();
    // Opens the card straight to removing the app with that desktop file id,
    // such as org.videolan.vlc.desktop, as Search asks: the package or
    // Flatpak that owns it is found, and the card waits for a yes. One the
    // system can't run without, or one nothing owns, is said and left.
    void removeApp(const QString &desktopId);

    // Words that ask to remove an app rather than find one, and the words
    // that are left once those are taken out: "uninstall MaiN Keyboard" is
    // MaiN Keyboard.
    static bool asksToRemove(const QString &words);
    static QString withoutRemoveWords(const QString &words);
    // Packages the system can't do without, by name, which are never
    // offered for removal; the administrator's helper also keeps Plasma's
    // own groups and whatever another package needs (split-rock-admin kept).
    static bool keptBySystem(const QString &package);

    // What the search printed, read into apps, best first, at most eight.
    static QList<App> readSearch(const QString &output, const QString &query);

    void setSearchScript(const QString &script) { m_searchScript = script; }
    void setFlathubScript(const QString &script) { m_flathubScript = script; }
    void setOpenScript(const QString &script) { m_openScript = script; }
    void setFlathubRemoveScript(const QString &script) { m_flathubRemoveScript = script; }
    void setOwnerScript(const QString &script) { m_ownerScript = script; }

private:
    // Waits for the yes to remove app, once the administrator's helper has
    // said, read only, that the system doesn't run on it.
    void askToRemove(const App &app);
    void finish(bool ok, const QString &output);
    void finishRemoving(bool ok, const QString &output);

    QString m_query;
    QList<App> m_results;
    App m_picked;
    QString m_id;
    QString m_why;
    bool m_removing = false;

    QString m_searchScript;
    QString m_flathubScript;
    QString m_openScript;
    QString m_flathubRemoveScript;
    QString m_ownerScript;
};

} // namespace splitrock
