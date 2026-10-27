// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "codex/Agent.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QTemporaryDir>

#include <memory>

namespace acp {
class Client;
}

namespace splitrock::broker {

// One question to Codex with no window: started in the fence Ask's Codex runs
// in, asked once, and stopped, with what it wrote about the conversation
// removed. Every request it makes to act is refused (acp::Client), and it is
// given no stand-ins, so the words it writes are all that comes back.
class CodexTask : public QObject
{
    Q_OBJECT

public:
    CodexTask(const codex::Agent &agent, QObject *parent = nullptr);
    ~CodexTask() override;

    void start(const QString &prompt);
    void cancel();

Q_SIGNALS:
    void said(const QString &text);
    // text is every word it wrote; problem, when it couldn't answer, says why
    // in plain words.
    void finished(const QString &text, const QString &problem);

private:
    void session();
    void ask();
    void end(const QString &problem);

    codex::Agent m_agent;
    QString m_prompt;
    QString m_text;
    std::unique_ptr<QTemporaryDir> m_work;
    QPointer<acp::Client> m_client;
    bool m_done = false;
};

} // namespace splitrock::broker
