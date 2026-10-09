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
// Its stages: asking, searching, results, installing, done or failed.
class AppInstall : public Chore
{
    Q_OBJECT
    Q_PROPERTY(QString query READ query NOTIFY changed)
    // Each: key, name, summary, source (shelf or flathub), sourceName,
    // package, installed and alsoFlathub.
    Q_PROPERTY(QVariantList results READ resultList NOTIFY changed)
    Q_PROPERTY(QVariantMap picked READ pickedMap NOTIFY changed)
    Q_PROPERTY(QString why READ why NOTIFY changed)

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

    // Opens the card, empty, for the person to say what they need.
    Q_INVOKABLE void ask();
    Q_INVOKABLE void search(const QString &query);
    // Installs the app with that key from the results.
    Q_INVOKABLE void install(const QString &key);
    // Opens the app just installed, or one already installed.
    Q_INVOKABLE void openApp(const QString &key = QString());

    // What the search printed, read into apps, best first, at most eight.
    static QList<App> readSearch(const QString &output, const QString &query);

    void setSearchScript(const QString &script) { m_searchScript = script; }
    void setShelfScript(const QString &script) { m_shelfScript = script; }
    void setFlathubScript(const QString &script) { m_flathubScript = script; }
    void setOpenScript(const QString &script) { m_openScript = script; }

private:
    void finish(bool ok, const QString &output);

    QString m_query;
    QList<App> m_results;
    App m_picked;
    QString m_id;
    QString m_why;

    QString m_searchScript;
    QString m_shelfScript;
    QString m_flathubScript;
    QString m_openScript;
};

} // namespace splitrock
