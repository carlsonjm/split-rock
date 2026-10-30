// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "ask/Engine.h"
#include "codex/Agent.h"
#include "desk/FrontDesk.h"
#include "guide/SkillShelf.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

#include <memory>

namespace splitrock {
class LocalModel;
}

namespace splitrock::ask {

// Ask on this computer (docs/ASK.md): one plain question at a time, answered
// by the front desk (desk/FrontDesk.h) in front of the model on this computer.
// It answers in words, names the Helpers that fit, and fills in a Do it card
// only from a settings skill. It changes nothing itself.
//
// Each question is asked afresh, with no earlier question in it, as Search
// asks one at a time. A question the desk's own safety check catches is
// answered with no model at all, even before the model is downloaded.
class Ask : public QObject, private desk::Tools
{
    Q_OBJECT

public:
    // What Ask reaches, given rather than found, so tests can use their own.
    struct Setup {
        // llama-server, or empty when it isn't installed.
        QString engine;
        // A llama-server already running instead of starting one, for tests
        // and trials.
        QString server;
        // The settings file where Ask is switched off: [Ask] Enabled=false.
        QString settings;
        // Where the settings skills are found, as the guide's folders.
        QStringList skillRoots;
        // Codex, which a bigger change is handed to once it is signed in.
        codex::Agent codex;
        // The broker, whose apps' tools the desk may use (docs/BROKER.md §
        // Tools), by its name on the session bus; empty for none.
        QString broker = QStringLiteral("io.github.carlsonjm.splitrock.Broker");
    };

    Ask(LocalModel *model, const Setup &setup, QObject *parent = nullptr);
    ~Ask() override;

    bool enabled() const;
    // Whether a bigger question goes straight to Codex where it is signed in:
    // [Ask] HandToCodex, off unless the person switched it on.
    bool handsToCodex() const;
    // Whether every question goes to Codex instead of the model on this
    // computer: [Ask] CodexOnly, the person's choice on Ask's first card or in
    // Settings. The safety check still answers here.
    bool codexOnly() const;
    void setCodexOnly(bool only);
    // Whether llama-server, which runs the model, is on this computer.
    bool hasEngine() const { return !m_setup.engine.isEmpty() || !m_setup.server.isEmpty(); }
    QString settings() const { return m_setup.settings; }
    // The question asked last, as answered gives its end.
    QString question() const { return m_question; }

    // Starts answering; the words come through said and the end through
    // answered, with the id returned here. A new question stops the one
    // before it, which ends answered with "cancelled".
    uint ask(const QString &question);
    void cancel(uint id);

    // What the model is doing: state (missing, downloading, verifying, ready,
    // failed), progress from 0 to 1, size and credit in words, and problem.
    QVariantMap model() const;
    // The words and shape the model sorts with, and what Ask tells the desk,
    // for the trial.
    QJsonObject framing() const;
    desk::Context context() const;

    // Whether a question is being answered or waits for the model.
    bool busy() const { return m_current != 0; }

Q_SIGNALS:
    void said(uint id, const QString &text);
    // The question is done. Keys: who ("computer"), text (every word said),
    // routed (how the desk handled it), helpers (the Helpers that fit, by
    // name), query (what the install Helper looks for), doIt (a Do it card
    // from a settings skill, or a card from an app's tool with the token
    // the broker runs it by, as JSON), read (what an app's tools read, by
    // name), handOff (Codex can take it), and
    // needsModel (with needsEngine when llama-server is missing), codexOnly,
    // off, cancelled or problem when it wasn't answered here.
    void answered(uint id, const QVariantMap &result);
    void modelChanged(const QVariantMap &model);

private:
    void openSkill(const QString &id, const std::function<void(const QString &)> &done) override;
    bool propose(const QJsonObject &proposal) override;
    void callTool(const QString &tool, const QVariantMap &arguments,
                  const std::function<void(const QVariantMap &)> &done) override;
    // Asks the broker for the apps' tools, then goes on.
    void withTools(const std::function<void()> &then);

    void begin();
    void finish(QVariantMap result);
    void engineUp();

    LocalModel *m_model;
    Setup m_setup;
    SkillShelf m_shelf;
    Engine m_engine;
    std::unique_ptr<desk::Model> m_client;
    std::unique_ptr<desk::FrontDesk> m_desk;
    uint m_next = 0;
    uint m_current = 0;
    bool m_cancelling = false;
    QString m_question;
    QString m_said;
    QString m_routed;
    QStringList m_helpers;
    QString m_query;
    QJsonObject m_doIt;
    QStringList m_read;
    // The apps' tools as the broker gave them for this question.
    QList<tools::Tool> m_appTools;
    // The last answer in words, which "save this answer" keeps.
    QString m_lastAnswer;
};

} // namespace splitrock::ask
