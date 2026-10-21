// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QString>
#include <QTimer>
#include <QUrl>

namespace splitrock::ask {

// llama-server, started inside the fence (Fence.h) on a free local port with a
// key only Ask knows, so no other program on the computer can use the model.
// It is stopped with Ask, or by stop().
class Engine : public QObject
{
    Q_OBJECT

public:
    explicit Engine(QObject *parent = nullptr);
    ~Engine() override;

    // The engine program and the model file, and how many of the model's
    // layers go to the graphics chip: "99" for all, "0" for none.
    void start(const QString &engine, const QString &model, const QString &layers);
    // A llama-server already running instead, for tests and trials.
    void use(const QUrl &server);
    void stop();

    bool isUp() const { return m_up; }
    bool isStarting() const { return m_starting; }
    QUrl url() const { return m_url; }
    QString key() const { return m_key; }
    QString problem() const { return m_problem; }

    // How long the model may take to load, in milliseconds.
    void setLoadLimit(int ms) { m_loadLimit = ms; }

Q_SIGNALS:
    void up();
    void down(const QString &problem);

private:
    void fail(const QString &why);
    void waitForHealth();

    QPointer<QProcess> m_process;
    QByteArray m_output;
    QNetworkAccessManager m_network;
    QElapsedTimer m_clock;
    QTimer m_poll;
    QUrl m_url;
    QString m_key;
    QString m_problem;
    bool m_up = false;
    bool m_starting = false;
    int m_loadLimit = 300000;
};

} // namespace splitrock::ask
