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
    m_storage = judge(m_samples);
}

bool Core::enabled(const QString &topic) const
{
    QSettings settings(m_settings, QSettings::IniFormat);
    // A topic's own switch, under the switch for all of Core.
    return settings.value(QStringLiteral("General/Enabled"), true).toBool()
        && settings.value(topic.left(1).toUpper() + topic.mid(1) + QStringLiteral("/Enabled"), true).toBool();
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

    const StorageFact before = m_storage;
    const bool islandBefore = m_attention.islandOpen;
    m_storage = judge(m_samples);
    const bool dnd = m_hooks.doNotDisturb && m_hooks.doNotDisturb();
    const Spoken spoken = decide(m_storage, m_attention, now, dnd);

    if (spoken.ignored)
        write(QStringLiteral("Storage  The island went unanswered for a day, so it closed."));
    if (spoken.settled)
        write(QStringLiteral("Storage  Settled at %1, %2.").arg(m_storage.line, m_storage.detail));
    if (spoken.ticker) {
        write(QStringLiteral("Storage  Told the ticker: %1, %2.").arg(m_storage.line, m_storage.detail));
        if (m_hooks.notify)
            m_hooks.notify(QStringLiteral("Storage · ") + m_storage.line, m_storage.detail);
    }
    if (spoken.island)
        write(QStringLiteral("Storage  Showed the island: %1, %2.").arg(m_storage.line, m_storage.detail));
    if (spoken.ticker || spoken.island || spoken.ignored || spoken.settled || islandBefore != m_attention.islandOpen)
        saveAttention();

    if (before.percent != m_storage.percent || before.level != m_storage.level || before.detail != m_storage.detail
        || islandBefore != m_attention.islandOpen)
        Q_EMIT factChanged(storageFact());
}

QList<QVariantMap> Core::facts() const
{
    QList<QVariantMap> facts;
    if (enabled(Storage) && m_storage.known)
        facts.append(storageFact());
    return facts;
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
    if (topic != Storage || !enabled(Storage) || !m_storage.known)
        return false;
    const bool islandBefore = m_attention.islandOpen;
    if (!answer(m_attention, action, m_storage.percent))
        return false;
    if (action == QLatin1String("open")) {
        write(QStringLiteral("Storage  You opened Free up space."));
        if (m_hooks.openCard)
            m_hooks.openCard(QStringLiteral("space"));
    } else if (action == QLatin1String("not-now")) {
        write(QStringLiteral("Storage  You said not now; quiet until %1% full.").arg(m_attention.quietUntil));
    } else {
        write(QStringLiteral("Storage  You dismissed it; quiet until %1% full.").arg(m_attention.quietUntil));
    }
    saveAttention();
    if (islandBefore != m_attention.islandOpen)
        Q_EMIT factChanged(storageFact());
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

void Core::loadAttention()
{
    QSettings saved(QDir(m_folder).filePath(QStringLiteral("attention.ini")), QSettings::IniFormat);
    saved.beginGroup(QStringLiteral("Storage"));
    m_attention.spokenLevel = Level(qBound(0, saved.value(QStringLiteral("SpokenLevel"), 0).toInt(), 2));
    m_attention.spokenPercent = saved.value(QStringLiteral("SpokenPercent"), 0).toInt();
    m_attention.quietUntil = saved.value(QStringLiteral("QuietUntil"), 0).toInt();
    m_attention.islandOpen = saved.value(QStringLiteral("IslandOpen"), false).toBool();
    m_attention.islandSince = QDateTime::fromString(saved.value(QStringLiteral("IslandSince")).toString(), Qt::ISODate);
    saved.endGroup();
    m_attention.islandsDay = QDate::fromString(saved.value(QStringLiteral("Islands/Day")).toString(), Qt::ISODate);
    m_attention.islandsToday = saved.value(QStringLiteral("Islands/Today"), 0).toInt();
}

void Core::saveAttention() const
{
    QSettings saved(QDir(m_folder).filePath(QStringLiteral("attention.ini")), QSettings::IniFormat);
    saved.beginGroup(QStringLiteral("Storage"));
    saved.setValue(QStringLiteral("SpokenLevel"), int(m_attention.spokenLevel));
    saved.setValue(QStringLiteral("SpokenPercent"), m_attention.spokenPercent);
    saved.setValue(QStringLiteral("QuietUntil"), m_attention.quietUntil);
    saved.setValue(QStringLiteral("IslandOpen"), m_attention.islandOpen);
    saved.setValue(QStringLiteral("IslandSince"), m_attention.islandSince.toUTC().toString(Qt::ISODate));
    saved.endGroup();
    saved.setValue(QStringLiteral("Islands/Day"), m_attention.islandsDay.toString(Qt::ISODate));
    saved.setValue(QStringLiteral("Islands/Today"), m_attention.islandsToday);
}

} // namespace splitrock::core
