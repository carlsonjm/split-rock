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
    void complete(const QJsonArray &messages, const QJsonObject &schema, int slot, int maxTokens,
                  const Chunk &chunk, const Done &done);
    void cancel();
    bool busy() const { return m_reply != nullptr; }

    // How long a reply may take, in milliseconds.
    void setTimeLimit(int ms) { m_timeLimit = ms; }

private:
    QNetworkAccessManager *m_network;
    QUrl m_server;
    QString m_key;
    QPointer<QNetworkReply> m_reply;
    int m_timeLimit = 180000;
};

// Removes a thinking block, whole or still open, from what a model wrote.
QString withoutThinking(const QString &text);

} // namespace splitrock::desk
