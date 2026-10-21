// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "codex/Agent.h"

#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>

// A folder of its own for each test's pretend assistant, inside the test home.
struct PretendFixture {
    static QString base()
    {
        const QString home = qEnvironmentVariable("SPLIT_ROCK_TEST_HOME");
        if (home.isEmpty())
            qFatal("SPLIT_ROCK_TEST_HOME is not set; run the tests through ctest");
        QDir().mkpath(home);
        return home + QStringLiteral("/state-XXXXXX");
    }

    QTemporaryDir state{base()};

    splitrock::codex::Agent assistant(const QString &script) const
    {
        return splitrock::codex::pretend(QStringLiteral(PRETEND_PROGRAM), QStringLiteral(SCRIPTS_DIR "/") + script,
                                         home());
    }

    bool isValid() const { return state.isValid(); }
    QString home() const { return state.path() + QStringLiteral("/pretend"); }

    // Every message the pretend assistant received, oldest first.
    QList<QJsonObject> received() const
    {
        QList<QJsonObject> messages;
        QFile file(home() + QStringLiteral("/received.jsonl"));
        if (!file.open(QIODevice::ReadOnly))
            return messages;
        for (const QByteArray &line : file.readAll().split('\n')) {
            if (!line.trimmed().isEmpty())
                messages.append(QJsonDocument::fromJson(line).object());
        }
        return messages;
    }

    QList<QJsonObject> received(const QString &method) const
    {
        QList<QJsonObject> matching;
        for (const QJsonObject &m : received()) {
            if (m.value(QStringLiteral("method")).toString() == method)
                matching.append(m);
        }
        return matching;
    }
};
