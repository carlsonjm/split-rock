// SPDX-License-Identifier: GPL-2.0-or-later
#include "settings/AskSettings.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QSettings>

namespace splitrock {

namespace {
const QString Path = QStringLiteral("/Ask");
const QString Interface = QStringLiteral("io.github.carlsonjm.SplitRock.Ask");
QString service()
{
    return QString::fromLatin1(AskSettings::Service);
}
} // namespace

AskSettings::AskSettings(const QString &settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return;
    bus.connect(service(), Path, Interface, QStringLiteral("ModelChanged"), this, SLOT(takeModel(QVariantMap)));
    bus.connect(service(), Path, Interface, QStringLiteral("CodexChanged"), this, SLOT(takeCodex(QVariantMap)));
    QDBusConnectionInterface *names = bus.interface();
    m_installed = names
        && (names->isServiceRegistered(service()).value() || names->activatableServiceNames().value().contains(service()));
}

bool AskSettings::enabled() const
{
    return QSettings(m_settings, QSettings::IniFormat).value(QStringLiteral("Ask/Enabled"), true).toBool();
}

void AskSettings::setEnabled(bool on)
{
    if (on == enabled())
        return;
    QSettings settings(m_settings, QSettings::IniFormat);
    settings.setValue(QStringLiteral("Ask/Enabled"), on);
    settings.sync();
    Q_EMIT enabledChanged();
}

void AskSettings::refresh()
{
    if (!m_installed)
        return;
    const auto ask = [this](const QString &method, void (AskSettings::*take)(const QVariantMap &)) {
        const QDBusMessage request = QDBusMessage::createMethodCall(service(), Path, Interface, method);
        auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(request, 10000), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, take] {
            const QDBusPendingReply<QVariantMap> reply = *watcher;
            watcher->deleteLater();
            if (!reply.isError())
                (this->*take)(reply.value());
        });
    };
    ask(QStringLiteral("Model"), &AskSettings::takeModel);
    ask(QStringLiteral("Codex"), &AskSettings::takeCodex);
}

void AskSettings::setCodexOnly(bool only)
{
    call(QStringLiteral("SetCodexOnly"), {only});
}

void AskSettings::download()
{
    call(QStringLiteral("DownloadModel"));
}

void AskSettings::cancelDownload()
{
    call(QStringLiteral("CancelDownload"));
}

void AskSettings::remove()
{
    call(QStringLiteral("RemoveModel"));
    refresh();
}

void AskSettings::signInToCodex()
{
    // With no question, Codex's window opens on its sign-in.
    call(QStringLiteral("OpenCodex"), {QString()});
}

void AskSettings::signOutOfCodex()
{
    call(QStringLiteral("SignOutCodex"));
}

void AskSettings::takeModel(const QVariantMap &model)
{
    m_model = model;
    Q_EMIT modelChanged();
}

void AskSettings::takeCodex(const QVariantMap &codex)
{
    if (codex == m_codex)
        return;
    m_codex = codex;
    Q_EMIT codexChanged();
}

void AskSettings::call(const QString &method, const QVariantList &arguments)
{
    if (!m_installed)
        return;
    QDBusMessage message = QDBusMessage::createMethodCall(service(), Path, Interface, method);
    message.setArguments(arguments);
    QDBusConnection::sessionBus().asyncCall(message);
}

} // namespace splitrock
