// SPDX-License-Identifier: GPL-2.0-or-later
// The model Ask runs on this computer: which model a computer gets, and its
// download, against a file server of the test's own. No real model and no
// network are used.

#include "ask/LocalModel.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
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
        QVERIFY(!LocalModel::usableGraphics(small, 3000));
        // Intel's built-in graphics answer wrongly; the processor answers.
        const QString mini = QStringLiteral("Available devices:\n  Vulkan0: Intel(R) UHD Graphics 630 (CFL GT2) (15946 MiB, 15000 MiB free)\n");
        QVERIFY(!LocalModel::usableGraphics(mini));
        QVERIFY(!LocalModel::usableGraphics(mini, 3000));
        QVERIFY(!LocalModel::usableGraphics(QStringLiteral("  Vulkan0: Intel(R) Iris(R) Xe Graphics (TGL GT2) (15000 MiB, 14000 MiB free)"), 3000));
        QVERIFY(LocalModel::usableGraphics(QStringLiteral("  Vulkan0: Intel(R) Arc(tm) A770 Graphics (DG2) (16032 MiB, 16000 MiB free)")));
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
        // One model for every computer (docs/DECISIONS.md).
        model.surveyAs(31, true);
        QCOMPARE(model.choice().id, QStringLiteral("qwen3-4b"));
        // The licence asks for the model's credit, which the panel shows.
        QVERIFY(model.choice().credit.contains(QLatin1String("Apache 2.0")));
        QCOMPARE(model.choice().name, QStringLiteral("Qwen3 4B"));
        if (model.choice().sha256.size() == 64)
            QSKIP("The shipped models have their fingerprints.");
        model.download();
        QCOMPARE(model.state(), QStringLiteral("failed"));
    }

    void removesTheModel()
    {
        QTemporaryDir dir;
        const QString models = dir.filePath(QStringLiteral("models"));
        QDir().mkpath(models);
        for (const char *name : {"small.gguf", "small.gguf.part"}) {
            QFile file(QDir(models).filePath(QLatin1String(name)));
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
        LocalModel model(QStringLiteral("/bin/true"), writeCatalogue(dir.path(), "x", QStringLiteral("https://example.invalid/m")),
                         models);
        model.surveyAs(8, false);
        QVERIFY(model.isReady());
        model.remove();
        QCOMPARE(model.state(), QStringLiteral("missing"));
        QVERIFY(!QFile::exists(model.path()));
        QVERIFY(!QFile::exists(model.path() + QStringLiteral(".part")));
    }
};

QTEST_GUILESS_MAIN(TestLocalModel)
#include "test_local_model.moc"
