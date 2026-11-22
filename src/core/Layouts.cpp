// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/Layouts.h"

#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QUuid>

#include <algorithm>
#include <chrono>

namespace splitrock::core {

namespace {

const QString Path = QStringLiteral("/Kadunce");
const QString Interface = QStringLiteral("co.goodinput.Kadunce");
const QString GroupPrefix = QStringLiteral("Layout ");

// A{sv} as it comes off the bus inside a variant list.
QVariantMap mapOf(const QVariant &value)
{
    const QVariant inner = value.canConvert<QDBusVariant>() ? value.value<QDBusVariant>().variant() : value;
    if (inner.canConvert<QDBusArgument>()) {
        QVariantMap map;
        inner.value<QDBusArgument>() >> map;
        return map;
    }
    return inner.toMap();
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

QVariantList listOf(const QVariant &value)
{
    if (value.metaType() == QMetaType::fromType<QList<QVariantMap>>()) {
        QVariantList list;
        for (const QVariantMap &map : value.value<QList<QVariantMap>>())
            list.append(map);
        return list;
    }
    if (value.canConvert<QDBusArgument>()) {
        const QDBusArgument argument = value.value<QDBusArgument>();
        QVariantList list;
        argument.beginArray();
        while (!argument.atEnd()) {
            QVariantMap map;
            argument >> map;
            list.append(map);
        }
        argument.endArray();
        return list;
    }
    return value.toList();
}

// The number of a "[Layout N]" group, or 0 when it is not one.
int layoutNumber(const QString &group)
{
    if (!group.startsWith(GroupPrefix))
        return 0;
    bool number = false;
    const int n = group.mid(GroupPrefix.size()).toInt(&number);
    return number && n > 0 ? n : 0;
}

QString groupOf(const QString &id)
{
    bool number = false;
    const int n = id.toInt(&number);
    return number && n > 0 ? GroupPrefix + QString::number(n) : QString();
}

} // namespace

QVariantMap placeOf(const QVariantMap &group)
{
    static const QStringList numbers = {
        QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("width"), QStringLiteral("height"),
        QStringLiteral("pictureX"), QStringLiteral("pictureY"), QStringLiteral("pictureWidth"), QStringLiteral("pictureHeight"),
        QStringLiteral("displayX"), QStringLiteral("displayY"), QStringLiteral("displayWidth"), QStringLiteral("displayHeight"),
    };
    QVariantMap place;
    const QString display = group.value(QStringLiteral("display")).toString().trimmed();
    if (!display.isEmpty())
        place.insert(QStringLiteral("display"), display);
    if (group.value(QStringLiteral("pane")).toBool())
        place.insert(QStringLiteral("pane"), true);
    if (group.value(QStringLiteral("shown")).toBool())
        place.insert(QStringLiteral("shown"), true);
    for (const QString &key : numbers) {
        const QVariant value = group.value(key);
        bool ok = false;
        const double number = value.toDouble(&ok);
        if (value.isValid() && ok)
            place.insert(key, number);
    }
    return place;
}

QList<LayoutGroup> placedAs(const QList<LayoutGroup> &groups, const QList<LayoutGroup> &desk)
{
    QList<LayoutGroup> placed = groups;
    for (LayoutGroup &group : placed) {
        if (!group.place.isEmpty())
            continue;
        for (const LayoutGroup &read : desk) {
            if (read.apps == group.apps && !read.place.isEmpty()) {
                group.place = read.place;
                break;
            }
        }
    }
    return placed;
}

QList<LayoutGroup> tidyGroups(const QList<LayoutGroup> &groups)
{
    QList<LayoutGroup> tidy;
    QStringList seen;
    for (const LayoutGroup &group : groups) {
        LayoutGroup kept;
        for (QString id : group.apps) {
            id = id.trimmed();
            if (id.endsWith(QLatin1String(".desktop")))
                id.chop(8);
            if (id.isEmpty() || id.contains(QLatin1Char('/')) || seen.contains(id))
                continue;
            seen.append(id);
            kept.apps.append(id);
        }
        kept.stack = group.stack && kept.apps.size() >= 2;
        kept.place = group.place;
        if (!kept.apps.isEmpty())
            tidy.append(kept);
    }
    return tidy;
}

QList<LayoutGroup> groupsFromJson(const QString &json)
{
    QJsonDocument document = QJsonDocument::fromJson(json.toUtf8());
    // An early Split Rock wrote each quote as a backslash. No app id holds
    // either, so such a list reads back as it was meant.
    if (!document.isArray() && !json.contains(QLatin1Char('"')) && json.contains(QLatin1Char('\\')))
        document = QJsonDocument::fromJson(QString(json).replace(QLatin1Char('\\'), QLatin1Char('"')).toUtf8());
    QList<LayoutGroup> groups;
    for (const QJsonValue &each : document.array()) {
        const QJsonObject object = each.toObject();
        LayoutGroup group;
        for (const QJsonValue &app : object.value(QStringLiteral("apps")).toArray())
            group.apps.append(app.toString());
        group.stack = object.value(QStringLiteral("stack")).toBool();
        group.place = placeOf(object.toVariantMap());
        groups.append(group);
    }
    return tidyGroups(groups);
}

QString groupsToJson(const QList<LayoutGroup> &groups)
{
    QJsonArray array;
    for (const LayoutGroup &group : groups) {
        QJsonObject object = QJsonObject::fromVariantMap(group.place);
        object.insert(QStringLiteral("apps"), QJsonArray::fromStringList(group.apps));
        object.insert(QStringLiteral("stack"), group.stack);
        array.append(object);
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

QList<LayoutGroup> groupsFrom(const QVariantList &answer)
{
    QList<LayoutGroup> groups;
    for (const QVariant &each : answer) {
        const QVariantMap map = mapOf(each);
        groups.append({stringsOf(map.value(QStringLiteral("apps"))), map.value(QStringLiteral("stack")).toBool(), placeOf(map)});
    }
    return tidyGroups(groups);
}

QList<QVariantMap> groupsToBus(const QList<LayoutGroup> &groups)
{
    QList<QVariantMap> list;
    for (const LayoutGroup &group : groups) {
        QVariantMap map = group.place;
        map.insert(QStringLiteral("apps"), group.apps);
        map.insert(QStringLiteral("stack"), group.stack);
        list.append(map);
    }
    return list;
}

QList<Layout> readLayouts(const QString &settingsFile)
{
    QSettings settings(settingsFile, QSettings::IniFormat);
    QList<std::pair<int, Layout>> found;
    for (const QString &group : settings.childGroups()) {
        const int n = layoutNumber(group);
        if (n == 0)
            continue;
        settings.beginGroup(group);
        Layout layout;
        layout.id = QString::number(n);
        layout.name = settings.value(QStringLiteral("Name")).toString().trimmed();
        layout.groups = groupsFromJson(settings.value(QStringLiteral("Groups")).toString());
        layout.atSignIn = settings.value(QStringLiteral("AtSignIn"), false).toBool();
        layout.added = QDateTime::fromString(settings.value(QStringLiteral("Added")).toString(), Qt::ISODate);
        layout.opened = QDateTime::fromString(settings.value(QStringLiteral("Opened")).toString(), Qt::ISODate);
        settings.endGroup();
        if (!layout.name.isEmpty() && !layout.groups.isEmpty())
            found.append({n, layout});
    }
    std::sort(found.begin(), found.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    QList<Layout> layouts;
    for (const auto &[n, layout] : found)
        layouts.append(layout);
    return layouts;
}

Layout saveLayout(const QString &settingsFile, const QString &name, const QList<LayoutGroup> &groups, bool atSignIn,
                  const QDateTime &added)
{
    Layout layout{QString(), name.trimmed(), tidyGroups(groups), atSignIn, added, {}};
    if (layout.name.isEmpty() || layout.groups.isEmpty())
        return {};
    QSettings settings(settingsFile, QSettings::IniFormat);
    int next = 1;
    for (const QString &group : settings.childGroups()) {
        const int n = layoutNumber(group);
        if (n == 0)
            continue;
        next = std::max(next, n + 1);
        // The same name replaces that layout, in its own group.
        if (layout.id.isEmpty()
            && settings.value(group + QStringLiteral("/Name")).toString().trimmed().compare(layout.name, Qt::CaseInsensitive) == 0)
            layout.id = QString::number(n);
    }
    if (layout.id.isEmpty())
        layout.id = QString::number(next);
    const QString group = groupOf(layout.id);
    // A layout replaced keeps when it was last opened.
    const QString opened = settings.value(group + QStringLiteral("/Opened")).toString();
    layout.opened = QDateTime::fromString(opened, Qt::ISODate);
    settings.remove(group);
    settings.beginGroup(group);
    settings.setValue(QStringLiteral("Name"), layout.name);
    settings.setValue(QStringLiteral("Groups"), groupsToJson(layout.groups));
    settings.setValue(QStringLiteral("AtSignIn"), layout.atSignIn);
    settings.setValue(QStringLiteral("Added"), added.toUTC().toString(Qt::ISODate));
    if (!opened.isEmpty())
        settings.setValue(QStringLiteral("Opened"), opened);
    settings.endGroup();
    settings.sync();
    return layout;
}

bool removeLayout(const QString &settingsFile, const QString &id)
{
    const QString group = groupOf(id);
    QSettings settings(settingsFile, QSettings::IniFormat);
    if (group.isEmpty() || !settings.childGroups().contains(group))
        return false;
    settings.remove(group);
    settings.sync();
    return true;
}

bool setLayoutAtSignIn(const QString &settingsFile, const QString &id, bool on)
{
    const QString group = groupOf(id);
    QSettings settings(settingsFile, QSettings::IniFormat);
    if (group.isEmpty() || !settings.childGroups().contains(group))
        return false;
    settings.setValue(group + QStringLiteral("/AtSignIn"), on);
    settings.sync();
    return true;
}

bool setLayoutOpened(const QString &settingsFile, const QString &id, const QDateTime &when)
{
    const QString group = groupOf(id);
    QSettings settings(settingsFile, QSettings::IniFormat);
    if (group.isEmpty() || !settings.childGroups().contains(group))
        return false;
    settings.setValue(group + QStringLiteral("/Opened"), when.toUTC().toString(Qt::ISODate));
    settings.sync();
    return true;
}

CurrentDesk currentDeskFrom(const QVariantMap &answer)
{
    // A Cards that reads every display says so in `everywhere`, and gives each
    // display's size, which goes with the groups standing on it.
    const QVariantList everywhere = listOf(answer.value(QStringLiteral("everywhere")));
    QList<LayoutGroup> groups = groupsFrom(everywhere.isEmpty() ? listOf(answer.value(QStringLiteral("groups"))) : everywhere);
    for (const QVariant &each : listOf(answer.value(QStringLiteral("displays")))) {
        const QVariantMap display = mapOf(each);
        for (LayoutGroup &group : groups) {
            if (group.place.value(QStringLiteral("display")).toString() != display.value(QStringLiteral("name")).toString())
                continue;
            for (const char *key : {"x", "y", "width", "height"}) {
                QString kept = QString::fromLatin1(key);
                kept[0] = kept.at(0).toUpper();
                group.place.insert(QStringLiteral("display") + kept, display.value(QString::fromLatin1(key)).toDouble());
            }
        }
    }
    return {answer.value(QStringLiteral("workspace")).toString(), groups};
}

LayoutPlan layoutPlanFrom(const QVariantMap &answer)
{
    LayoutPlan plan;
    plan.accepted = answer.value(QStringLiteral("accepted")).toBool();
    plan.launch = stringsOf(answer.value(QStringLiteral("launch")));
    plan.launch.removeAll(QString());
    plan.launch.removeDuplicates();
    plan.alreadyOpen = stringsOf(answer.value(QStringLiteral("alreadyOpen")));
    plan.alreadyOpen.removeAll(QString());
    plan.reason = answer.value(QStringLiteral("reason")).toString();
    return plan;
}

KadunceLayouts::KadunceLayouts(QDBusConnection bus, const QString &service, QObject *parent)
    : QObject(parent)
    , m_bus(bus)
    , m_service(service)
{
    qDBusRegisterMetaType<QList<QVariantMap>>();
    m_bus.connect(m_service, Path, Interface, QStringLiteral("layoutAppPlaced"), this,
                  SLOT(layoutAppPlaced(QString, QString, bool)));
    m_bus.connect(m_service, Path, Interface, QStringLiteral("layoutFinished"), this,
                  SLOT(layoutFinished(QString, int, int)));
    m_limit.setSingleShot(true);
    m_limit.setInterval(std::chrono::seconds(RunLimitSeconds));
    connect(&m_limit, &QTimer::timeout, this, [this] { finish(false); });
}

void KadunceLayouts::whenKnown(std::function<void(bool)> next)
{
    if (m_version == Version) {
        next(true);
        return;
    }
    QDBusMessage call = QDBusMessage::createMethodCall(m_service, Path, Interface, QStringLiteral("layoutProtocolVersion"));
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

void KadunceLayouts::currentLayout(std::function<void(std::optional<CurrentDesk>)> done)
{
    whenKnown([this, done](bool known) {
        if (!known) {
            done(std::nullopt);
            return;
        }
        auto *watcher = new QDBusPendingCallWatcher(
            m_bus.asyncCall(QDBusMessage::createMethodCall(m_service, Path, Interface, QStringLiteral("currentLayout")), 5000),
            this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [watcher, done] {
            watcher->deleteLater();
            const QDBusPendingReply<QVariantMap> reply = *watcher;
            if (reply.isError())
                done(std::nullopt);
            else
                done(currentDeskFrom(reply.value()));
        });
    });
}

void KadunceLayouts::arrange(const QList<LayoutGroup> &groups, std::function<void(const LayoutPlan &)> armed,
                             std::function<void(const LayoutRun &)> done)
{
    if (running()) {
        LayoutRun busy;
        busy.reason = QStringLiteral("busy");
        busy.finished = true;
        done(busy);
        return;
    }
    m_token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_run = LayoutRun();
    m_placed.clear();
    m_done = std::move(done);
    whenKnown([this, groups, armed](bool known) {
        if (!known) {
            m_run.reason = QStringLiteral("unavailable");
            finish(true);
            return;
        }
        QDBusMessage call = QDBusMessage::createMethodCall(m_service, Path, Interface, QStringLiteral("arrangeLayout"));
        call.setArguments({m_token, QVariant::fromValue(groupsToBus(groups))});
        auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call, 5000), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, armed] {
            watcher->deleteLater();
            const QDBusPendingReply<QVariantMap> reply = *watcher;
            const LayoutPlan plan = reply.isError() ? LayoutPlan{} : layoutPlanFrom(reply.value());
            m_run.accepted = plan.accepted;
            m_run.reason = plan.accepted || !plan.reason.isEmpty() ? plan.reason : QStringLiteral("unavailable");
            m_run.launch = plan.launch;
            m_run.alreadyOpen = plan.alreadyOpen;
            if (!plan.accepted) {
                finish(true);
                return;
            }
            // Cards waits for each launched app's first window; the run ends
            // when it says so.
            m_limit.start();
            if (armed)
                armed(plan);
        });
    });
}

void KadunceLayouts::layoutAppPlaced(const QString &token, const QString &appId, bool placed)
{
    if (token.isEmpty() || token != m_token)
        return;
    if (placed)
        m_placed.insert(appId);
    // Each one keeps the run alive while apps still arrive.
    if (m_limit.isActive())
        m_limit.start();
}

void KadunceLayouts::layoutFinished(const QString &token, int placed, int missed)
{
    if (token.isEmpty() || token != m_token)
        return;
    m_run.placed = placed;
    m_run.missed = missed;
    finish(true);
}

void KadunceLayouts::finish(bool finished)
{
    if (m_token.isEmpty())
        return;
    m_limit.stop();
    if (m_run.accepted) {
        // The apps Cards never said reached their place.
        for (const QString &id : m_run.launch + m_run.alreadyOpen) {
            if (!m_placed.contains(id) && !m_run.missedApps.contains(id))
                m_run.missedApps.append(id);
        }
        if (finished && m_run.missed == 0)
            m_run.missedApps.clear();
        if (!finished) {
            QDBusMessage call = QDBusMessage::createMethodCall(m_service, Path, Interface, QStringLiteral("cancelLayout"));
            call.setArguments({m_token});
            m_bus.asyncCall(call);
            m_run.placed = int(m_placed.size());
            m_run.missed = int(m_run.missedApps.size());
        }
    }
    m_run.finished = finished;
    m_token.clear();
    const auto done = std::move(m_done);
    m_done = nullptr;
    if (done)
        done(m_run);
}

} // namespace splitrock::core
