// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "codex/Agent.h"

#include <QDBusConnection>
#include <QFileSystemWatcher>
#include <QObject>
#include <QVariantMap>

namespace splitrock {
class LocalModel;
}

namespace splitrock::ask {

class Ask;

// Ask on the session bus, beside Core (docs/ASK.md § On the bus). The bus
// starts the program on the first call; it ends on its own when idle.
class AskService : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.carlsonjm.SplitRock.Ask")

public:
    static constexpr uint Version = 1;
    static constexpr char Service[] = "io.github.carlsonjm.splitrock.Ask";
    static constexpr char Path[] = "/Ask";

    // codex: Codex, as the Ask Codex window starts it; window: the program
    // that opens that window (split-rock --codex).
    AskService(ask::Ask *asking, LocalModel *model, const codex::Agent &codex, const QString &window,
               QObject *parent = nullptr);

    bool publish(QDBusConnection bus);

public Q_SLOTS:
    Q_SCRIPTABLE uint ProtocolVersion() const;
    Q_SCRIPTABLE bool Enabled() const;
    // Starts answering; the words come in Said and the end in Answered, with
    // the id returned here.
    Q_SCRIPTABLE uint Ask(const QString &question);
    Q_SCRIPTABLE void Cancel(uint id);
    Q_SCRIPTABLE QVariantMap Model() const;
    Q_SCRIPTABLE void DownloadModel();
    Q_SCRIPTABLE void CancelDownload();
    Q_SCRIPTABLE void RemoveModel();
    // Codex, for the bigger questions: installed, signedIn, handOff (the
    // switch in Settings) and, when it can't start, problem.
    Q_SCRIPTABLE QVariantMap Codex() const;
    // Opens Ask Codex with the question; with none, it only signs in.
    Q_SCRIPTABLE bool OpenCodex(const QString &question);
    Q_SCRIPTABLE bool SignOutCodex();

Q_SIGNALS:
    Q_SCRIPTABLE void Said(uint id, const QString &text);
    Q_SCRIPTABLE void Answered(uint id, const QVariantMap &result);
    Q_SCRIPTABLE void ModelChanged(const QVariantMap &model);
    Q_SCRIPTABLE void CodexChanged(const QVariantMap &codex);

private:
    void watchCodex();

    ask::Ask *m_ask;
    LocalModel *m_model;
    codex::Agent m_codex;
    QString m_window;
    QFileSystemWatcher m_watch;
    bool m_signedIn = false;
};

} // namespace splitrock::ask
