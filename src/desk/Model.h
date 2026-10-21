// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QUrl>

#include <functional>

class QNetworkAccessManager;
#include <QNetworkReply>

namespace splitrock::desk {

// The model on this computer, through llama.cpp's llama-server and its
// OpenAI-style chat endpoint. One request at a time.
class Model : public QObject
{
    Q_OBJECT

public:
    using Chunk = std::function<void(const QString &text)>;
    using Done = std::function<void(const QString &text, const QString &error)>;

    Model(const QUrl &server, const QString &key, QObject *parent = nullptr);

    // Sends messages to the model. With a schema, the server holds the reply
    // to it and the reply comes whole; without one, the words come as they are
    // written, through chunk. slot keeps a conversation's start in the
    // server's memory between requests: the sorting and the answers each keep
    // their own.
    //
    // A reply that ends early says why in error, with what was written before
    // it: "cancelled", "looping" when the words went round in circles (they
    // stop where the circling began), or the time limit. timeLimit, in
    // milliseconds, is this reply's own; 0 keeps the model's.
    void complete(const QJsonArray &messages, const QJsonObject &schema, int slot, int maxTokens,
                  const Chunk &chunk, const Done &done, int timeLimit = 0);
    void cancel();
    bool busy() const { return m_reply != nullptr; }

    // How long a reply may take, in milliseconds.
    void setTimeLimit(int ms) { m_timeLimit = ms; }

private:
    QNetworkReply *send(const QString &path, const QJsonObject &body, int timeLimit);
    void post(const QString &path, const QJsonObject &body,
              const std::function<void(const QByteArray &, const QString &)> &done, int timeLimit = 0);
    void stream(const QJsonObject &body, const Chunk &chunk, const Done &done, int timeLimit);

    QNetworkAccessManager *m_network;
    QUrl m_server;
    QString m_key;
    QPointer<QNetworkReply> m_reply;
    int m_timeLimit = 180000;
};

// Where the end of what a model wrote starts going round in circles, or -1.
qsizetype loopStart(const QString &text);

// Removes a thinking block, whole or still open, from what a model wrote.
QString withoutThinking(const QString &text);

} // namespace splitrock::desk
