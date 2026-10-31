// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "tools/Capabilities.h"

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QTemporaryDir>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <memory>

class QLocalServer;
class QLocalSocket;

namespace splitrock::codex {

// The door the apps' tools reach Codex through (docs/BROKER.md § Codex).
//
// Codex runs in the fence, which keeps the desktop's message bus out, so it
// can't reach the broker or any app. Instead it is given one tool server,
// split-rock-tools-mcp, which runs beside it in the fence and only forwards:
// each message goes through a socket in a folder of this door's own, the one
// thing of the outside the fence lets it reach, and the door answers it here,
// outside the fence. The tools it lists are those the apps' files mark for
// Codex, generated from the same files Ask's form is; every call goes to the
// broker, which checks it as it checks Ask's, with Codex as the caller.
//
// A read's answer goes back to Codex set apart as material, and the door says
// what was read, for the window to list. A write never runs from here: the
// broker's card comes out of proposed, for the window's Do it row.
class ToolDoor : public QObject
{
    Q_OBJECT

public:
    using Reply = std::function<void(const QVariantMap &result)>;
    // The broker, as the door reaches it.
    struct Broker {
        // Its Tools(), as maps.
        std::function<QVariantList()> tools;
        // Its Call(), always with codex as the caller.
        std::function<void(const QString &tool, const QVariantMap &arguments, const Reply &reply)> call;
    };

    // The server's name, as Codex knows it.
    static constexpr char ServerName[] = "apps";

    explicit ToolDoor(const Broker &broker, QObject *parent = nullptr);
    ~ToolDoor() override;

    // Opens the door where some app has a tool for Codex. False when none
    // has, or the socket couldn't be made.
    bool open();
    // The folder the socket is in, which the fence lets Codex read.
    QString folder() const;
    QString socket() const;
    // The tool server as the Agent Client Protocol gives it to the agent.
    QJsonObject server(const QString &adapter) const;
    // The tools Codex is offered.
    QList<tools::Tool> offered() const { return m_offered; }

    // A tool's name as a tool server may spell it: "notes.search" is
    // "notes_search".
    static QString serverName(const QString &tool);
    // The Model Context Protocol's answer to one message, or nothing for a
    // notification; reply gets it, at once or once the broker has answered.
    void answer(const QJsonObject &message, const std::function<void(const QJsonObject &)> &reply);

Q_SIGNALS:
    // A tool read these, in that app.
    void read(const QString &app, const QStringList &names);
    // A write the person decides on: the broker's card.
    void proposed(const QVariantMap &card);

private:
    void serve(QLocalSocket *socket);

    Broker m_broker;
    QList<tools::Tool> m_offered;
    std::unique_ptr<QTemporaryDir> m_folder;
    QLocalServer *m_server = nullptr;
};

// The tool server program, where it was built or installed; empty when it
// is in neither place.
QString toolAdapter();

} // namespace splitrock::codex
