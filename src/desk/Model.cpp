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

namespace {

// The last characters written, which may still turn out to start a loop or a
// thinking block, are held back until more comes.
constexpr qsizetype HeldBack = 48;

} // namespace

qsizetype loopStart(const QString &text)
{
    // A short piece said eight times over, such as "a a a a" or "1111", or a
    // longer one four times, at the end of what was written so far.
    static const QRegularExpression shortLoop(QStringLiteral("(.{1,6}?)\\1{7,}$"), QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression longLoop(QStringLiteral("(.{7,60}?)\\1{3,}$"), QRegularExpression::DotMatchesEverythingOption);
    const QString tail = text.right(400);
    const qsizetype offset = text.size() - tail.size();
    for (const QRegularExpression *loop : {&shortLoop, &longLoop}) {
        const QRegularExpressionMatch match = loop->match(tail);
        if (match.hasMatch() && !match.captured(1).trimmed().isEmpty())
            return offset + match.capturedStart();
    }
    // Hundreds of characters with nothing to read in them.
    if (text.size() > 400 && text.trimmed().isEmpty())
        return 0;
    return -1;
}

void Model::complete(const QJsonArray &messages, const QJsonObject &schema, int slot, int maxTokens,
                     const Chunk &chunk, const Done &done, int timeLimit)
{
    if (m_reply) {
        done({}, QStringLiteral("busy"));
        return;
    }
    // Qwen3's own switch: answer straight away rather than think first.
    const QJsonObject noThinking{{QStringLiteral("enable_thinking"), false}};
    if (schema.isEmpty()) {
        // Words as they are written come from the plain completion, with the
        // chat laid out by the server's own template first: the chat endpoint
        // reads its partial words as a message and may send one twice.
        post(QStringLiteral("/apply-template"),
             QJsonObject{{QStringLiteral("messages"), messages}, {QStringLiteral("chat_template_kwargs"), noThinking}},
             [this, slot, maxTokens, chunk, done, timeLimit](const QByteArray &body, const QString &error) {
            const QString prompt = QJsonDocument::fromJson(body).object().value(QStringLiteral("prompt")).toString();
            if (!error.isEmpty() || prompt.isEmpty()) {
                done({}, error.isEmpty() ? QStringLiteral("The model couldn't read the question.") : error);
                return;
            }
            stream(QJsonObject{
                       {QStringLiteral("prompt"), prompt},
                       // Qwen3's settings for answers without thinking; a
                       // lower temperature talks in circles.
                       {QStringLiteral("temperature"), 0.7},
                       {QStringLiteral("top_p"), 0.8},
                       {QStringLiteral("top_k"), 20},
                       {QStringLiteral("min_p"), 0.0},
                       {QStringLiteral("presence_penalty"), 1.0},
                       {QStringLiteral("seed"), 7},
                       {QStringLiteral("n_predict"), maxTokens},
                       {QStringLiteral("stream"), true},
                       {QStringLiteral("cache_prompt"), true},
                       {QStringLiteral("id_slot"), slot},
                   },
                   chunk, done, timeLimit);
        });
        return;
    }
    const QJsonObject body{
        {QStringLiteral("messages"), messages},
        {QStringLiteral("temperature"), 0.2},
        {QStringLiteral("top_p"), 0.8},
        {QStringLiteral("top_k"), 20},
        {QStringLiteral("seed"), 7},
        {QStringLiteral("max_tokens"), maxTokens},
        {QStringLiteral("stream"), false},
        {QStringLiteral("cache_prompt"), true},
        {QStringLiteral("id_slot"), slot},
        {QStringLiteral("chat_template_kwargs"), noThinking},
        {QStringLiteral("response_format"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("json_schema")},
                     {QStringLiteral("json_schema"),
                      QJsonObject{{QStringLiteral("name"), QStringLiteral("reply")}, {QStringLiteral("schema"), schema}}}}},
    };
    post(QStringLiteral("/v1/chat/completions"), body, [done](const QByteArray &reply, const QString &error) {
        if (!error.isEmpty()) {
            done({}, error);
            return;
        }
        const QJsonArray choices = QJsonDocument::fromJson(reply).object().value(QStringLiteral("choices")).toArray();
        const QString text = choices.isEmpty() ? QString()
                                               : choices.first().toObject().value(QStringLiteral("message")).toObject()
                                                     .value(QStringLiteral("content")).toString();
        done(withoutThinking(text).trimmed(), {});
    }, timeLimit);
}

QNetworkReply *Model::send(const QString &path, const QJsonObject &body, int timeLimit)
{
    QUrl url = m_server;
    url.setPath(path);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!m_key.isEmpty())
        request.setRawHeader("Authorization", "Bearer " + m_key.toUtf8());
    QNetworkReply *reply = m_network->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    m_reply = reply;
    auto *limit = new QTimer(reply);
    limit->setSingleShot(true);
    connect(limit, &QTimer::timeout, reply, [reply] { reply->setProperty("timedOut", true); reply->abort(); });
    limit->start(timeLimit > 0 ? timeLimit : m_timeLimit);
    return reply;
}

// Why a reply ended early, or empty when it didn't.
static QString endedEarly(QNetworkReply *reply)
{
    if (reply->property("cancelled").toBool())
        return QStringLiteral("cancelled");
    if (reply->property("looping").toBool())
        return QStringLiteral("looping");
    if (reply->property("timedOut").toBool())
        return QStringLiteral("The model took too long.");
    if (reply->error() != QNetworkReply::NoError)
        return reply->errorString();
    return {};
}

void Model::post(const QString &path, const QJsonObject &body,
                 const std::function<void(const QByteArray &, const QString &)> &done, int timeLimit)
{
    QNetworkReply *reply = send(path, body, timeLimit);
    connect(reply, &QNetworkReply::finished, this, [this, reply, done] {
        reply->deleteLater();
        if (m_reply == reply)
            m_reply = nullptr;
        const QString error = endedEarly(reply);
        done(error.isEmpty() ? reply->readAll() : QByteArray(), error);
    });
}

void Model::stream(const QJsonObject &body, const Chunk &chunk, const Done &done, int timeLimit)
{
    QNetworkReply *reply = send(QStringLiteral("/completion"), body, timeLimit);
    // The words written so far, what of the stream is not yet a line, and how
    // much has been shown.
    auto written = std::make_shared<QString>();
    auto pending = std::make_shared<QByteArray>();
    auto shown = std::make_shared<qsizetype>(0);
    // What may be shown of the words: no thinking block, and nothing from
    // where they began to go round in circles.
    auto sayable = [written]() {
        QString clean = withoutThinking(*written);
        const qsizetype loop = loopStart(clean);
        if (loop >= 0)
            clean.truncate(loop);
        return clean;
    };
    connect(reply, &QNetworkReply::readyRead, this, [reply, written, pending, shown, chunk, sayable] {
        pending->append(reply->readAll());
        qsizetype end;
        while ((end = pending->indexOf('\n')) >= 0) {
            const QByteArray line = pending->left(end).trimmed();
            pending->remove(0, end + 1);
            if (!line.startsWith("data:"))
                continue;
            const QJsonObject event = QJsonDocument::fromJson(line.mid(5).trimmed()).object();
            written->append(event.value(QStringLiteral("content")).toString());
        }
        if (loopStart(withoutThinking(*written)) >= 0) {
            reply->setProperty("looping", true);
            reply->abort();
            return;
        }
        const QString clean = sayable();
        const qsizetype ready = std::max<qsizetype>(*shown, clean.size() - HeldBack);
        if (ready > *shown) {
            chunk(clean.mid(*shown, ready - *shown));
            *shown = ready;
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, written, shown, chunk, done, sayable] {
        reply->deleteLater();
        if (m_reply == reply)
            m_reply = nullptr;
        const QString error = endedEarly(reply);
        if (error == QLatin1String("cancelled")) {
            done({}, error);
            return;
        }
        const QString clean = sayable();
        if (clean.size() > *shown)
            chunk(clean.mid(*shown));
        done(clean.trimmed(), error);
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
