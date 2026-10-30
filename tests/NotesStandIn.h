// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QDBusConnection>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

// A stand-in for Notes on a bus of the test's own, answering the QuickNote
// calls its description file names (tests/fixtures/capabilities/notes.json),
// on a connection of its own so every call to it really crosses the bus.
// It holds a few made-up notes and counts what it is asked.
class NotesStandIn : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.carlsonjm.Gooseberry.QuickNote")

public:
    static constexpr char Service[] = "io.github.carlsonjm.gooseberry";

    struct Note {
        QString id;
        QString title;
        QString text;
        QString folder;
    };
    QList<Note> notes{
        {QStringLiteral("n1"), QStringLiteral("Connected testing"),
         QStringLiteral("Connected testing\n\nLeft off at the tablet's second pass: Wi-Fi drops after sleep."),
         QStringLiteral("work")},
        {QStringLiteral("n2"), QStringLiteral("Groceries"), QStringLiteral("Groceries\n\nMilk, bread."), QString()},
    };
    QStringList asked;
    QList<QStringList> created;

    bool publish(const QString &connectionName)
    {
        m_bus = QDBusConnection::connectToBus(QDBusConnection::SessionBus, connectionName);
        return m_bus.isConnected()
            && m_bus.registerObject(QStringLiteral("/QuickNote"), this, QDBusConnection::ExportScriptableSlots)
            && m_bus.registerService(QLatin1String(Service));
    }
    void unpublish()
    {
        m_bus.unregisterService(QLatin1String(Service));
        m_bus.unregisterObject(QStringLiteral("/QuickNote"));
    }

public Q_SLOTS:
    Q_SCRIPTABLE QVariantList Find(const QString &words, uint limit)
    {
        asked.append(QStringLiteral("Find"));
        QVariantList found;
        for (const Note &note : std::as_const(notes)) {
            if (!note.text.contains(words, Qt::CaseInsensitive) || (limit > 0 && uint(found.size()) >= limit))
                continue;
            const QString line = note.text.section(QLatin1Char('\n'), 2).simplified();
            found.append(QVariantMap{{QStringLiteral("id"), note.id},
                                     {QStringLiteral("title"), note.title},
                                     {QStringLiteral("excerpt"), line}});
        }
        return found;
    }
    Q_SCRIPTABLE QVariantMap Read(const QString &id)
    {
        asked.append(QStringLiteral("Read"));
        for (const Note &note : std::as_const(notes)) {
            if (note.id == id)
                return {{QStringLiteral("id"), note.id},
                        {QStringLiteral("title"), note.title},
                        {QStringLiteral("text"), note.text},
                        {QStringLiteral("folder"), note.folder}};
        }
        return {};
    }
    Q_SCRIPTABLE QVariantMap Create(const QString &text, const QString &folder)
    {
        asked.append(QStringLiteral("Create"));
        if (!folder.isEmpty() && folder != QLatin1String("work"))
            return {{QStringLiteral("problem"), QStringLiteral("There is no folder called %1.").arg(folder)}};
        created.append({text, folder});
        const QString id = QStringLiteral("n%1").arg(notes.size() + 1);
        notes.append({id, text.section(QLatin1Char('\n'), 0, 0), text, folder});
        return {{QStringLiteral("id"), id}, {QStringLiteral("title"), notes.last().title}};
    }

private:
    QDBusConnection m_bus{QString()};
};
