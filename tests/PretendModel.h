// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>

// A pretend llama-server. What it answers is chosen by the person's last
// message: routes[ask] is the sorting, cards[ask] the card, fits[ask] the
// yes or no, answers[ask] the words, sent a few at a time.
class PretendModel : public QObject
{
public:
    QHash<QString, QJsonObject> routes;
    QHash<QString, QJsonObject> cards;
    QHash<QString, bool> fits;
    QHash<QString, QString> answers;
    QStringList asked;

    PretendModel()
    {
        m_server.listen(QHostAddress::LocalHost, 0);
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = m_server.nextPendingConnection())
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] { read(socket); });
        });
    }

    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort())); }

private:
    void read(QTcpSocket *socket)
    {
        QByteArray &buffer = m_buffers[socket];
        buffer += socket->readAll();
        const qsizetype headerEnd = buffer.indexOf("\r\n\r\n");
        if (headerEnd < 0)
            return;
        const QByteArray header = buffer.left(headerEnd).toLower();
        const qsizetype at = header.indexOf("content-length:");
        const int length = at < 0 ? 0 : header.mid(at + 15, header.indexOf("\r\n", at) - at - 15).trimmed().toInt();
        if (buffer.size() < headerEnd + 4 + length)
            return;
        const QJsonObject body = QJsonDocument::fromJson(buffer.mid(headerEnd + 4, length)).object();
        m_buffers.remove(socket);
        if (header.startsWith("get /health")) {
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: 15\r\n\r\n{\"status\":\"ok\"}");
            socket->disconnectFromHost();
            return;
        }
        reply(socket, body);
    }

    void reply(QTcpSocket *socket, const QJsonObject &body)
    {
        const QJsonArray messages = body.value(QStringLiteral("messages")).toArray();
        const QString last = messages.last().toObject().value(QStringLiteral("content")).toString();
        const QJsonObject schema = body.value(QStringLiteral("response_format")).toObject()
                                       .value(QStringLiteral("json_schema")).toObject().value(QStringLiteral("schema")).toObject();
        const QJsonObject properties = schema.value(QStringLiteral("properties")).toObject();
        QString content;
        if (properties.contains(QStringLiteral("kind"))) {
            asked.append(QStringLiteral("sort:") + last);
            content = QString::fromUtf8(QJsonDocument(routes.value(last, QJsonObject{{QStringLiteral("kind"), QStringLiteral("answer")},
                                                                                       {QStringLiteral("target"), QStringLiteral("none")},
                                                                                       {QStringLiteral("worry"), QStringLiteral("none")},
                                                                                       {QStringLiteral("query"), QString()}}))
                                            .toJson(QJsonDocument::Compact));
        } else if (properties.contains(QStringLiteral("command"))) {
            asked.append(QStringLiteral("card:") + last);
            content = QString::fromUtf8(QJsonDocument(cards.value(last)).toJson(QJsonDocument::Compact));
        } else if (properties.contains(QStringLiteral("fits"))) {
            QString ask;
            for (auto it = routes.cbegin(); it != routes.cend(); ++it) {
                if (last.contains(QLatin1Char('"') + it.key() + QLatin1Char('"')))
                    ask = it.key();
            }
            asked.append(QStringLiteral("fits:") + ask);
            content = QStringLiteral("{\"fits\": %1}").arg(fits.value(ask, true) ? QStringLiteral("true") : QStringLiteral("false"));
        } else {
            asked.append(QStringLiteral("answer:") + last);
            const QString text = answers.value(last, QStringLiteral("I don't know."));
            QByteArray out = "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nConnection: close\r\n\r\n";
            for (qsizetype i = 0; i < text.size(); i += 7) {
                const QJsonObject event{{QStringLiteral("choices"), QJsonArray{QJsonObject{
                    {QStringLiteral("delta"), QJsonObject{{QStringLiteral("content"), text.mid(i, 7)}}}}}}};
                out += "data: " + QJsonDocument(event).toJson(QJsonDocument::Compact) + "\n\n";
            }
            out += "data: [DONE]\n\n";
            socket->write(out);
            socket->disconnectFromHost();
            return;
        }
        const QByteArray json = QJsonDocument(QJsonObject{{QStringLiteral("choices"), QJsonArray{QJsonObject{
            {QStringLiteral("message"), QJsonObject{{QStringLiteral("content"), content}}}}}}}).toJson(QJsonDocument::Compact);
        socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: "
                      + QByteArray::number(json.size()) + "\r\n\r\n" + json);
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_buffers;
};

