// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "ask/Engine.h"
#include "codex/Agent.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

namespace splitrock {
class LocalModel;
}

namespace splitrock::desk {
class Model;
}

namespace splitrock::broker {

class CodexTask;

// The broker (docs/BROKER.md): the models, for the Good Input apps that ask.
// An app hands it material and gets a structured result back; the broker
// reaches no app, no mail server and no file of the person's, and the app does
// every action itself, after the person's tap where one is needed.
//
// The model on this computer sorts and summarises on an enforced form, one
// request at a time, with no tools. Codex drafts and answers only what an app
// passes it, on the person's tap, in the fence Ask's Codex runs in, with every
// request it makes to act refused.
class Broker : public QObject
{
    Q_OBJECT

public:
    struct Setup {
        // llama-server, or empty when it isn't installed.
        QString engine;
        // A llama-server already running instead of starting one, for tests.
        QString server;
        codex::Agent codex;
    };

    Broker(LocalModel *model, const Setup &setup, QObject *parent = nullptr);
    ~Broker() override;

    // Starts the model ahead of the first request, where it is downloaded.
    void warm();
    QVariantMap model() const;
    // Codex: installed, signedIn, and problem when it can't start.
    QVariantMap codex() const;

    // Each returns the request's id; the result comes in sorted, answered,
    // drafted or failed with it.
    uint sortMail(const QVariantMap &message);
    uint styleNote(const QVariantList &sent);
    uint draftReply(const QVariantMap &conversation, const QString &styleNote, const QString &ask);
    uint clerk(const QString &question, const QVariantList &found);
    void cancel(uint id);

    bool busy() const;

    // The words and shape the model sorts mail with, for tests and the trial.
    static QString sortingWords();
    static QJsonObject sortingShape();
    // An email as the model is given it: material set apart, never
    // instructions.
    static QString mailText(const QVariantMap &message, int bodyLimit = 4000);
    // The sorting the model wrote, made safe to show: the known values only,
    // and one plain line. Empty when it doesn't fit the form.
    static QVariantMap sortingFrom(const QString &reply, const QString &id);
    // Proposals at the end of what Codex wrote, as the app shows them as cards,
    // with the words before them. Only the actions and messages it was given.
    static QVariantList proposalsIn(QString *text, const QStringList &ids);

Q_SIGNALS:
    void sorted(uint id, const QVariantMap &result);
    void said(uint id, const QString &text);
    void drafted(uint id, const QVariantMap &result);
    void answered(uint id, const QVariantMap &result);
    void failed(uint id, const QString &problem);
    void modelChanged(const QVariantMap &model);

private:
    struct Job {
        uint id = 0;
        QString kind;
        QVariantMap message;
        QVariantList sent;
        bool again = false;
    };

    void next();
    void run(const Job &job);
    void engineUp();
    void finishLocal();
    uint codexTask(const QString &kind, const QString &prompt, const QStringList &ids);

    LocalModel *m_model;
    Setup m_setup;
    ask::Engine m_engine;
    std::unique_ptr<desk::Model> m_client;
    QList<Job> m_queue;
    uint m_current = 0;
    uint m_next = 0;
    QString m_said;
    QHash<uint, QPointer<CodexTask>> m_codex;
};

} // namespace splitrock::broker
