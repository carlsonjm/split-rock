// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/Core.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimeZone>

namespace splitrock::core {

namespace {

const QString Storage = QStringLiteral("storage");
const QString Updates = QStringLiteral("updates");

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
}

bool Core::enabled(const QString &topic) const
{
    QSettings settings(m_settings, QSettings::IniFormat);
    // A topic's own switch, under the switch for all of Core.
    return settings.value(QStringLiteral("General/Enabled"), true).toBool()
        && settings.value(topic.left(1).toUpper() + topic.mid(1) + QStringLiteral("/Enabled"), true).toBool();
}

QString Core::notice() const
{
    const QString notice = QSettings(m_settings, QSettings::IniFormat)
                               .value(QStringLiteral("General/Notice"), QStringLiteral("balanced"))
                               .toString();
    if (notice == QLatin1String("quiet") || notice == QLatin1String("eager"))
        return notice;
    return QStringLiteral("balanced");
}

StorageLimits Core::storageLimits() const
{
    StorageLimits limits;
    const QString n = notice();
    if (n == QLatin1String("quiet")) {
        limits.ticker = 85;
        limits.island = 95;
    } else if (n == QLatin1String("eager")) {
        limits.ticker = 70;
        limits.island = 85;
    }
    return limits;
}

UpdatesLimits Core::updatesLimits() const
{
    UpdatesLimits limits;
    const QString n = notice();
    if (n == QLatin1String("quiet")) {
        limits.ticker = 21;
        limits.island = 45;
    } else if (n == QLatin1String("eager")) {
        limits.ticker = 3;
        limits.island = 10;
    }
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

QList<QVariantMap> Core::facts() const
{
    QList<QVariantMap> facts;
    if (enabled(Storage) && m_storage.known)
        facts.append(storageFact());
    if (enabled(Updates) && m_updates.known)
        facts.append(updatesFact());
    return facts;
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
               {"update", "updates", "upgrade", "check for updates"}),
        action("space", "Free up space", "Old copies and leftovers", "drive-harddisk",
               {"free up space", "space", "storage", "disk", "clean up", "cleanup"}),
        action("install", "Install an app", "Say what you need, or remove one", "system-software-install",
               {"install", "install an app", "uninstall", "remove an app", "get an app"}),
        action("health", "Health check", "A look over the computer", "security-high",
               {"health", "health check", "check my computer", "diagnose", "troubleshoot"}),
        action("changes", "What changed", "Undo, or go back to before", "edit-undo",
               {"what changed", "changed", "undo", "something changed", "stopped working"}),
        action("restore", "Restore points", "Go back to an earlier state", "document-revert",
               {"restore", "restore point", "restore points", "snapshot", "go back"}),
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
        if (m_hooks.openCard)
            m_hooks.openCard(id, id == QLatin1String("install") ? query.trimmed() : QString());
        return true;
    }
    return false;
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
    saveState(saved, QStringLiteral("Storage"), m_attention);
}

} // namespace splitrock::core
