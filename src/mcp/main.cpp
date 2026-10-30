// SPDX-License-Identifier: GPL-2.0-or-later
//
// split-rock-tools-mcp: the apps' tools for Codex (docs/BROKER.md § Codex).
// Codex starts it inside its fence as a Model Context Protocol server on its
// standard input and output. It only forwards: each message goes, as it came,
// through the socket Split Rock holds outside the fence, and each answer
// comes back the same way. It knows no tool, reads no file and reaches no
// app; every check is made outside, by Split Rock and the broker.
//
//   split-rock-tools-mcp SOCKET

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>

#include <cstdio>
#include <iostream>
#include <string>

namespace {

// The longest answer waited for: a call goes on to an app through the broker.
constexpr int AnswerTime = 60000;

void write(const QByteArray &line)
{
    std::fwrite(line.constData(), 1, size_t(line.size()), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().size() != 2) {
        std::fputs("usage: split-rock-tools-mcp SOCKET\n", stderr);
        return 2;
    }
    QLocalSocket door;
    door.connectToServer(app.arguments().at(1));
    if (!door.waitForConnected(5000)) {
        std::fputs("split-rock-tools-mcp: Split Rock isn't answering\n", stderr);
        return 1;
    }
    std::string text;
    while (std::getline(std::cin, text)) {
        const QByteArray line = QByteArray::fromStdString(text).trimmed();
        if (line.isEmpty())
            continue;
        door.write(line + '\n');
        if (!door.waitForBytesWritten(5000))
            return 1;
        // A request has an id and waits for its answer; a notification has
        // none and is answered by nothing.
        const QJsonValue id = QJsonDocument::fromJson(line).object().value(QStringLiteral("id"));
        if (id.isUndefined() || id.isNull())
            continue;
        while (!door.canReadLine()) {
            if (!door.waitForReadyRead(AnswerTime))
                return 1;
        }
        write(door.readLine().trimmed());
    }
    return 0;
}
