// SPDX-License-Identifier: GPL-2.0-or-later
#include "desk/Model.h"

#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>

namespace splitrock::desk {

QString withoutThinking(const QString &text)
{
    static const QRegularExpression closed(QStringLiteral("<think>.*?</think>"), QRegularExpression::DotMatchesEverythingOption);
    QString out = text;
    out.remove(closed);
    const qsizetype open = out.indexOf(QLatin1String("<think>"));
    if (open >= 0)
        out.truncate(open);
    return out;
}

Model::Model(const QUrl &server, const QString &key, QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
    , m_server(server)
    , m_key(key)
{
}

void Model::complete(const QJsonArray &messages, const QJsonObject &schema, int slot, int maxTokens,
                     const Chunk &chunk, const Done &done)
{
    if (m_reply) {
        done({}, QStringLiteral("busy"));
        return;
    }
    const bool streamed = schema.isEmpty();
    QJsonObject body{
        {QStringLiteral("messages"), messages},
        {QStringLiteral("temperature"), 0.2},
        {QStringLiteral("seed"), 7},
        {QStringLiteral("max_tokens"), maxTokens},
        {QStringLiteral("stream"), streamed},
        {QStringLiteral("cache_prompt"), true},
        {QStringLiteral("id_slot"), slot},
        // Qwen3's own switch: answer straight away rather than think first.
        {QStringLiteral("chat_template_kwargs"), QJsonObject{{QStringLiteral("enable_thinking"), false}}},
    };
    if (!schema.isEmpty()) {
        body.insert(QStringLiteral("response_format"),
                    QJsonObject{{QStringLiteral("type"), QStringLiteral("json_schema")},
                                {QStringLiteral("json_schema"),
                                 QJsonObject{{QStringLiteral("name"), QStringLiteral("reply")},
                                             {QStringLiteral("schema"), schema}}}});
    }
    QUrl url = m_server;
    url.setPath(QStringLiteral("/v1/chat/completions"));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!m_key.isEmpty())
        request.setRawHeader("Authorization", "Bearer " + m_key.toUtf8());
    QNetworkReply *reply = m_network->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    m_reply = reply;

    auto *limit = new QTimer(reply);
    limit->setSingleShot(true);
    connect(limit, &QTimer::timeout, reply, [reply] { reply->setProperty("timedOut", true); reply->abort(); });
    limit->start(m_timeLimit);

    // The words written so far, and what of the stream is not yet a line.
    auto written = std::make_shared<QString>();
    auto pending = std::make_shared<QByteArray>();
    auto shown = std::make_shared<qsizetype>(0);
    if (streamed) {
        connect(reply, &QNetworkReply::readyRead, this, [reply, written, pending, shown, chunk] {
            pending->append(reply->readAll());
            qsizetype end;
            while ((end = pending->indexOf('\n')) >= 0) {
                const QByteArray line = pending->left(end).trimmed();
                pending->remove(0, end + 1);
                if (!line.startsWith("data:"))
                    continue;
                const QByteArray data = line.mid(5).trimmed();
                if (data == "[DONE]")
                    continue;
                const QJsonObject event = QJsonDocument::fromJson(data).object();
                const QJsonArray choices = event.value(QStringLiteral("choices")).toArray();
                if (choices.isEmpty())
                    continue;
                written->append(choices.first().toObject().value(QStringLiteral("delta")).toObject()
                                    .value(QStringLiteral("content")).toString());
                // Only what is surely not part of a thinking block is shown.
                const QString clean = withoutThinking(*written);
                const qsizetype hold = clean.endsWith(QLatin1Char('<')) ? 1 : 0;
                if (clean.size() - hold > *shown) {
                    chunk(clean.mid(*shown, clean.size() - hold - *shown));
                    *shown = clean.size() - hold;
                }
            }
        });
    }
    connect(reply, &QNetworkReply::finished, this, [this, reply, written, shown, streamed, chunk, done] {
        reply->deleteLater();
        if (m_reply == reply)
            m_reply = nullptr;
        if (reply->property("cancelled").toBool()) {
            done({}, QStringLiteral("cancelled"));
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            done(withoutThinking(*written),
                 reply->property("timedOut").toBool() ? QStringLiteral("The model took too long.") : reply->errorString());
            return;
        }
        if (streamed) {
            const QString clean = withoutThinking(*written);
            if (clean.size() > *shown)
                chunk(clean.mid(*shown));
            done(clean.trimmed(), {});
            return;
        }
        const QJsonObject result = QJsonDocument::fromJson(reply->readAll()).object();
        const QJsonArray choices = result.value(QStringLiteral("choices")).toArray();
        const QString text = choices.isEmpty() ? QString()
                                               : choices.first().toObject().value(QStringLiteral("message")).toObject()
                                                     .value(QStringLiteral("content")).toString();
        done(withoutThinking(text).trimmed(), {});
    });
}

void Model::cancel()
{
    if (!m_reply)
        return;
    m_reply->setProperty("cancelled", true);
    m_reply->abort();
}

} // namespace splitrock::desk
