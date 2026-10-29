// SPDX-License-Identifier: GPL-2.0-or-later
#include "ask/AskService.h"

#include "ask/Ask.h"
#include "ask/LocalModel.h"

#include <QDir>
#include <QProcess>

namespace splitrock::ask {

AskService::AskService(ask::Ask *asking, LocalModel *model, const codex::Agent &codex, const QString &window,
                       QObject *parent)
    : QObject(parent)
    , m_ask(asking)
    , m_model(model)
    , m_codex(codex)
    , m_window(window)
{
    connect(m_ask, &ask::Ask::said, this, &AskService::Said);
    connect(m_ask, &ask::Ask::answered, this, [this](uint id, QVariantMap result) {
        // A bigger question goes straight on to Codex when the person
        // switched that on and Codex is signed in; the answer says so.
        if (result.value(QStringLiteral("handOff")).toBool() && m_ask->handsToCodex()
            && Codex().value(QStringLiteral("signedIn")).toBool() && OpenCodex(m_ask->question())) {
            result.insert(QStringLiteral("handedToCodex"), true);
            result.insert(QStringLiteral("text"),
                          QStringLiteral("This is a bigger one, so Codex is taking it, in its own window."));
        }
        Q_EMIT Answered(id, result);
    });
    connect(m_ask, &ask::Ask::modelChanged, this, &AskService::ModelChanged);
    m_signedIn = m_codex.signedIn();
    connect(&m_watch, &QFileSystemWatcher::directoryChanged, this, [this] {
        watchCodex();
        if (m_codex.signedIn() == m_signedIn)
            return;
        m_signedIn = !m_signedIn;
        Q_EMIT CodexChanged(Codex());
    });
    watchCodex();
}

void AskService::watchCodex()
{
    // Codex's folder, where its sign-in comes and goes.
    if (m_codex.home.isEmpty() || !QDir().mkpath(m_codex.home))
        return;
    if (!m_watch.directories().contains(m_codex.home))
        m_watch.addPath(m_codex.home);
}

bool AskService::publish(QDBusConnection bus)
{
    return bus.registerObject(QLatin1String(Path), this, QDBusConnection::ExportScriptableContents);
}

uint AskService::ProtocolVersion() const
{
    return Version;
}

bool AskService::Enabled() const
{
    return m_ask->enabled();
}

uint AskService::Ask(const QString &question)
{
    return m_ask->ask(question);
}

void AskService::Cancel(uint id)
{
    m_ask->cancel(id);
}

QVariantMap AskService::Model() const
{
    return m_ask->model();
}

void AskService::DownloadModel()
{
    // Only after the person's tap, which is the caller's to ask for.
    m_model->download();
}

void AskService::CancelDownload()
{
    m_model->cancel();
}

void AskService::RemoveModel()
{
    m_model->remove();
}

QVariantMap AskService::Codex() const
{
    return {{QStringLiteral("installed"), m_codex.unavailable.isEmpty()},
            {QStringLiteral("signedIn"), m_codex.unavailable.isEmpty() && m_codex.signedIn()},
            {QStringLiteral("handOff"), m_ask->handsToCodex()},
            {QStringLiteral("only"), m_codex.unavailable.isEmpty() && m_ask->codexOnly()},
            {QStringLiteral("problem"), m_codex.unavailable}};
}

bool AskService::OpenCodex(const QString &question)
{
    if (!m_codex.unavailable.isEmpty() || m_window.isEmpty())
        return false;
    QStringList arguments{QStringLiteral("--codex")};
    if (!question.trimmed().isEmpty())
        arguments << QStringLiteral("--question") << question.trimmed().left(2000);
    return QProcess::startDetached(m_window, arguments);
}

bool AskService::SignOutCodex()
{
    const bool done = m_codex.signOut();
    if (m_signedIn != m_codex.signedIn()) {
        m_signedIn = m_codex.signedIn();
        Q_EMIT CodexChanged(Codex());
    }
    return done;
}

bool AskService::SetCodexOnly(bool only)
{
    if (only && !m_codex.unavailable.isEmpty())
        return false;
    if (m_ask->codexOnly() != only) {
        m_ask->setCodexOnly(only);
        Q_EMIT CodexChanged(Codex());
    }
    return true;
}

} // namespace splitrock::ask
