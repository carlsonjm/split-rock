// SPDX-License-Identifier: GPL-2.0-or-later
//
// Core's Workspace Memory and Automations topics (docs/CORE.md § Workspace
// Memory, § Automations): the session Cards offers after sign-in, the island
// that asks, and Always; and the apps opened together, the offer to open them
// at sign-in, and running the ones the person accepted.
#include "core/Core.h"

#include <QSettings>

namespace splitrock::core {

namespace {

const QString Memory = QStringLiteral("memory");
const QString MemoryTitle = QStringLiteral("Workspace Memory");
const QString Automations = QStringLiteral("automations");
const QString AutomationsTitle = QStringLiteral("Automations");

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
    if (watchesMemory() && session.allOpen) {
        write(QStringLiteral("%1  Every app Cards remembers is open already; nothing to bring back.").arg(MemoryTitle));
        if (m_hooks.dismissSavedSession)
            m_hooks.dismissSavedSession();
        return;
    }
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
        startBringBack();
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
        startBringBack();
    } else if (m_hooks.dismissSavedSession) {
        m_hooks.dismissSavedSession();
    }
    if (islandBefore != m_memoryAttention.islandOpen)
        Q_EMIT factChanged(memoryFact());
    // Not now settles the question, so a layout waiting for it opens.
    nextLayoutAtSignIn();
    return true;
}

void Core::startBringBack()
{
    if (!m_hooks.bringBack)
        return;
    m_bringingBack = true;
    m_hooks.bringBack();
}

bool Core::memorySettled() const
{
    if (m_bringingBack)
        return false;
    // A question still standing on the island; one nobody touched for a day
    // has closed.
    const bool asking = watchesMemory() && m_sessionPending && m_memoryAttention.islandOpen
        && (!m_memoryAttention.islandSince.isValid() || m_memoryAttention.islandSince.secsTo(m_hooks.now()) < 24 * 3600);
    return !asking;
}

void Core::broughtBack(const BringBackRun &run)
{
    m_bringingBack = false;
    tellBroughtBack(run);
    // Whatever came of it, the layouts waiting for it may open now.
    nextLayoutAtSignIn();
}

void Core::tellBroughtBack(const BringBackRun &run)
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

bool Core::watchesAutomations() const
{
    return enabled(Automations);
}

QList<Automation> Core::automations() const
{
    return readAutomations(m_settings);
}

void Core::seeOpenTogether(const QList<OpenSet> &sets)
{
    // One offer at a time, and only while the topic is on.
    if (!watchesAutomations() || m_offerPending)
        return;
    const std::optional<OpenSet> set = setToOffer(sets, m_offered, automations());
    if (!set)
        return;
    const QDateTime now = m_hooks.now();
    // An offer can wait: during Do Not Disturb, or with today's island
    // already shown, it waits for the next look rather than go to the
    // ticker, where a tap would be a yes nobody read.
    if (m_hooks.doNotDisturb && m_hooks.doNotDisturb())
        return;
    if (m_automationsAttention.islandsDay == now.date() && m_automationsAttention.islandsToday >= 1)
        return;
    m_offer = *set;
    m_offerPending = true;
    m_offered.append(setKey(set->apps));
    Measure measure;
    measure.known = true;
    measure.level = Level::Island;
    measure.score = int(m_offered.size());
    measure.step = 1;
    const Spoken spoken = decide(measure, m_automationsAttention, now, false);
    const QVariantMap fact = automationsFact();
    if (spoken.island)
        write(QStringLiteral("%1  Offered: %2 %3.")
                  .arg(AutomationsTitle, fact.value(QStringLiteral("line")).toString(), fact.value(QStringLiteral("detail")).toString()));
    else
        m_offerPending = false;
    saveAttention(Automations);
    if (spoken.island)
        Q_EMIT factChanged(automationsFact());
}

QVariantMap Core::automationsFact() const
{
    QStringList names;
    for (const QString &id : m_offer.apps)
        names.append(m_hooks.appName ? m_hooks.appName(id) : id);
    return {
        {QStringLiteral("topic"), Automations},
        {QStringLiteral("title"), AutomationsTitle},
        {QStringLiteral("line"), automationWords::offer(namesList(names))},
        {QStringLiteral("detail"), automationWords::because(m_offer.count)},
        {QStringLiteral("apps"), names},
        {QStringLiteral("count"), int(m_offer.apps.size())},
        {QStringLiteral("level"), levelName(Level::Island)},
        {QStringLiteral("attention"), m_automationsAttention.islandOpen && m_offerPending ? QStringLiteral("island")
                                                                                          : QStringLiteral("none")},
        {QStringLiteral("icon"), QStringLiteral("system-run")},
        {QStringLiteral("actionText"), QString::fromUtf8(automationWords::Yes)},
        {QStringLiteral("dismissText"), QString::fromUtf8(automationWords::NotNow)},
        {QStringLiteral("keywords"), QStringList{QStringLiteral("automation"), QStringLiteral("open at sign in"),
                                                 QStringLiteral("startup apps")}},
    };
}

bool Core::actAutomations(const QString &action)
{
    if (!watchesAutomations() || !m_offerPending)
        return false;
    if (!answer(m_automationsAttention, action, int(m_offered.size()), 1))
        return false;
    QStringList names;
    for (const QString &id : m_offer.apps)
        names.append(m_hooks.appName ? m_hooks.appName(id) : id);
    if (action == QLatin1String("open")) {
        const Automation added = addAutomation(m_settings, m_offer.apps, names, m_hooks.now());
        write(QStringLiteral("%1  You said yes: %2 open when you sign in (%3 in the settings file).")
                  .arg(AutomationsTitle, namesList(names), added.group));
    } else {
        write(QStringLiteral("%1  You %2; Core won't offer %3 again.")
                  .arg(AutomationsTitle,
                       action == QLatin1String("not-now") ? QStringLiteral("said not now") : QStringLiteral("dismissed it"),
                       namesList(names)));
    }
    m_offerPending = false;
    saveAttention(Automations);
    Q_EMIT factChanged(automationsFact());
    return true;
}

bool Core::runAutomations(const QStringList &skip)
{
    if (!watchesAutomations())
        return false;
    const QString signIn = m_hooks.signIn ? m_hooks.signIn() : QString();
    if (!signIn.isEmpty() && signIn == m_automationsRanIn)
        return false;
    QStringList open;
    QStringList said;
    for (const Automation &automation : automations()) {
        if (!automation.enabled)
            continue;
        for (const QString &id : automation.apps) {
            if (!skip.contains(id) && !open.contains(id))
                open.append(id);
        }
        QStringList names = automation.names;
        if (names.size() != automation.apps.size()) {
            names.clear();
            for (const QString &id : automation.apps)
                names.append(m_hooks.appName ? m_hooks.appName(id) : id);
        }
        said.append(namesList(names));
    }
    m_automationsRanIn = signIn;
    m_openedAtSignIn = open;
    saveAttention(Automations);
    if (open.isEmpty())
        return false;
    write(QStringLiteral("%1  Opened at sign-in, as you said yes to: %2.").arg(AutomationsTitle, said.join(QStringLiteral("; "))));
    if (m_hooks.openApps)
        m_hooks.openApps(open);
    return true;
}

} // namespace splitrock::core
