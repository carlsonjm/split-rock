// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QFile>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <memory>

class QNetworkAccessManager;
class QNetworkReply;

namespace splitrock {

// The model Genie on this computer runs, and its one-time download
// (docs/DECISIONS.md, Genie on this computer). Only the model that fits the
// computer is downloaded: the larger one where there are 16 GB of memory and a
// graphics chip it can use, the smaller one everywhere else.
class LocalModel : public QObject
{
    Q_OBJECT
    // missing, downloading, verifying, ready or failed.
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    // "about 5 GB", for the person.
    Q_PROPERTY(QString size READ size NOTIFY changed)
    // Why this computer gets this model, in plain words.
    Q_PROPERTY(QString why READ why NOTIFY changed)
    Q_PROPERTY(QString problem READ problem NOTIFY changed)

public:
    struct Choice {
        QString id;
        QString file;
        // The exact size, and the SHA-256 the download must have. A model
        // without one is not downloaded.
        qint64 bytes = 0;
        QString sha256;
        QStringList urls;
        // What it needs: memory in GiB as the system reports it, and a
        // graphics chip.
        double memoryGiB = 0;
        bool graphics = false;
    };

    // The models on offer, larger first, from models.json.
    static QList<Choice> catalog(const QString &path);
    // The first model this computer meets the needs of, or the last.
    static Choice choose(const QList<Choice> &catalog, double memoryGiB, bool graphics);
    // The computer's memory, in GiB, from /proc/meminfo (root for tests).
    static double memoryGiB(const QString &root = QStringLiteral("/"));
    // Whether llama.cpp's own list of devices, as `llama-server
    // --list-devices` prints it, holds a graphics chip with room for a model:
    // not a software one, and with this many MiB or more.
    static bool usableGraphics(const QString &devices, int neededMiB = 6000);
    static QString listDevices(const QString &engine);

    // engine is llama-server, models the folder the model is kept in.
    LocalModel(const QString &engine, const QString &cataloguePath, const QString &models, QObject *parent = nullptr);
    ~LocalModel() override;

    // Reads the computer and picks the model. Runs llama-server once, so it
    // is not done at construction.
    void survey();
    // For tests: the computer as given rather than read.
    void surveyAs(double memoryGiB, bool graphics);

    QString engine() const { return m_engine; }
    Choice choice() const { return m_choice; }
    // The model's file, where it is kept once downloaded.
    QString path() const;
    // How many of the model's layers go to the graphics chip.
    QString layers() const { return m_graphics ? QStringLiteral("99") : QStringLiteral("0"); }
    bool isReady() const { return m_state == QLatin1String("ready"); }

    QString state() const { return m_state; }
    double progress() const { return m_progress; }
    QString size() const;
    QString why() const;
    QString problem() const { return m_problem; }

    Q_INVOKABLE void download();
    Q_INVOKABLE void cancel();

Q_SIGNALS:
    void changed();

private:
    void setState(const QString &state, const QString &problem = QString());
    void tryUrl(int index);
    void verify();

    QString m_engine;
    QString m_cataloguePath;
    QString m_models;
    Choice m_choice;
    bool m_surveyed = false;
    double m_memory = 0;
    bool m_graphics = false;
    QString m_state = QStringLiteral("missing");
    QString m_problem;
    double m_progress = 0;
    QNetworkAccessManager *m_network = nullptr;
    QPointer<QNetworkReply> m_reply;
    std::unique_ptr<QFile> m_part;
};

} // namespace splitrock
