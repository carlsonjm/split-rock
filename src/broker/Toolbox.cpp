// SPDX-License-Identifier: GPL-2.0-or-later
#include "broker/Toolbox.h"

#include "broker/BrokerService.h"

#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QRandomGenerator>

namespace splitrock::broker {

namespace {

QVariantMap refused(const QString &why)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("problem"), why}};
}

QString newToken()
{
    QByteArray bytes(16, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(bytes.data()), bytes.size() / 4);
    return QString::fromLatin1(bytes.toHex());
}

} // namespace

Toolbox::Toolbox(const QStringList &folders, const QDBusConnection &bus, QObject *parent)
    : QObject(parent)
    , m_capabilities(folders)
    , m_bus(bus)
{
}

QVariantList Toolbox::tools()
{
    m_capabilities.read();
    for (const QString &problem : m_capabilities.problems())
        qWarning().noquote() << QStringLiteral("split-rock-broker: capabilities: ") + problem;
    QVariantList list;
    for (const tools::Tool &tool : m_capabilities.tools())
        list.append(tool.toMap());
    return list;
}

QString Toolbox::cardTitle(const tools::Tool &tool, const QVariantList &positional)
{
    // The words the person will recognise: the longest text it writes.
    QString longest;
    for (const QVariant &value : positional) {
        if (value.typeId() == QMetaType::QString && value.toString().size() > longest.size())
            longest = value.toString();
    }
    const QStringList words = longest.simplified().remove(QLatin1Char('#')).simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QString title = words.mid(0, 7).join(QLatin1Char(' '));
    if (title.size() > 60)
        title = title.left(59).trimmed();
    if (title.isEmpty())
        return tool.summary;
    return words.size() > 7 || title.size() < words.join(QLatin1Char(' ')).size() ? title + QChar(0x2026) : title;
}

void Toolbox::call(const QString &name, const QVariantMap &arguments, const QString &caller, const Reply &reply)
{
    Q_EMIT used();
    // Read afresh every time, so an app removed a moment ago is gone.
    m_capabilities.read();
    const std::optional<tools::Tool> tool = m_capabilities.tool(name);
    const QString who = caller.simplified().left(40);
    if (!tool) {
        log(name.left(80), who, QStringLiteral("refused: no such tool"));
        reply(refused(QStringLiteral("No app on this computer offers %1.").arg(name.left(80))));
        return;
    }
    if (tool->risk == QLatin1String("outward")) {
        log(tool->name, who, QStringLiteral("refused: outward"));
        reply(refused(QStringLiteral("%1 sends something off this computer, which isn't offered yet.").arg(tool->name)));
        return;
    }
    if (who == QLatin1String("codex") && !tool->codex) {
        log(tool->name, who, QStringLiteral("refused: not for Codex"));
        reply(refused(QStringLiteral("%1 doesn't let Codex use %2.").arg(tool->appName, tool->name)));
        return;
    }
    QVariantList positional;
    const QString why = tools::Capabilities::check(*tool, arguments, &positional);
    if (!why.isEmpty()) {
        log(tool->name, who, QStringLiteral("refused: arguments"));
        reply(refused(why));
        return;
    }
    if (tool->risk == QLatin1String("write")) {
        // Nothing is written until the person taps the card.
        const QDateTime now = QDateTime::currentDateTimeUtc();
        for (auto it = m_cards.begin(); it != m_cards.end();) {
            if (it->made.secsTo(now) > CardMinutes * 60)
                it = m_cards.erase(it);
            else
                ++it;
        }
        const QString token = newToken();
        m_cards.insert(token, {*tool, positional, who, now});
        QVariantMap args;
        for (qsizetype i = 0; i < tool->arguments.size() && i < positional.size(); ++i)
            args.insert(tool->arguments.at(i).name, positional.at(i));
        log(tool->name, who, QStringLiteral("card"));
        reply({{QStringLiteral("ok"), true},
               {QStringLiteral("card"), true},
               {QStringLiteral("title"), cardTitle(*tool, positional)},
               {QStringLiteral("what"), QStringLiteral("%1: %2").arg(tool->appName, tool->summary)},
               {QStringLiteral("tool"), tool->name},
               {QStringLiteral("app"), tool->app},
               {QStringLiteral("appName"), tool->appName},
               {QStringLiteral("args"), args},
               {QStringLiteral("token"), token}});
        return;
    }
    invoke(*tool, positional, who, QStringLiteral("call"), reply);
}

void Toolbox::run(const QString &token, const Reply &reply)
{
    Q_EMIT used();
    const auto it = m_cards.constFind(token);
    if (token.isEmpty() || it == m_cards.cend()) {
        log(QStringLiteral("-"), QStringLiteral("-"), QStringLiteral("run refused: no such card"));
        reply(refused(QStringLiteral("That card has gone. Ask again.")));
        return;
    }
    const Card card = *it;
    // Each card runs once.
    m_cards.erase(it);
    if (card.made.secsTo(QDateTime::currentDateTimeUtc()) > CardMinutes * 60) {
        log(card.tool.name, card.caller, QStringLiteral("run refused: card too old"));
        reply(refused(QStringLiteral("That card waited too long. Ask again.")));
        return;
    }
    // The app may have gone since the card was shown.
    m_capabilities.read();
    if (!m_capabilities.tool(card.tool.name)) {
        log(card.tool.name, card.caller, QStringLiteral("run refused: no such tool"));
        reply(refused(QStringLiteral("%1 isn't on this computer any more.").arg(card.tool.appName)));
        return;
    }
    invoke(card.tool, card.positional, card.caller, QStringLiteral("run"), reply);
}

void Toolbox::invoke(const tools::Tool &tool, const QVariantList &positional, const QString &caller,
                     const QString &how, const Reply &reply)
{
    QDBusMessage message = QDBusMessage::createMethodCall(tool.service, tool.path, tool.interface, tool.method);
    message.setArguments(positional);
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message, 10000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [tool, caller, how, reply](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();
        const QDBusMessage answer = watcher->reply();
        if (answer.type() != QDBusMessage::ReplyMessage) {
            const bool old = answer.errorName() == QLatin1String("org.freedesktop.DBus.Error.UnknownMethod");
            log(tool.name, caller, how + QStringLiteral(" failed: ") + answer.errorName());
            reply(refused(old ? QStringLiteral("This version of %1 can't do that yet.").arg(tool.appName)
                              : QStringLiteral("%1 didn't answer.").arg(tool.appName)));
            return;
        }
        const QVariantList values = answer.arguments();
        QVariant result = values.size() == 1 ? BrokerService::plain(values.first()) : BrokerService::plain(values);
        // An app's own problem, in its own words, is a failure too.
        const QString problem = result.toMap().value(QStringLiteral("problem")).toString();
        log(tool.name, caller, how + (problem.isEmpty() ? QStringLiteral(" ok") : QStringLiteral(" failed: the app's problem")));
        if (!problem.isEmpty()) {
            reply(refused(problem));
            return;
        }
        reply({{QStringLiteral("ok"), true},
               {QStringLiteral("tool"), tool.name},
               {QStringLiteral("app"), tool.app},
               {QStringLiteral("appName"), tool.appName},
               {QStringLiteral("risk"), tool.risk},
               {QStringLiteral("result"), result}});
    });
}

void Toolbox::log(const QString &tool, const QString &caller, const QString &outcome)
{
    // Never what was passed or read: a note's words stay out of the log.
    qInfo().noquote() << QStringLiteral("split-rock-broker: tool %1 by %2: %3").arg(tool, caller.isEmpty() ? QStringLiteral("-") : caller, outcome);
}

} // namespace splitrock::broker
