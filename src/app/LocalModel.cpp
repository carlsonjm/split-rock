// SPDX-License-Identifier: GPL-2.0-or-later
#include "app/LocalModel.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QStorageInfo>
#include <QThread>

namespace splitrock {

namespace {

QString gigabytes(qint64 bytes)
{
    const double gb = double(bytes) / 1e9;
    return gb < 1 ? QStringLiteral("under 1 GB") : QStringLiteral("about %1 GB").arg(qRound(gb * 2) / 2.0);
}

} // namespace

QList<LocalModel::Choice> LocalModel::catalog(const QString &path)
{
    QFile file(path);
    QList<Choice> list;
    if (!file.open(QIODevice::ReadOnly))
        return list;
    for (const QJsonValue &value : QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("models")).toArray()) {
        const QJsonObject object = value.toObject();
        Choice choice;
        choice.id = object.value(QStringLiteral("id")).toString();
        choice.file = QFileInfo(object.value(QStringLiteral("file")).toString()).fileName();
        choice.bytes = qint64(object.value(QStringLiteral("bytes")).toDouble());
        choice.sha256 = object.value(QStringLiteral("sha256")).toString().toLower();
        for (const QJsonValue &url : object.value(QStringLiteral("urls")).toArray()) {
            if (url.toString().startsWith(QLatin1String("https://")) || url.toString().startsWith(QLatin1String("http://127.0.0.1")))
                choice.urls.append(url.toString());
        }
        choice.credit = object.value(QStringLiteral("credit")).toString();
        const QJsonObject needs = object.value(QStringLiteral("needs")).toObject();
        choice.memoryGiB = needs.value(QStringLiteral("memoryGiB")).toDouble();
        choice.graphics = needs.value(QStringLiteral("graphics")).toBool();
        if (!choice.id.isEmpty() && !choice.file.isEmpty())
            list.append(choice);
    }
    return list;
}

LocalModel::Choice LocalModel::choose(const QList<Choice> &catalog, double memoryGiB, bool graphics)
{
    for (const Choice &choice : catalog) {
        if (memoryGiB >= choice.memoryGiB && (graphics || !choice.graphics))
            return choice;
    }
    return catalog.isEmpty() ? Choice() : catalog.last();
}

double LocalModel::memoryGiB(const QString &root)
{
    QFile file(QDir(root).filePath(QStringLiteral("proc/meminfo")));
    if (!file.open(QIODevice::ReadOnly))
        return 0;
    static const QRegularExpression total(QStringLiteral("^MemTotal:\\s+(\\d+)\\s+kB"), QRegularExpression::MultilineOption);
    return total.match(QString::fromUtf8(file.readAll())).captured(1).toDouble() / (1024.0 * 1024.0);
}

bool LocalModel::usableGraphics(const QString &devices, int neededMiB)
{
    // "  Vulkan0: AMD Radeon Graphics (RADV GFX1151) (65536 MiB, 63000 MiB free)"
    static const QRegularExpression device(QStringLiteral("^\\s*\\S+?\\d+: (.+) \\((\\d+) MiB, (\\d+) MiB free\\)\\s*$"),
                                           QRegularExpression::MultilineOption);
    static const QRegularExpression software(QStringLiteral("llvmpipe|lavapipe|softpipe|swiftshader"),
                                             QRegularExpression::CaseInsensitiveOption);
    for (auto it = device.globalMatch(devices); it.hasNext();) {
        const QRegularExpressionMatch match = it.next();
        if (!software.match(match.captured(1)).hasMatch() && match.captured(2).toInt() >= neededMiB)
            return true;
    }
    return false;
}

QString LocalModel::listDevices(const QString &engine)
{
    if (engine.isEmpty() || !QFileInfo(engine).isExecutable())
        return {};
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(engine, {QStringLiteral("--list-devices")});
    process.closeWriteChannel();
    if (!process.waitForFinished(15000)) {
        process.kill();
        process.waitForFinished(1000);
        return {};
    }
    return QString::fromUtf8(process.readAll());
}

LocalModel::LocalModel(const QString &engine, const QString &cataloguePath, const QString &models, QObject *parent)
    : QObject(parent)
    , m_engine(engine)
    , m_cataloguePath(cataloguePath)
    , m_models(models)
{
}

LocalModel::~LocalModel()
{
    cancel();
}

void LocalModel::survey()
{
    surveyAs(memoryGiB(), usableGraphics(listDevices(m_engine)));
}

void LocalModel::surveyAs(double memoryGiB, bool graphics)
{
    m_memory = memoryGiB;
    m_choice = choose(catalog(m_cataloguePath), memoryGiB, graphics);
    // Even the smaller model goes to a graphics chip with room for it.
    m_graphics = graphics || usableGraphics(listDevices(m_engine), 3000);
    m_surveyed = true;
    if (m_state != QLatin1String("downloading") && m_state != QLatin1String("verifying"))
        setState(!m_choice.file.isEmpty() && QFileInfo::exists(path()) ? QStringLiteral("ready") : QStringLiteral("missing"));
}

QString LocalModel::path() const
{
    return m_choice.file.isEmpty() ? QString() : QDir(m_models).filePath(m_choice.file);
}

QString LocalModel::size() const
{
    return gigabytes(m_choice.bytes);
}

QString LocalModel::why() const
{
    if (!m_surveyed || m_choice.id.isEmpty())
        return {};
    if (m_choice.graphics)
        return QStringLiteral("This computer has %1 GB of memory and a graphics chip that can run it, so it gets the "
                              "larger model, which makes fewer mistakes.").arg(qRound(m_memory));
    return QStringLiteral("It runs on this computer's own memory and graphics. Bigger fixes can still go to Codex.");
}

void LocalModel::setState(const QString &state, const QString &problem)
{
    m_state = state;
    m_problem = problem;
    Q_EMIT changed();
}

void LocalModel::download()
{
    if (m_state == QLatin1String("downloading") || m_state == QLatin1String("verifying") || m_state == QLatin1String("ready"))
        return;
    if (!m_surveyed)
        survey();
    if (m_choice.id.isEmpty() || m_choice.urls.isEmpty()) {
        setState(QStringLiteral("failed"), QStringLiteral("There's no model to download for this computer."));
        return;
    }
    // A model that can't be checked against its fingerprint is never run.
    if (m_choice.sha256.size() != 64) {
        setState(QStringLiteral("failed"), QStringLiteral("This model can't be checked yet, so it isn't downloaded."));
        return;
    }
    if (!QDir().mkpath(m_models)) {
        setState(QStringLiteral("failed"), QStringLiteral("The folder for the model couldn't be made."));
        return;
    }
    const qint64 room = QStorageInfo(m_models).bytesAvailable();
    const qint64 have = QFileInfo(path() + QStringLiteral(".part")).size();
    if (room >= 0 && room + have < m_choice.bytes + qint64(1) * 1000 * 1000 * 1000) {
        setState(QStringLiteral("failed"),
                 QStringLiteral("The download needs %1 free, plus a little room to spare. Free up space first.")
                     .arg(gigabytes(m_choice.bytes)));
        return;
    }
    if (!m_network)
        m_network = new QNetworkAccessManager(this);
    m_progress = m_choice.bytes > 0 ? double(have) / double(m_choice.bytes) : 0;
    setState(QStringLiteral("downloading"));
    tryUrl(0);
}

void LocalModel::tryUrl(int index)
{
    if (index >= m_choice.urls.size()) {
        setState(QStringLiteral("failed"), QStringLiteral("The download didn't finish. Check the internet connection, "
                                                          "then try again; it carries on where it stopped."));
        return;
    }
    m_part = std::make_unique<QFile>(path() + QStringLiteral(".part"));
    if (!m_part->open(QIODevice::ReadWrite | QIODevice::Append)) {
        setState(QStringLiteral("failed"), QStringLiteral("The model couldn't be saved: %1").arg(m_part->errorString()));
        return;
    }
    const qint64 have = m_part->size();
    if (m_choice.bytes > 0 && have >= m_choice.bytes) {
        m_part->close();
        verify();
        return;
    }
    QNetworkRequest request{QUrl(m_choice.urls.at(index))};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    if (have > 0)
        request.setRawHeader("Range", "bytes=" + QByteArray::number(have) + "-");
    QNetworkReply *reply = m_network->get(request);
    m_reply = reply;
    connect(reply, &QNetworkReply::metaDataChanged, this, [this, reply, have] {
        // A server that ignores the range sends the whole file again.
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (have > 0 && status == 200 && m_part) {
            m_part->resize(0);
            m_part->seek(0);
        }
    });
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        if (!m_part)
            return;
        m_part->write(reply->readAll());
        if (m_choice.bytes > 0) {
            const double now = double(m_part->size()) / double(m_choice.bytes);
            if (now - m_progress >= 0.002 || now >= 1) {
                m_progress = std::min(now, 1.0);
                Q_EMIT changed();
            }
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, index] {
        reply->deleteLater();
        if (m_reply == reply)
            m_reply = nullptr;
        if (!m_part)
            return;
        if (!reply->property("cancelled").toBool())
            m_part->write(reply->readAll());
        m_part->close();
        if (reply->property("cancelled").toBool())
            return;
        if (reply->error() != QNetworkReply::NoError) {
            tryUrl(index + 1);
            return;
        }
        verify();
    });
}

void LocalModel::verify()
{
    setState(QStringLiteral("verifying"));
    const QString part = path() + QStringLiteral(".part");
    const QString want = m_choice.sha256;
    const qint64 bytes = m_choice.bytes;
    // Reading several gigabytes back takes a while; it is done beside the
    // window, which keeps answering.
    QThread *thread = QThread::create([this, part, want, bytes] {
        QFile file(part);
        QString why;
        if (bytes > 0 && file.size() != bytes) {
            why = QStringLiteral("The download came out the wrong size.");
        } else if (!file.open(QIODevice::ReadOnly)) {
            why = file.errorString();
        } else {
            QCryptographicHash hash(QCryptographicHash::Sha256);
            hash.addData(&file);
            if (QString::fromLatin1(hash.result().toHex()) != want)
                why = QStringLiteral("The download didn't match the model's fingerprint.");
        }
        QMetaObject::invokeMethod(this, [this, part, why] {
            if (!why.isEmpty()) {
                // A file that doesn't match is never kept: the next try starts
                // afresh.
                QFile::remove(part);
                setState(QStringLiteral("failed"), why + QStringLiteral(" Try again."));
                return;
            }
            QFile::remove(path());
            if (!QFile::rename(part, path())) {
                setState(QStringLiteral("failed"), QStringLiteral("The model couldn't be put in place."));
                return;
            }
            m_progress = 1;
            setState(QStringLiteral("ready"));
        });
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void LocalModel::cancel()
{
    if (m_reply) {
        m_reply->setProperty("cancelled", true);
        m_reply->abort();
    }
    if (m_part) {
        m_part->close();
        m_part.reset();
    }
    if (m_state == QLatin1String("downloading"))
        setState(QStringLiteral("missing"));
}

} // namespace splitrock
