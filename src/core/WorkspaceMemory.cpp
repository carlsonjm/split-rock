// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/WorkspaceMemory.h"

#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusArgument>
#include <QFile>
#include <QLocale>
#include <QProcess>
#include <QStandardPaths>
#include <QTimeZone>
#include <QUuid>

#include <chrono>

namespace splitrock::core {

namespace {

const QString Path = QStringLiteral("/Kadunce");
const QString Interface = QStringLiteral("co.goodinput.Kadunce");

// A{sv} as it comes off the bus inside a variant list.
QVariantMap mapOf(const QVariant &value)
{
    if (value.canConvert<QDBusArgument>()) {
        QVariantMap map;
        value.value<QDBusArgument>() >> map;
        return map;
    }
    return value.toMap();
}

QStringList stringsOf(const QVariant &value)
{
    if (value.canConvert<QDBusArgument>()) {
        QStringList list;
        value.value<QDBusArgument>() >> list;
        return list;
    }
    return value.toStringList();
}

QDateTime unixTime(const QVariant &value)
{
    const qint64 seconds = value.toLongLong();
    return seconds > 0 ? QDateTime::fromSecsSinceEpoch(seconds, QTimeZone::utc()) : QDateTime();
}

} // namespace

SavedSession savedSessionFrom(const QVariantMap &answer)
{
    SavedSession session;
    session.apps = stringsOf(answer.value(QStringLiteral("apps")));
    session.apps.removeAll(QString());
    session.count = answer.value(QStringLiteral("count"), session.apps.size()).toInt();
    session.available = answer.value(QStringLiteral("available")).toBool() && session.count > 0;
    session.when = unixTime(answer.value(QStringLiteral("when")));
    session.clean = answer.value(QStringLiteral("clean"), true).toBool();
    session.windows = answer.value(QStringLiteral("windows"), session.count).toInt();
    return session;
}

BringBackPlan bringBackPlanFrom(const QVariantMap &answer)
{
    BringBackPlan plan;
    plan.accepted = answer.value(QStringLiteral("accepted")).toBool();
    plan.launch = stringsOf(answer.value(QStringLiteral("launch")));
    plan.launch.removeAll(QString());
    plan.launch.removeDuplicates();
    plan.alreadyOpen = stringsOf(answer.value(QStringLiteral("alreadyOpen")));
    plan.reason = answer.value(QStringLiteral("reason")).toString();
    return plan;
}

QList<OpenSet> openSetsFrom(const QVariantList &answer)
{
    QList<OpenSet> sets;
    for (const QVariant &each : answer) {
        const QVariantMap map = mapOf(each.canConvert<QDBusVariant>() ? each.value<QDBusVariant>().variant() : each);
        OpenSet set;
        set.apps = stringsOf(map.value(QStringLiteral("apps")));
        set.apps.removeAll(QString());
        set.apps.removeDuplicates();
        set.apps.sort();
        set.count = map.value(QStringLiteral("count")).toInt();
        set.lastSeen = unixTime(map.value(QStringLiteral("lastSeen")));
        if (set.apps.size() >= 2 && set.count > 0)
            sets.append(set);
    }
    return sets;
}

QString fromWhen(const QDateTime &when, const QDateTime &now)
{
    const QDate day = when.toLocalTime().date();
    const qint64 days = day.daysTo(now.toLocalTime().date());
    if (days <= 0)
        return QStringLiteral("from earlier today");
    if (days == 1)
        return QStringLiteral("from yesterday");
    const QLocale english(QLocale::English);
    if (days < 7)
        return QStringLiteral("from %1").arg(english.dayName(day.dayOfWeek()));
    return QStringLiteral("from %1 %2").arg(day.day()).arg(english.monthName(day.month()));
}

QString namesList(const QStringList &names)
{
    if (names.isEmpty())
        return QString();
    if (names.size() == 1)
        return names.first();
    if (names.size() <= 3)
        return QStringList(names.mid(0, names.size() - 1)).join(QStringLiteral(", ")) + QStringLiteral(" and ")
            + names.last();
    return QStringList(names.mid(0, 3)).join(QStringLiteral(", ")) + QStringLiteral(" and %1 more").arg(names.size() - 3);
}

QString appName(const QString &desktopId)
{
    const QString path = QStandardPaths::locate(QStandardPaths::ApplicationsLocation, desktopId + QStringLiteral(".desktop"));
    QFile file(path);
    if (!path.isEmpty() && file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        bool entry = false;
        while (!file.atEnd()) {
            const QString line = QString::fromUtf8(file.readLine()).trimmed();
            if (line.startsWith(QLatin1Char('[')))
                entry = line == QLatin1String("[Desktop Entry]");
            else if (entry && line.startsWith(QLatin1String("Name=")))
                return line.mid(5).trimmed();
        }
    }
    QString name = desktopId.section(QLatin1Char('.'), -1);
    name.replace(QLatin1Char('-'), QLatin1Char(' ')).replace(QLatin1Char('_'), QLatin1Char(' '));
    return name.isEmpty() ? desktopId : name.left(1).toUpper() + name.mid(1);
}

KadunceMemory::KadunceMemory(QDBusConnection bus, const QString &service, QObject *parent)
    : QObject(parent)
    , m_bus(bus)
    , m_service(service)
{
    m_bus.connect(m_service, Path, Interface, QStringLiteral("appBroughtBack"), this,
                  SLOT(appBroughtBack(QString, QString, QString, bool)));
    m_bus.connect(m_service, Path, Interface, QStringLiteral("bringBackFinished"), this,
                  SLOT(bringBackFinished(QString, int, int)));
    m_limit.setSingleShot(true);
    m_limit.setInterval(std::chrono::seconds(RunLimitSeconds));
    connect(&m_limit, &QTimer::timeout, this, [this] { finish(false); });
}

void KadunceMemory::whenKnown(std::function<void(bool)> next)
{
    if (m_version == Version) {
        next(true);
        return;
    }
    QDBusMessage call = QDBusMessage::createMethodCall(m_service, Path, Interface,
                                                       QStringLiteral("workspaceMemoryProtocolVersion"));
    // Asked only of a Kadunce already running.
    call.setAutoStartService(false);
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call, 5000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, next] {
        watcher->deleteLater();
        const QDBusPendingReply<int> reply = *watcher;
        if (!reply.isError())
            m_version = reply.value();
        next(m_version == Version);
    });
}

void KadunceMemory::savedSession(std::function<void(std::optional<SavedSession>)> done)
{
    whenKnown([this, done](bool known) {
        if (!known) {
            done(std::nullopt);
            return;
        }
        auto *watcher = new QDBusPendingCallWatcher(
            m_bus.asyncCall(QDBusMessage::createMethodCall(m_service, Path, Interface, QStringLiteral("savedSession")), 5000),
            this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [watcher, done] {
            watcher->deleteLater();
            const QDBusPendingReply<QVariantMap> reply = *watcher;
            if (reply.isError())
                done(std::nullopt);
            else
                done(savedSessionFrom(reply.value()));
        });
    });
}

void KadunceMemory::openTogether(std::function<void(std::optional<QList<OpenSet>>)> done)
{
    whenKnown([this, done](bool known) {
        if (!known) {
            done(std::nullopt);
            return;
        }
        auto *watcher = new QDBusPendingCallWatcher(
            m_bus.asyncCall(QDBusMessage::createMethodCall(m_service, Path, Interface, QStringLiteral("openTogether")), 5000),
            this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [watcher, done] {
            watcher->deleteLater();
            const QDBusPendingReply<QVariantList> reply = *watcher;
            if (reply.isError())
                done(std::nullopt);
            else
                done(openSetsFrom(reply.value()));
        });
    });
}

void KadunceMemory::dismissSavedSession()
{
    whenKnown([this](bool known) {
        if (known)
            m_bus.asyncCall(QDBusMessage::createMethodCall(m_service, Path, Interface, QStringLiteral("dismissSavedSession")));
    });
}

void KadunceMemory::bringBack(std::function<void(const QString &)> launch, std::function<void(const BringBackRun &)> done)
{
    if (running())
        return;
    m_token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_run = BringBackRun();
    m_done = std::move(done);
    whenKnown([this, launch](bool known) {
        if (!known) {
            m_run.reason = QStringLiteral("nothing");
            finish(true);
            return;
        }
        QDBusMessage call = QDBusMessage::createMethodCall(m_service, Path, Interface, QStringLiteral("bringBack"));
        call.setArguments({m_token});
        auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call, 5000), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, launch] {
            watcher->deleteLater();
            const QDBusPendingReply<QVariantMap> reply = *watcher;
            const BringBackPlan plan = reply.isError() ? BringBackPlan{} : bringBackPlanFrom(reply.value());
            m_run.accepted = plan.accepted;
            m_run.reason = plan.reason;
            m_run.alreadyOpen = plan.alreadyOpen;
            if (!plan.accepted) {
                finish(true);
                return;
            }
            // Cards waits for each app's first window; the run ends when
            // it says so.
            m_limit.start();
            for (const QString &id : plan.launch) {
                m_run.launched.append(id);
                launch(id);
            }
            if (plan.launch.isEmpty())
                finish(true);
        });
    });
}

void KadunceMemory::appBroughtBack(const QString &token, const QString &, const QString &, bool)
{
    // Each one keeps the run alive while apps still arrive.
    if (token == m_token && m_limit.isActive())
        m_limit.start();
}

void KadunceMemory::bringBackFinished(const QString &token, int placed, int missed)
{
    if (token.isEmpty() || token != m_token)
        return;
    m_run.placed = placed;
    m_run.missed = missed;
    finish(true);
}

void KadunceMemory::finish(bool finished)
{
    if (m_token.isEmpty())
        return;
    m_limit.stop();
    if (!finished) {
        QDBusMessage call = QDBusMessage::createMethodCall(m_service, Path, Interface, QStringLiteral("cancelBringBack"));
        call.setArguments({m_token});
        m_bus.asyncCall(call);
    }
    m_run.finished = finished;
    m_token.clear();
    const auto done = std::move(m_done);
    m_done = nullptr;
    if (done)
        done(m_run);
}

bool launchApp(const QString &desktopId)
{
    if (desktopId.isEmpty() || desktopId.contains(QLatin1Char('/')))
        return false;
    const QString kstart = QStandardPaths::findExecutable(QStringLiteral("kstart"));
    if (!kstart.isEmpty())
        return QProcess::startDetached(kstart, {QStringLiteral("--application"), desktopId});
    const QString gtk = QStandardPaths::findExecutable(QStringLiteral("gtk-launch"));
    return !gtk.isEmpty() && QProcess::startDetached(gtk, {desktopId});
}

} // namespace splitrock::core
