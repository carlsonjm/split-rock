// SPDX-License-Identifier: GPL-2.0-or-later
//
// Core's Workspace Memory topic (docs/CORE.md § Workspace Memory): the
// session Cards offers after sign-in, the island that asks, and Always.
#include "core/Core.h"

#include <QSettings>

namespace splitrock::core {

namespace {

const QString Memory = QStringLiteral("memory");
const QString MemoryTitle = QStringLiteral("Workspace Memory");

QString appsCount(int count)
{
    return count == 1 ? QStringLiteral("1 app") : QStringLiteral("%1 apps").arg(count);
}

} // namespace

bool Core::watchesMemory() const
{
    return enabled(Memory);
}

bool Core::bringsBackAlways() const
{
    return QSettings(m_settings, QSettings::IniFormat).value(QStringLiteral("WorkspaceMemory/Always"), false).toBool();
}

void Core::setSetting(const QString &key, const QVariant &value)
{
    QSettings settings(m_settings, QSettings::IniFormat);
    settings.setValue(key, value);
    settings.sync();
}

void Core::seeSavedSession(const SavedSession &session)
{
    if (!watchesMemory() || !session.available || session.count <= 0)
        return;
    // Each saved session is offered once, whatever Core did since.
    if (session.when.isValid() && session.when == m_sessionSeen)
        return;
    const QDateTime now = m_hooks.now();
    m_sessionSeen = session.when;
    const bool islandBefore = m_memoryAttention.islandOpen;
    if (session.when.isValid() && session.when.daysTo(now) > MemoryKeepDays) {
        write(QStringLiteral("%1  The apps Cards remembers are from %2 days ago; not offered.")
                  .arg(MemoryTitle)
                  .arg(session.when.daysTo(now)));
        m_sessionPending = false;
        saveAttention(Memory);
        return;
    }
    m_session = session;

    if (bringsBackAlways()) {
        write(QStringLiteral("%1  Bringing back %2 %3, as you chose Always.")
                  .arg(MemoryTitle, appsCount(session.count), fromWhen(session.when, now)));
        m_sessionPending = false;
        m_memoryAttention.islandOpen = false;
        m_memoryAlwaysRun = true;
        saveAttention(Memory);
        if (m_hooks.bringBack)
            m_hooks.bringBack();
        if (islandBefore)
            Q_EMIT factChanged(memoryFact());
        return;
    }

    // Each session is news once; the island's daily allowance, as on a
    // second sign-in the same day, and Do Not Disturb leave it to the ticker.
    ++m_sessionOffers;
    m_sessionPending = true;
    Measure measure;
    measure.known = true;
    measure.level = Level::Island;
    measure.score = m_sessionOffers;
    measure.step = 1;
    const bool dnd = m_hooks.doNotDisturb && m_hooks.doNotDisturb();
    const Spoken spoken = decide(measure, m_memoryAttention, now, dnd);
    const QVariantMap fact = memoryFact();
    const QString line = fact.value(QStringLiteral("line")).toString();
    const QString detail = fact.value(QStringLiteral("detail")).toString();
    if (spoken.island)
        write(QStringLiteral("%1  Showed the island: %2 %3.").arg(MemoryTitle, line, detail));
    if (spoken.ticker) {
        write(QStringLiteral("%1  Told the ticker: %2 %3.").arg(MemoryTitle, line, detail));
        if (m_hooks.notify)
            m_hooks.notify(Memory, line, detail);
    }
    saveAttention(Memory);
    if (spoken.island || islandBefore != m_memoryAttention.islandOpen)
        Q_EMIT factChanged(memoryFact());
}

QVariantMap Core::memoryFact() const
{
    QStringList names;
    for (const QString &id : m_session.apps)
        names.append(m_hooks.appName ? m_hooks.appName(id) : id);
    return {
        {QStringLiteral("topic"), Memory},
        {QStringLiteral("title"), MemoryTitle},
        {QStringLiteral("line"), QStringLiteral("Bring back %1 %2?").arg(appsCount(m_session.count), fromWhen(m_session.when, m_hooks.now()))},
        {QStringLiteral("detail"), QStringLiteral("Each one goes back to its place")},
        {QStringLiteral("apps"), names},
        {QStringLiteral("count"), m_session.count},
        {QStringLiteral("level"), levelName(Level::Island)},
        {QStringLiteral("attention"), m_memoryAttention.islandOpen && m_sessionPending ? QStringLiteral("island")
                                                                                       : QStringLiteral("none")},
        {QStringLiteral("icon"), QStringLiteral("window-restore")},
        {QStringLiteral("actionText"), QStringLiteral("Bring back")},
        // A third choice beside the tap and Not now; a surface that doesn't
        // know it shows the other two.
        {QStringLiteral("choices"), QVariant::fromValue(QList<QVariantMap>{
                                        {{QStringLiteral("action"), QStringLiteral("always")},
                                         {QStringLiteral("text"), QStringLiteral("Always")}}})},
        {QStringLiteral("keywords"), QStringList{QStringLiteral("bring back"), QStringLiteral("restore apps"),
                                                 QStringLiteral("last session")}},
    };
}

bool Core::actMemory(const QString &action)
{
    if (!watchesMemory() || !m_sessionPending)
        return false;
    const bool islandBefore = m_memoryAttention.islandOpen;
    if (action == QLatin1String("always")) {
        setSetting(QStringLiteral("WorkspaceMemory/Always"), true);
        m_memoryAttention.islandOpen = false;
        write(QStringLiteral("%1  You chose Always: from now on your apps come back without asking.").arg(MemoryTitle));
    } else if (!answer(m_memoryAttention, action, m_sessionOffers, 1)) {
        return false;
    } else if (action == QLatin1String("open")) {
        write(QStringLiteral("%1  You chose Bring back.").arg(MemoryTitle));
    } else {
        write(QStringLiteral("%1  You %2; the desktop stays as it is.")
                  .arg(MemoryTitle,
                       action == QLatin1String("not-now") ? QStringLiteral("said not now") : QStringLiteral("dismissed it")));
    }
    m_sessionPending = false;
    saveAttention(Memory);
    if (action == QLatin1String("open") || action == QLatin1String("always")) {
        if (m_hooks.bringBack)
            m_hooks.bringBack();
    } else if (m_hooks.dismissSavedSession) {
        m_hooks.dismissSavedSession();
    }
    if (islandBefore != m_memoryAttention.islandOpen)
        Q_EMIT factChanged(memoryFact());
    return true;
}

void Core::broughtBack(const BringBackRun &run)
{
    const bool always = m_memoryAlwaysRun;
    m_memoryAlwaysRun = false;
    if (!run.accepted) {
        write(QStringLiteral("%1  Cards had nothing to bring back%2.")
                  .arg(MemoryTitle, run.reason == QLatin1String("busy") ? QStringLiteral(": it was already bringing some back")
                                                                        : QString()));
        return;
    }
    const int launched = int(run.launched.size());
    QString said = QStringLiteral("%1  Brought back %2").arg(MemoryTitle, appsCount(launched));
    if (!run.alreadyOpen.isEmpty())
        said += QStringLiteral(", with %1 already open").arg(run.alreadyOpen.size());
    if (run.finished)
        said += QStringLiteral("; %1 in place, %2 missed.").arg(run.placed).arg(run.missed);
    else
        said += QStringLiteral("; Cards didn't say they all arrived.");
    write(said);
    if (!always || launched == 0 || !watchesMemory())
        return;
    // Always: nobody asked this time, so the ticker says it happened.
    const QString detail = !run.finished || run.missed == 0
        ? QStringLiteral("As you left them")
        : run.missed == 1 ? QStringLiteral("1 didn't open in time") : QStringLiteral("%1 didn't open in time").arg(run.missed);
    const int back = launched - (run.finished ? run.missed : 0);
    if (!m_hooks.notify)
        return;
    if (back <= 0)
        m_hooks.notify(Memory, QStringLiteral("Your apps didn't come back"), QStringLiteral("Open them from the app menu"));
    else
        m_hooks.notify(Memory, QStringLiteral("Brought back %1").arg(appsCount(back)), detail);
}

} // namespace splitrock::core
