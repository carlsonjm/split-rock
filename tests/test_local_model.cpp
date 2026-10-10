// SPDX-License-Identifier: GPL-2.0-or-later
// Genie on this computer from Split Rock's side: which model a computer gets,
// its download, and the conversation with the front desk, against a pretend
// model server. No real model and no network are used.

#include "app/Assistants.h"
#include "app/Conversation.h"
#include "app/LocalModel.h"
#include "PretendModel.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace splitrock;

namespace {

// Serves one file, honouring a range, as a download server does.
class FileServer : public QObject
{
public:
    QByteArray data;
    int requests = 0;

    FileServer()
    {
        m_server.listen(QHostAddress::LocalHost, 0);
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                    const QByteArray request = socket->readAll();
                    if (!request.contains("\r\n\r\n"))
                        return;
                    ++requests;
                    qsizetype from = 0;
                    const qsizetype range = request.indexOf("Range: bytes=");
                    if (range >= 0)
                        from = request.mid(range + 13, request.indexOf('-', range + 13) - range - 13).toLongLong();
                    const QByteArray body = data.mid(from);
                    socket->write((from > 0 ? "HTTP/1.1 206 Partial Content\r\n" : "HTTP/1.1 200 OK\r\n")
                                  + QByteArray("Content-Length: ") + QByteArray::number(body.size())
                                  + "\r\nConnection: close\r\n\r\n" + body);
                    socket->disconnectFromHost();
                });
            }
        });
    }

    QString url() const { return QStringLiteral("http://127.0.0.1:%1/model.gguf").arg(m_server.serverPort()); }

private:
    QTcpServer m_server;
};

QString writeCatalogue(const QString &folder, const QByteArray &data, const QString &url, const QString &sha = QString())
{
    const QString sum = sha.isEmpty() ? QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex()) : sha;
    const QJsonObject small{{QStringLiteral("id"), QStringLiteral("small")},
                            {QStringLiteral("file"), QStringLiteral("small.gguf")},
                            {QStringLiteral("bytes"), double(data.size())},
                            {QStringLiteral("sha256"), sum},
                            {QStringLiteral("needs"), QJsonObject{{QStringLiteral("memoryGiB"), 0}, {QStringLiteral("graphics"), false}}},
                            {QStringLiteral("urls"), QJsonArray{url}}};
    const QJsonObject large{{QStringLiteral("id"), QStringLiteral("large")},
                            {QStringLiteral("file"), QStringLiteral("large.gguf")},
                            {QStringLiteral("bytes"), 5e9},
                            {QStringLiteral("sha256"), QString(64, QLatin1Char('a'))},
                            {QStringLiteral("needs"), QJsonObject{{QStringLiteral("memoryGiB"), 13}, {QStringLiteral("graphics"), true}}},
                            {QStringLiteral("urls"), QJsonArray{url}}};
    const QString path = QDir(folder).filePath(QStringLiteral("models.json"));
    QFile file(path);
    if (file.open(QIODevice::WriteOnly))
        file.write(QJsonDocument(QJsonObject{{QStringLiteral("models"), QJsonArray{large, small}}}).toJson());
    return path;
}

QByteArray someModel()
{
    QByteArray data;
    for (int i = 0; i < 300000; ++i)
        data.append(char(i * 7 % 251));
    return data;
}

} // namespace

class TestLocalModel : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void choosesByMemoryAndGraphics()
    {
        QTemporaryDir dir;
        const QList<LocalModel::Choice> list = LocalModel::catalog(writeCatalogue(dir.path(), "x", QStringLiteral("https://example.invalid/m")));
        QCOMPARE(list.size(), 2);
        QCOMPARE(LocalModel::choose(list, 30, true).id, QStringLiteral("large"));
        // Plenty of memory, but no graphics chip it can use: the smaller one.
        QCOMPARE(LocalModel::choose(list, 30, false).id, QStringLiteral("small"));
        QCOMPARE(LocalModel::choose(list, 7.6, true).id, QStringLiteral("small"));
        // A 16 GB computer reports a little under 16 GiB.
        QCOMPARE(LocalModel::choose(list, 14.9, true).id, QStringLiteral("large"));
    }

    void readsTheDevicesLlamaCppLists()
    {
        const QString amd = QStringLiteral("Available devices:\n  Vulkan0: AMD Radeon Graphics (RADV GFX1151) (65536 MiB, 63210 MiB free)\n");
        QVERIFY(LocalModel::usableGraphics(amd));
        const QString software = QStringLiteral("Available devices:\n  Vulkan0: llvmpipe (LLVM 19.1.7, 256 bits) (31876 MiB, 31876 MiB free)\n");
        QVERIFY(!LocalModel::usableGraphics(software));
        const QString small = QStringLiteral("Available devices:\n  Vulkan0: Intel(R) UHD Graphics 620 (KBL GT2) (3072 MiB, 3000 MiB free)\n");
        QVERIFY(!LocalModel::usableGraphics(small));
        QVERIFY(LocalModel::usableGraphics(small, 3000));
        QVERIFY(!LocalModel::usableGraphics(QStringLiteral("Available devices:\n")));
        QVERIFY(LocalModel::usableGraphics(QStringLiteral("  CUDA0: NVIDIA GeForce RTX 4060 Laptop GPU (8188 MiB, 7900 MiB free)")));
    }

    void downloadsChecksAndKeeps()
    {
        QTemporaryDir dir;
        FileServer server;
        server.data = someModel();
        LocalModel model(QString(), writeCatalogue(dir.path(), server.data, server.url()), dir.filePath(QStringLiteral("models")));
        model.surveyAs(8, false);
        QCOMPARE(model.state(), QStringLiteral("missing"));
        QCOMPARE(model.choice().id, QStringLiteral("small"));
        model.download();
        QTRY_COMPARE_WITH_TIMEOUT(model.state(), QStringLiteral("ready"), 10000);
        QFile kept(model.path());
        QVERIFY(kept.open(QIODevice::ReadOnly));
        QCOMPARE(kept.readAll(), server.data);
        QVERIFY(!QFile::exists(model.path() + QStringLiteral(".part")));
        // Already there next time.
        LocalModel again(QString(), writeCatalogue(dir.path(), server.data, server.url()), dir.filePath(QStringLiteral("models")));
        again.surveyAs(8, false);
        QVERIFY(again.isReady());
    }

    void carriesOnWhereItStopped()
    {
        QTemporaryDir dir;
        FileServer server;
        server.data = someModel();
        const QString models = dir.filePath(QStringLiteral("models"));
        QDir().mkpath(models);
        QFile part(QDir(models).filePath(QStringLiteral("small.gguf.part")));
        QVERIFY(part.open(QIODevice::WriteOnly));
        part.write(server.data.left(100000));
        part.close();
        LocalModel model(QString(), writeCatalogue(dir.path(), server.data, server.url()), models);
        model.surveyAs(8, false);
        model.download();
        QTRY_COMPARE_WITH_TIMEOUT(model.state(), QStringLiteral("ready"), 10000);
        QFile kept(model.path());
        QVERIFY(kept.open(QIODevice::ReadOnly));
        QCOMPARE(kept.readAll(), server.data);
    }

    void neverKeepsAFileThatDoesntMatch()
    {
        QTemporaryDir dir;
        FileServer server;
        server.data = someModel();
        LocalModel model(QString(), writeCatalogue(dir.path(), server.data, server.url(), QString(64, QLatin1Char('0'))),
                         dir.filePath(QStringLiteral("models")));
        model.surveyAs(8, false);
        model.download();
        QTRY_COMPARE_WITH_TIMEOUT(model.state(), QStringLiteral("failed"), 10000);
        QVERIFY(!QFile::exists(model.path()));
        QVERIFY(!QFile::exists(model.path() + QStringLiteral(".part")));
        QVERIFY(model.problem().contains(QLatin1String("fingerprint")));
    }

    void neverDownloadsWithoutAFingerprint()
    {
        QTemporaryDir dir;
        LocalModel model(QString(), QStringLiteral(PROJECT_DIR "/data/models.json"), dir.filePath(QStringLiteral("models")));
        model.surveyAs(8, false);
        QCOMPARE(model.choice().id, QStringLiteral("qwen3-4b"));
        model.surveyAs(31, true);
        QCOMPARE(model.choice().id, QStringLiteral("qwen3-8b"));
        if (model.choice().sha256.size() == 64)
            QSKIP("The shipped models have their fingerprints.");
        model.download();
        QCOMPARE(model.state(), QStringLiteral("failed"));
    }

    // The conversation with the front desk program: a model that's ready
    // starts it, the answer streams in, and a change no skill covers offers
    // the bigger assistant, which then gets the same question.
    void conversationHandsOffToTheBiggerAssistant()
    {
        QTemporaryDir dir;
        PretendModel pretendModel;
        pretendModel.answers.insert(QStringLiteral("What is KDE?"), QStringLiteral("It's the desktop you're using."));
        const QString printer = QStringLiteral("Set up my printer");
        pretendModel.routes.insert(printer, {{QStringLiteral("kind"), QStringLiteral("change")},
                                             {QStringLiteral("target"), QStringLiteral("none")},
                                             {QStringLiteral("worry"), QStringLiteral("none")},
                                             {QStringLiteral("query"), QString()}});

        const QString models = dir.filePath(QStringLiteral("models"));
        QDir().mkpath(models);
        QFile present(QDir(models).filePath(QStringLiteral("small.gguf")));
        QVERIFY(present.open(QIODevice::WriteOnly));
        present.close();
        LocalModel local(QStringLiteral("/bin/true"), writeCatalogue(dir.path(), "x", QStringLiteral("https://example.invalid/m")), models);
        local.surveyAs(8, false);
        QVERIFY(local.isReady());

        Assistant desk = localAssistant(QStringLiteral(DESK_PROGRAM), QStringLiteral("/bin/true"), dir.filePath(QStringLiteral("state")));
        desk.unavailable.clear();
        desk.mayRunUnfenced = true;
        desk.environment.insert(QStringLiteral("SPLIT_ROCK_DESK_SERVER"), pretendModel.url().toString());
        const Assistant bigger = pretendAssistant(QStringLiteral(PRETEND_PROGRAM), QStringLiteral(SCRIPTS_DIR "/hand-off.json"),
                                                  dir.filePath(QStringLiteral("state")));
        Conversation conversation({desk, bigger});
        conversation.setLocalModel(&local);
        conversation.chooseAssistant(QStringLiteral("local"));
        QTRY_VERIFY2_WITH_TIMEOUT(conversation.phase() == QLatin1String("ready"),
                                  qPrintable(conversation.phase() + QLatin1Char(' ') + conversation.problem() + QLatin1Char(' ')
                                             + conversation.problemDetails()), 15000);

        conversation.ask(QStringLiteral("What is KDE?"));
        QTRY_COMPARE_WITH_TIMEOUT(conversation.phase(), QStringLiteral("ready"), 15000);
        QCOMPARE(conversation.headline(), QStringLiteral("It's the desktop you're using."));
        QVERIFY(conversation.handOffTo().isEmpty());

        conversation.ask(printer);
        QTRY_COMPARE_WITH_TIMEOUT(conversation.handOffTo(), bigger.name, 15000);
        conversation.handOff();
        QCOMPARE(conversation.assistantId(), QStringLiteral("pretend"));
        QTRY_COMPARE_WITH_TIMEOUT(conversation.question(), printer, 15000);
        QTRY_VERIFY2_WITH_TIMEOUT(conversation.phase() == QLatin1String("ready") && conversation.headline() == QLatin1String("Let's add the printer."), qPrintable(conversation.phase() + QLatin1Char(' ') + conversation.problem() + QLatin1Char(' ') + conversation.problemDetails() + conversation.notice()), 15000);
    }

    void waitsForTheDownloadFirst()
    {
        QTemporaryDir dir;
        LocalModel local(QStringLiteral("/bin/true"), writeCatalogue(dir.path(), "x", QStringLiteral("https://example.invalid/m")),
                         dir.filePath(QStringLiteral("models")));
        local.surveyAs(8, false);
        Assistant desk = localAssistant(QStringLiteral(DESK_PROGRAM), QStringLiteral("/bin/true"), dir.filePath(QStringLiteral("state")));
        desk.unavailable.clear();
        desk.mayRunUnfenced = true;
        Conversation conversation({desk});
        conversation.setLocalModel(&local);
        conversation.chooseAssistant(QStringLiteral("local"));
        QCOMPARE(conversation.phase(), QStringLiteral("download"));
        // A question asked meanwhile waits, shown.
        conversation.ask(QStringLiteral("Where are my files?"));
        QCOMPARE(conversation.question(), QStringLiteral("Where are my files?"));
        QVERIFY(conversation.waiting());
    }
};

QTEST_GUILESS_MAIN(TestLocalModel)
#include "test_local_model.moc"
