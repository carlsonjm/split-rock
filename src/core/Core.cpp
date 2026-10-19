// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/Core.h"

#include "core/Windows.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimeZone>

namespace splitrock::core {

namespace {

const QString Storage = QStringLiteral("storage");
const QString Updates = QStringLiteral("updates");
const QString Health = QStringLiteral("health");
const QString Windows = QStringLiteral("windows");

QString stamp(const QDateTime &at)
{
    return at.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

} // namespace

QString Core::defaultFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/split-rock/core");
}

QString Core::defaultSettings()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/split-rock-corerc");
}

Core::Core(const QString &folder, const QString &settings, Hooks hooks, QObject *parent)
    : QObject(parent)
    , m_folder(folder)
    , m_settings(settings)
    , m_hooks(std::move(hooks))
{
    QDir().mkpath(m_folder);
    loadSamples();
    loadAttention();
    trimLogbook();
    m_storage = judge(m_samples, storageLimits());
    m_updates = judge(m_updatesLook, m_waitingSince, updatesLimits());
    m_health = judge(m_healthLook);
}

bool Core::enabled(const QString &topic) const
{
    QSettings settings(m_settings, QSettings::IniFormat);
    // A topic's own switch, under the switch for all of Core.
    return settings.value(QStringLiteral("General/Enabled"), true).toBool()
        && settings.value(topic.left(1).toUpper() + topic.mid(1) + QStringLiteral("/Enabled"), true).toBool();
}

int Core::notice() const
{
    const QVariant stored = QSettings(m_settings, QSettings::IniFormat)
                                .value(QStringLiteral("General/Notice"), 50);
    const QString word = stored.toString().trimmed();
    if (word == QLatin1String("quiet"))
        return 0;
    if (word == QLatin1String("eager"))
        return 100;
    bool number = false;
    const double level = word.toDouble(&number);
    if (!number)
        return 50;
    return qBound(0, qRound(level), 100);
}

namespace {

// A mark along the Notice slider: the value at 0 (Less), 50 (the default) and
// 100 (More), and straight lines between.
int along(int level, int less, int balanced, int more)
{
    if (level <= 50)
        return qRound(less + (balanced - less) * level / 50.0);
    return qRound(balanced + (more - balanced) * (level - 50) / 50.0);
}

} // namespace

StorageLimits Core::storageLimits() const
{
    StorageLimits limits;
    const int n = notice();
    limits.ticker = along(n, 85, 75, 70);
    limits.island = along(n, 95, 90, 85);
    return limits;
}

UpdatesLimits Core::updatesLimits() const
{
    UpdatesLimits limits;
    const int n = notice();
    limits.ticker = along(n, 21, 7, 3);
    limits.island = along(n, 45, 21, 10);
    return limits;
}

void Core::tell(const QString &topic, const QString &title, const Spoken &spoken, const QString &line,
                const QString &detail)
{
    if (spoken.ignored)
        write(QStringLiteral("%1  The island went unanswered for a day, so it closed.").arg(title));
    if (spoken.settled)
        write(QStringLiteral("%1  Settled at %2, %3.").arg(title, line, detail));
    if (spoken.ticker) {
        write(QStringLiteral("%1  Told the ticker: %2, %3.").arg(title, line, detail));
        if (m_hooks.notify)
            m_hooks.notify(topic, title + QStringLiteral(" · ") + line, detail);
    }
    if (spoken.island)
        write(QStringLiteral("%1  Showed the island: %2, %3.").arg(title, line, detail));
}

void Core::check()
{
    if (!enabled(Storage) || !m_hooks.look)
        return;
    const QDateTime now = m_hooks.now();
    const Sample sample = m_hooks.look(now);
    if (sample.totalKb <= 0)
        return;
    m_samples.append(sample);
    saveSamples(false);
    judgeStorage(now);
}

void Core::judgeStorage(const QDateTime &now)
{
    if (m_samples.isEmpty())
        return;
    const StorageFact before = m_storage;
    const bool islandBefore = m_attention.islandOpen;
    const StorageLimits limits = storageLimits();
    m_storage = judge(m_samples, limits);
    const bool dnd = m_hooks.doNotDisturb && m_hooks.doNotDisturb();
    const Spoken spoken = decide(m_storage, m_attention, now, dnd, limits);

    // One line each time Core starts, so the logbook shows it is looking even
    // while the drive gives it nothing to say.
    if (!m_looked)
        write(QStringLiteral("Storage  First look since starting: %1, %2.").arg(m_storage.line, m_storage.detail));
    m_looked = true;
    tell(Storage, QStringLiteral("Storage"), spoken, m_storage.line, m_storage.detail);
    if (spoken.ticker || spoken.island || spoken.ignored || spoken.settled || islandBefore != m_attention.islandOpen)
        saveAttention(Storage);

    if (before.percent != m_storage.percent || before.level != m_storage.level || before.detail != m_storage.detail
        || islandBefore != m_attention.islandOpen)
        Q_EMIT factChanged(storageFact());
}

void Core::reconsider()
{
    // Judged again from the looks already taken: a change of settings is no
    // reason to look at the drive.
    if (enabled(Storage))
        judgeStorage(m_hooks.now());
    if (!m_updatesLook.known)
        return;
    UpdatesLook again = m_updatesLook;
    again.at = m_hooks.now();
    seeUpdates(again);
}

bool Core::watchesUpdates() const
{
    return enabled(Updates);
}

void Core::seeUpdates(const UpdatesLook &look)
{
    if (!enabled(Updates))
        return;
    if (!look.known) {
        if (!m_updatesLooked)
            write(QStringLiteral("Updates  Could not check for updates; will try again later."));
        m_updatesLooked = true;
        return;
    }
    const UpdatesFact before = m_updates;
    const bool islandBefore = m_updatesAttention.islandOpen;

    // Since when updates have waited without a break, for a computer with no
    // record of its own.
    if (look.waiting <= 0)
        m_waitingSince = QDateTime();
    else if (!m_waitingSince.isValid())
        m_waitingSince = look.at;
    m_updatesLook = look;
    const UpdatesLimits limits = updatesLimits();
    m_updates = judge(look, m_waitingSince, limits);

    Measure measure;
    measure.known = true;
    measure.level = m_updates.level;
    measure.score = m_updates.daysBehind;
    measure.step = 7;
    measure.settled = m_updates.waiting == 0;
    const bool dnd = m_hooks.doNotDisturb && m_hooks.doNotDisturb();
    const Spoken spoken = decide(measure, m_updatesAttention, look.at, dnd);

    if (!m_updatesLooked)
        write(QStringLiteral("Updates  First look since starting: %1, %2.").arg(m_updates.line, m_updates.detail));
    m_updatesLooked = true;
    tell(Updates, QStringLiteral("Updates"), spoken, m_updates.line, m_updates.detail);
    // Updated while Core had said nothing yet: the logbook still records it.
    if (!spoken.settled && before.known && before.waiting > 0 && m_updates.waiting == 0)
        write(QStringLiteral("Updates  Settled at %1, %2.").arg(m_updates.line, m_updates.detail));
    saveAttention(Updates);

    if (!before.known || before.waiting != m_updates.waiting || before.level != m_updates.level
        || before.detail != m_updates.detail || islandBefore != m_updatesAttention.islandOpen)
        Q_EMIT factChanged(updatesFact());
}

bool Core::healthDue() const
{
    if (!enabled(Health))
        return false;
    return !m_healthLook.at.isValid() || m_healthLook.at.secsTo(m_hooks.now()) >= HealthEveryDays * 86400;
}

void Core::seeHealth(const HealthLook &look)
{
    if (!enabled(Health))
        return;
    if (!look.known) {
        if (!m_healthLooked)
            write(QStringLiteral("Health  Could not look over the computer; will try again later."));
        m_healthLooked = true;
        return;
    }
    const HealthFact before = m_health;
    const bool islandBefore = m_healthAttention.islandOpen;
    m_healthLook = look;
    m_health = judge(look);

    // A problem more is worse; one fixed, with others left, is not news.
    Measure measure;
    measure.known = true;
    measure.level = m_health.level;
    measure.score = m_health.problems;
    measure.step = 1;
    measure.settled = m_health.problems == 0;
    const bool dnd = m_hooks.doNotDisturb && m_hooks.doNotDisturb();
    const Spoken spoken = decide(measure, m_healthAttention, look.at, dnd);

    if (!m_healthLooked)
        write(QStringLiteral("Health  First look since starting: %1, %2.").arg(m_health.line, m_health.detail));
    m_healthLooked = true;
    tell(Health, QStringLiteral("Health"), spoken, m_health.line, m_health.detail);
    saveAttention(Health);

    if (!before.known || before.problems != m_health.problems || before.level != m_health.level
        || before.detail != m_health.detail || islandBefore != m_healthAttention.islandOpen)
        Q_EMIT factChanged(healthFact());
}

bool Core::watchesDownloads() const
{
    return enabled(Windows) && !m_windowsNever;
}

void Core::seeInstaller(const QString &fileName)
{
    if (!watchesDownloads() || !isWindowsInstaller(fileName))
        return;
    const bool islandBefore = m_windowsAttention.islandOpen;
    m_installer = fileName.section(QLatin1Char('/'), -1);
    m_program = programName(m_installer);
    ++m_installers;

    // Each installer is news once; the island's daily allowance and Do Not
    // Disturb leave a second one the same day to the ticker.
    Measure measure;
    measure.known = true;
    measure.level = Level::Island;
    measure.score = m_installers;
    measure.step = 1;
    const QDateTime now = m_hooks.now();
    const bool dnd = m_hooks.doNotDisturb && m_hooks.doNotDisturb();
    const Spoken spoken = decide(measure, m_windowsAttention, now, dnd);
    const QVariantMap fact = windowsFact();
    tell(Windows, QStringLiteral("Windows programs"), spoken, fact.value(QStringLiteral("line")).toString(),
         fact.value(QStringLiteral("detail")).toString());
    saveAttention(Windows);
    if (spoken.island || islandBefore != m_windowsAttention.islandOpen)
        Q_EMIT factChanged(windowsFact());
}

QVariantMap Core::windowsFact() const
{
    const QString find = m_program.isEmpty() ? QStringLiteral("Find a Linux app") : QStringLiteral("Find %1").arg(m_program);
    return {
        {QStringLiteral("topic"), Windows},
        {QStringLiteral("title"), QStringLiteral("Windows programs")},
        {QStringLiteral("line"), QStringLiteral("%1 is for Windows").arg(m_installer)},
        {QStringLiteral("detail"), m_program.isEmpty()
             ? QStringLiteral("Windows programs don't run here. Find a Linux app that does the same")
             : QStringLiteral("Windows programs don't run here. Find the Linux version of %1").arg(m_program)},
        {QStringLiteral("program"), m_program},
        {QStringLiteral("level"), levelName(Level::Island)},
        {QStringLiteral("attention"), m_windowsAttention.islandOpen ? QStringLiteral("island") : QStringLiteral("none")},
        {QStringLiteral("icon"), QStringLiteral("application-x-ms-dos-executable")},
        {QStringLiteral("actionText"), find},
        {QStringLiteral("keywords"), QStringList{QStringLiteral("exe"), QStringLiteral("windows program"),
                                                 QStringLiteral("installer")}},
    };
}

QList<QVariantMap> Core::facts() const
{
    QList<QVariantMap> facts;
    if (enabled(Storage) && m_storage.known)
        facts.append(storageFact());
    if (enabled(Updates) && m_updates.known)
        facts.append(updatesFact());
    if (enabled(Health) && m_health.known)
        facts.append(healthFact());
    // An installer is news only while its island is up.
    if (watchesDownloads() && m_windowsAttention.islandOpen)
        facts.append(windowsFact());
    return facts;
}

QVariantMap Core::healthFact() const
{
    return {
        {QStringLiteral("topic"), Health},
        {QStringLiteral("title"), QStringLiteral("Health")},
        {QStringLiteral("line"), m_health.line},
        {QStringLiteral("detail"), m_health.detail},
        {QStringLiteral("problems"), m_health.problems},
        {QStringLiteral("level"), levelName(m_health.level)},
        {QStringLiteral("attention"), m_healthAttention.islandOpen ? QStringLiteral("island") : QStringLiteral("none")},
        {QStringLiteral("icon"), QStringLiteral("security-high")},
        {QStringLiteral("actionText"), QStringLiteral("Health check")},
        {QStringLiteral("keywords"), QStringList{QStringLiteral("health"), QStringLiteral("health check"),
                                                 QStringLiteral("problem"), QStringLiteral("check my computer")}},
    };
}

QVariantMap Core::updatesFact() const
{
    return {
        {QStringLiteral("topic"), Updates},
        {QStringLiteral("title"), QStringLiteral("Updates")},
        {QStringLiteral("line"), m_updates.line},
        {QStringLiteral("detail"), m_updates.detail},
        {QStringLiteral("waiting"), m_updates.waiting},
        {QStringLiteral("daysBehind"), m_updates.daysBehind},
        {QStringLiteral("level"), levelName(m_updates.level)},
        {QStringLiteral("attention"), m_updatesAttention.islandOpen ? QStringLiteral("island") : QStringLiteral("none")},
        {QStringLiteral("icon"), QStringLiteral("system-software-update")},
        {QStringLiteral("actionText"), QStringLiteral("Check for updates")},
        {QStringLiteral("keywords"), QStringList{QStringLiteral("updates"), QStringLiteral("update"),
                                                 QStringLiteral("upgrade"), QStringLiteral("check for updates")}},
    };
}

QVariantMap Core::storageFact() const
{
    return {
        {QStringLiteral("topic"), Storage},
        {QStringLiteral("title"), QStringLiteral("Storage")},
        {QStringLiteral("line"), m_storage.line},
        {QStringLiteral("detail"), m_storage.detail},
        {QStringLiteral("percent"), m_storage.percent},
        {QStringLiteral("level"), levelName(m_storage.level)},
        {QStringLiteral("attention"), m_attention.islandOpen ? QStringLiteral("island") : QStringLiteral("none")},
        {QStringLiteral("icon"), QStringLiteral("drive-harddisk")},
        {QStringLiteral("actionText"), QStringLiteral("Free up space")},
        {QStringLiteral("keywords"), QStringList{QStringLiteral("storage"), QStringLiteral("disk"), QStringLiteral("drive"),
                                                 QStringLiteral("space"), QStringLiteral("free up space")}},
    };
}

bool Core::act(const QString &topic, const QString &action)
{
    if (topic == Health) {
        if (!enabled(Health) || !m_health.known)
            return false;
        const bool islandBefore = m_healthAttention.islandOpen;
        if (!answer(m_healthAttention, action, m_health.problems, 1))
            return false;
        if (action == QLatin1String("open")) {
            write(QStringLiteral("Health  You opened Health check."));
            if (m_hooks.openCard)
                m_hooks.openCard(QStringLiteral("health"), QString());
        } else {
            write(QStringLiteral("Health  You %1; quiet until a problem more.")
                      .arg(action == QLatin1String("not-now") ? QStringLiteral("said not now") : QStringLiteral("dismissed it")));
        }
        saveAttention(Health);
        if (islandBefore != m_healthAttention.islandOpen)
            Q_EMIT factChanged(healthFact());
        return true;
    }
    if (topic == Windows) {
        if (!enabled(Windows) || m_installer.isEmpty())
            return false;
        const bool islandBefore = m_windowsAttention.islandOpen;
        if (action == QLatin1String("never")) {
            m_windowsNever = true;
            m_windowsAttention.islandOpen = false;
            write(QStringLiteral("Windows programs  You said not to tell you again."));
        } else if (!answer(m_windowsAttention, action, m_installers, 1)) {
            return false;
        } else if (action == QLatin1String("open")) {
            write(QStringLiteral("Windows programs  You looked for the Linux version of %1.").arg(m_installer));
            if (m_hooks.openCard)
                m_hooks.openCard(QStringLiteral("install"), m_program);
        } else {
            write(QStringLiteral("Windows programs  You %1.")
                      .arg(action == QLatin1String("not-now") ? QStringLiteral("said not now") : QStringLiteral("dismissed it")));
        }
        saveAttention(Windows);
        if (islandBefore != m_windowsAttention.islandOpen)
            Q_EMIT factChanged(windowsFact());
        return true;
    }
    if (topic == Updates) {
        if (!enabled(Updates) || !m_updates.known)
            return false;
        const bool islandBefore = m_updatesAttention.islandOpen;
        if (!answer(m_updatesAttention, action, m_updates.daysBehind, 7))
            return false;
        if (action == QLatin1String("open")) {
            write(QStringLiteral("Updates  You opened Check for updates."));
            if (m_hooks.openCard)
                m_hooks.openCard(QStringLiteral("update"), QString());
        } else {
            const int days = m_updatesAttention.quietUntil;
            write(QStringLiteral("Updates  You %1; quiet until %2 days behind.")
                      .arg(action == QLatin1String("not-now") ? QStringLiteral("said not now") : QStringLiteral("dismissed it"))
                      .arg(days));
        }
        saveAttention(Updates);
        if (islandBefore != m_updatesAttention.islandOpen)
            Q_EMIT factChanged(updatesFact());
        return true;
    }
    if (topic != Storage || !enabled(Storage) || !m_storage.known)
        return false;
    const bool islandBefore = m_attention.islandOpen;
    if (!answer(m_attention, action, m_storage.percent, AttentionLimits().step))
        return false;
    if (action == QLatin1String("open")) {
        write(QStringLiteral("Storage  You opened Free up space."));
        if (m_hooks.openCard)
            m_hooks.openCard(QStringLiteral("space"), QString());
    } else if (action == QLatin1String("not-now")) {
        write(QStringLiteral("Storage  You said not now; quiet until %1% full.").arg(m_attention.quietUntil));
    } else {
        write(QStringLiteral("Storage  You dismissed it; quiet until %1% full.").arg(m_attention.quietUntil));
    }
    saveAttention(Storage);
    if (islandBefore != m_attention.islandOpen)
        Q_EMIT factChanged(storageFact());
    return true;
}

QList<QVariantMap> Core::actions()
{
    const auto action = [](const char *id, const char *title, const char *sub, const char *icon,
                           std::initializer_list<const char *> words) {
        QStringList keywords;
        for (const char *word : words)
            keywords.append(QString::fromUtf8(word));
        return QVariantMap{{QStringLiteral("id"), QString::fromUtf8(id)},
                           {QStringLiteral("title"), QString::fromUtf8(title)},
                           {QStringLiteral("sub"), QString::fromUtf8(sub)},
                           {QStringLiteral("icon"), QString::fromUtf8(icon)},
                           {QStringLiteral("keywords"), keywords}};
    };
    // The same cards, words and order as the upkeep shelf.
    return {
        action("update", "Check for updates", "Everything, with a restore point first", "system-software-update",
               {"update", "updates", "upgrade", "check for updates", "windows update"}),
        action("space", "Free up space", "Old copies and leftovers", "drive-harddisk",
               {"free up space", "space", "storage", "disk", "clean up", "cleanup", "disk cleanup", "storage sense"}),
        action("install", "Install an app", "Say what you need, or remove one", "system-software-install",
               {"install", "install an app", "uninstall", "remove an app", "get an app", "add or remove programs",
                "programs and features", "uninstall a program", "microsoft store", "app store"}),
        action("apps", "Your apps", "The apps you added, biggest first", "view-grid",
               {"your apps", "my apps", "installed apps", "uninstall", "remove apps", "apps taking space", "app sizes"}),
        action("health", "Health check", "A look over the computer", "security-high",
               {"health", "health check", "check my computer", "diagnose", "troubleshoot", "troubleshooter"}),
        action("changes", "What changed", "Undo, or go back to before", "edit-undo",
               {"what changed", "changed", "undo", "something changed", "stopped working"}),
        action("restore", "Restore points", "Go back to an earlier state", "document-revert",
               {"restore", "restore point", "restore points", "snapshot", "go back", "system restore"}),
        action("community", "Community apps", "Read, then update, AUR apps", "applications-other",
               {"community apps", "aur", "community"}),
    };
}

bool Core::openAction(const QString &id, const QString &query)
{
    for (const QVariantMap &action : actions()) {
        if (action.value(QStringLiteral("id")).toString() != id)
            continue;
        write(QStringLiteral("Search  You opened %1.").arg(action.value(QStringLiteral("title")).toString()));
        // A Windows installer's name typed in full, "ZoomInstaller.exe",
        // asks for the program it installs: "Zoom".
        QString asked = query.trimmed();
        if (isWindowsInstaller(asked) && !programName(asked).isEmpty())
            asked = programName(asked);
        if (m_hooks.openCard)
            m_hooks.openCard(id, id == QLatin1String("install") ? asked : QString());
        return true;
    }
    return false;
}

bool Core::removeApp(const QString &desktopId)
{
    static const QRegularExpression shape(QStringLiteral("^[A-Za-z0-9_][A-Za-z0-9._+-]{0,200}\\.desktop$"));
    if (!shape.match(desktopId).hasMatch())
        return false;
    write(QStringLiteral("Search  You asked to remove %1.").arg(desktopId.chopped(8)));
    if (m_hooks.openCard)
        m_hooks.openCard(QStringLiteral("uninstall"), desktopId);
    return true;
}

void Core::write(const QString &line)
{
    QFile file(QDir(m_folder).filePath(QStringLiteral("logbook.txt")));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;
    file.write((stamp(m_hooks.now()) + QStringLiteral("  ") + line + QLatin1Char('\n')).toUtf8());
}

void Core::trimLogbook()
{
    QFile file(QDir(m_folder).filePath(QStringLiteral("logbook.txt")));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    const QString cutoff = stamp(m_hooks.now().addDays(-KeepDays));
    QStringList kept;
    bool trimmed = false;
    for (const QString &line : QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        // Each line opens with its time, written so that text order is time order.
        if (line.left(16) < cutoff)
            trimmed = true;
        else
            kept.append(line);
    }
    file.close();
    if (!trimmed)
        return;
    QSaveFile out(file.fileName());
    if (out.open(QIODevice::WriteOnly | QIODevice::Text)) {
        out.write((kept.join(QLatin1Char('\n')) + (kept.isEmpty() ? QString() : QStringLiteral("\n"))).toUtf8());
        out.commit();
    }
}

void Core::loadSamples()
{
    QFile file(QDir(m_folder).filePath(QStringLiteral("storage.tsv")));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    const QDateTime cutoff = m_hooks.now().addDays(-KeepDays);
    bool trimmed = false;
    for (const QString &line : QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QStringList cells = line.split(QLatin1Char('\t'));
        if (cells.size() != 3)
            continue;
        Sample sample;
        sample.at = QDateTime::fromString(cells[0], Qt::ISODate);
        sample.totalKb = cells[1].toLongLong();
        sample.freeKb = cells[2].toLongLong();
        if (!sample.at.isValid() || sample.totalKb <= 0)
            continue;
        if (sample.at < cutoff) {
            trimmed = true;
            continue;
        }
        m_samples.append(sample);
    }
    file.close();
    if (trimmed)
        saveSamples(true);
}

void Core::saveSamples(bool all)
{
    const QString path = QDir(m_folder).filePath(QStringLiteral("storage.tsv"));
    const auto row = [](const Sample &s) {
        return s.at.toUTC().toString(Qt::ISODate) + QLatin1Char('\t') + QString::number(s.totalKb) + QLatin1Char('\t')
            + QString::number(s.freeKb) + QLatin1Char('\n');
    };
    if (!all) {
        QFile file(path);
        if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
            file.write(row(m_samples.last()).toUtf8());
        return;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return;
    for (const Sample &sample : std::as_const(m_samples))
        file.write(row(sample).toUtf8());
    file.commit();
}

namespace {

void loadState(QSettings &saved, const QString &group, Attention &state)
{
    saved.beginGroup(group);
    state.spokenLevel = Level(qBound(0, saved.value(QStringLiteral("SpokenLevel"), 0).toInt(), 2));
    state.spokenPercent = saved.value(QStringLiteral("SpokenPercent"), 0).toInt();
    state.quietUntil = saved.value(QStringLiteral("QuietUntil"), 0).toInt();
    state.islandOpen = saved.value(QStringLiteral("IslandOpen"), false).toBool();
    state.islandSince = QDateTime::fromString(saved.value(QStringLiteral("IslandSince")).toString(), Qt::ISODate);
    state.islandsDay = QDate::fromString(saved.value(QStringLiteral("IslandsDay")).toString(), Qt::ISODate);
    state.islandsToday = saved.value(QStringLiteral("IslandsToday"), 0).toInt();
    saved.endGroup();
}

void saveState(QSettings &saved, const QString &group, const Attention &state)
{
    saved.beginGroup(group);
    saved.setValue(QStringLiteral("SpokenLevel"), int(state.spokenLevel));
    saved.setValue(QStringLiteral("SpokenPercent"), state.spokenPercent);
    saved.setValue(QStringLiteral("QuietUntil"), state.quietUntil);
    saved.setValue(QStringLiteral("IslandOpen"), state.islandOpen);
    saved.setValue(QStringLiteral("IslandSince"), state.islandSince.toUTC().toString(Qt::ISODate));
    saved.setValue(QStringLiteral("IslandsDay"), state.islandsDay.toString(Qt::ISODate));
    saved.setValue(QStringLiteral("IslandsToday"), state.islandsToday);
    saved.endGroup();
}

QDateTime savedTime(const QSettings &saved, const QString &key)
{
    return QDateTime::fromString(saved.value(key).toString(), Qt::ISODate);
}

} // namespace

void Core::loadAttention()
{
    QSettings saved(QDir(m_folder).filePath(QStringLiteral("attention.ini")), QSettings::IniFormat);
    loadState(saved, QStringLiteral("Storage"), m_attention);
    loadState(saved, QStringLiteral("Updates"), m_updatesAttention);
    // The last look for updates, so a new start has the fact before it looks again.
    m_updatesLook.at = savedTime(saved, QStringLiteral("Updates/LookedAt"));
    m_updatesLook.known = m_updatesLook.at.isValid();
    m_updatesLook.waiting = saved.value(QStringLiteral("Updates/Waiting"), 0).toInt();
    m_updatesLook.lastUpdate = savedTime(saved, QStringLiteral("Updates/LastUpdate"));
    m_waitingSince = savedTime(saved, QStringLiteral("Updates/WaitingSince"));
    // The last look at the computer's health, with its problems.
    loadState(saved, QStringLiteral("Health"), m_healthAttention);
    m_healthLook.at = savedTime(saved, QStringLiteral("Health/LookedAt"));
    m_healthLook.known = m_healthLook.at.isValid();
    const int problems = saved.beginReadArray(QStringLiteral("HealthProblems"));
    for (int i = 0; i < problems; ++i) {
        saved.setArrayIndex(i);
        m_healthLook.problems.append({saved.value(QStringLiteral("Id")).toString(), saved.value(QStringLiteral("Title")).toString(),
                                      saved.value(QStringLiteral("Detail")).toString()});
    }
    saved.endArray();
    loadState(saved, QStringLiteral("Windows"), m_windowsAttention);
    m_installer = saved.value(QStringLiteral("Windows/Installer")).toString();
    m_program = saved.value(QStringLiteral("Windows/Program")).toString();
    m_installers = saved.value(QStringLiteral("Windows/Installers"), 0).toInt();
    m_windowsNever = saved.value(QStringLiteral("Windows/Never"), false).toBool();
}

void Core::saveAttention(const QString &topic) const
{
    QSettings saved(QDir(m_folder).filePath(QStringLiteral("attention.ini")), QSettings::IniFormat);
    if (topic == Updates) {
        saveState(saved, QStringLiteral("Updates"), m_updatesAttention);
        const auto time = [](const QDateTime &at) { return at.isValid() ? at.toUTC().toString(Qt::ISODate) : QString(); };
        saved.setValue(QStringLiteral("Updates/LookedAt"), time(m_updatesLook.at));
        saved.setValue(QStringLiteral("Updates/Waiting"), m_updatesLook.waiting);
        saved.setValue(QStringLiteral("Updates/LastUpdate"), time(m_updatesLook.lastUpdate));
        saved.setValue(QStringLiteral("Updates/WaitingSince"), time(m_waitingSince));
        return;
    }
    if (topic == Health) {
        saveState(saved, QStringLiteral("Health"), m_healthAttention);
        saved.setValue(QStringLiteral("Health/LookedAt"),
                       m_healthLook.at.isValid() ? m_healthLook.at.toUTC().toString(Qt::ISODate) : QString());
        saved.remove(QStringLiteral("HealthProblems"));
        saved.beginWriteArray(QStringLiteral("HealthProblems"), int(m_healthLook.problems.size()));
        for (int i = 0; i < m_healthLook.problems.size(); ++i) {
            saved.setArrayIndex(i);
            saved.setValue(QStringLiteral("Id"), m_healthLook.problems.at(i).id);
            saved.setValue(QStringLiteral("Title"), m_healthLook.problems.at(i).title);
            saved.setValue(QStringLiteral("Detail"), m_healthLook.problems.at(i).detail);
        }
        saved.endArray();
        return;
    }
    if (topic == Windows) {
        saveState(saved, QStringLiteral("Windows"), m_windowsAttention);
        saved.setValue(QStringLiteral("Windows/Installer"), m_installer);
        saved.setValue(QStringLiteral("Windows/Program"), m_program);
        saved.setValue(QStringLiteral("Windows/Installers"), m_installers);
        saved.setValue(QStringLiteral("Windows/Never"), m_windowsNever);
        return;
    }
    saveState(saved, QStringLiteral("Storage"), m_attention);
}

} // namespace splitrock::core
