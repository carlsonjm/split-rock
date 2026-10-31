// SPDX-License-Identifier: GPL-2.0-or-later
//
// Core's saved layouts (docs/CORE.md § Layouts): keeping the named desks the
// person saves from Search, opening one through Cards, and opening those
// switched on at sign-in once the Workspace Memory question is settled.
#include "core/Core.h"

namespace splitrock::core {

namespace {

const QString LayoutsTitle = QStringLiteral("Layouts");

QString appsCount(int count)
{
    return count == 1 ? QStringLiteral("1 app") : QStringLiteral("%1 apps").arg(count);
}

} // namespace

QList<Layout> Core::layouts() const
{
    return readLayouts(m_settings);
}

QList<QVariantMap> Core::groupsWithNames(const QList<LayoutGroup> &groups) const
{
    QList<QVariantMap> list;
    for (const LayoutGroup &group : groups) {
        QStringList names;
        for (const QString &id : group.apps)
            names.append(m_hooks.appName ? m_hooks.appName(id) : id);
        list.append({{QStringLiteral("apps"), group.apps},
                     {QStringLiteral("names"), names},
                     {QStringLiteral("stack"), group.stack}});
    }
    return list;
}

QList<QVariantMap> Core::layoutsForBus() const
{
    QList<QVariantMap> list;
    for (const Layout &layout : layouts()) {
        list.append({{QStringLiteral("id"), layout.id},
                     {QStringLiteral("name"), layout.name},
                     {QStringLiteral("groups"), QVariant::fromValue(groupsWithNames(layout.groups))},
                     {QStringLiteral("atSignIn"), layout.atSignIn}});
    }
    return list;
}

void Core::currentLayout(std::function<void(const QVariantMap &)> answered)
{
    const auto answer = [this, answered](const std::optional<CurrentDesk> &desk) {
        answered({{QStringLiteral("available"), desk.has_value()},
                  {QStringLiteral("workspace"), desk ? desk->workspace : QString()},
                  {QStringLiteral("groups"), QVariant::fromValue(groupsWithNames(desk ? desk->groups : QList<LayoutGroup>()))}});
    };
    if (!m_hooks.currentLayout) {
        answer(std::nullopt);
        return;
    }
    m_hooks.currentLayout(answer);
}

QString Core::saveLayout(const QString &name, const QString &groupsJson, bool atSignIn)
{
    const Layout saved = core::saveLayout(m_settings, name, groupsFromJson(groupsJson), atSignIn, m_hooks.now());
    if (saved.id.isEmpty())
        return QString();
    int apps = 0;
    for (const LayoutGroup &group : saved.groups)
        apps += int(group.apps.size());
    write(QStringLiteral("%1  Saved %2: %3 in %4 %5%6.")
              .arg(LayoutsTitle, saved.name, appsCount(apps))
              .arg(saved.groups.size())
              .arg(saved.groups.size() == 1 ? QStringLiteral("group") : QStringLiteral("groups"),
                   saved.atSignIn ? QStringLiteral(", opening when you sign in") : QString()));
    Q_EMIT layoutsChanged();
    return saved.id;
}

bool Core::removeLayout(const QString &id)
{
    QString name;
    for (const Layout &layout : layouts()) {
        if (layout.id == id)
            name = layout.name;
    }
    if (!core::removeLayout(m_settings, id))
        return false;
    write(QStringLiteral("%1  Removed %2.").arg(LayoutsTitle, name.isEmpty() ? QStringLiteral("a layout") : name));
    Q_EMIT layoutsChanged();
    return true;
}

bool Core::setLayoutAtSignIn(const QString &id, bool on)
{
    if (!core::setLayoutAtSignIn(m_settings, id, on))
        return false;
    Q_EMIT layoutsChanged();
    return true;
}

bool Core::openLayout(const QString &id, std::function<void(bool)> answered)
{
    std::optional<Layout> found;
    for (const Layout &layout : layouts()) {
        if (layout.id == id)
            found = layout;
    }
    // Cards arranges one desk at a time, and never during a bring-back.
    if (!found || !m_layoutRunning.isEmpty() || m_bringingBack || !m_hooks.arrangeLayout)
        return false;
    const Layout layout = *found;
    m_layoutRunning = id;
    const auto armed = [this, answered](const LayoutPlan &plan) {
        // What automations opened this sign-in is already on its way.
        QStringList launch;
        for (const QString &app : plan.launch) {
            if (!m_openedAtSignIn.contains(app))
                launch.append(app);
        }
        if (!launch.isEmpty() && m_hooks.openApps)
            m_hooks.openApps(launch);
        if (answered)
            answered(true);
    };
    const auto done = [this, layout, answered](const LayoutRun &run) {
        m_layoutRunning.clear();
        if (!run.accepted) {
            write(QStringLiteral("%1  Cards didn't open %2%3.")
                      .arg(LayoutsTitle, layout.name,
                           run.reason == QLatin1String("busy") ? QStringLiteral(": it was arranging the desk already")
                           : run.reason == QLatin1String("empty") ? QStringLiteral(": none of its apps could be found")
                                                                  : QStringLiteral(": Cards isn't running")));
            if (answered)
                answered(false);
            nextLayoutAtSignIn();
            return;
        }
        QStringList missedNames;
        for (const QString &app : run.missedApps)
            missedNames.append(m_hooks.appName ? m_hooks.appName(app) : app);
        QString said = QStringLiteral("%1  Opened %2: launched %3").arg(LayoutsTitle, layout.name, appsCount(int(run.launch.size())));
        if (!run.alreadyOpen.isEmpty())
            said += QStringLiteral(", with %1 already open").arg(run.alreadyOpen.size());
        if (run.finished)
            said += QStringLiteral("; %1 in place, %2 missed").arg(run.placed).arg(run.missed);
        else
            said += QStringLiteral("; Cards didn't say they all arrived");
        if (!missedNames.isEmpty())
            said += QStringLiteral(" (%1)").arg(namesList(missedNames));
        write(said + QLatin1Char('.'));
        Q_EMIT layoutOpened(layout.id, run.placed, run.missed, missedNames);
        nextLayoutAtSignIn();
    };
    write(QStringLiteral("%1  Opening %2.").arg(LayoutsTitle, layout.name));
    m_hooks.arrangeLayout(layout.groups, armed, done);
    return true;
}

bool Core::openLayoutsAtSignIn()
{
    const QString signIn = m_hooks.signIn ? m_hooks.signIn() : QString();
    if (!signIn.isEmpty() && signIn == m_layoutsRanIn)
        return false;
    m_layoutsRanIn = signIn;
    saveAttention(QStringLiteral("layouts"));
    m_layoutsWaiting.clear();
    for (const Layout &layout : layouts()) {
        if (layout.atSignIn)
            m_layoutsWaiting.append(layout.id);
    }
    nextLayoutAtSignIn();
    return true;
}

void Core::nextLayoutAtSignIn()
{
    // One at a time, and never beside the Workspace Memory question or its
    // bring-back.
    if (m_layoutsWaiting.isEmpty() || !m_layoutRunning.isEmpty() || !memorySettled())
        return;
    const QString id = m_layoutsWaiting.takeFirst();
    if (!openLayout(id))
        nextLayoutAtSignIn();
}

} // namespace splitrock::core
