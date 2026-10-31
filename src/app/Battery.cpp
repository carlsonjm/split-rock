// SPDX-License-Identifier: GPL-2.0-or-later
#include "app/Battery.h"

#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDir>
#include <QFile>
#include <QVariantMap>

#include <algorithm>
#include <cmath>

namespace splitrock {

namespace {

QString line(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()).trimmed() : QString();
}

// The first of these files that holds a number above nothing.
qint64 number(const QDir &dir, std::initializer_list<const char *> names)
{
    for (const char *name : names) {
        const qint64 value = line(dir.filePath(QLatin1String(name))).toLongLong();
        if (value > 0)
            return value;
    }
    return 0;
}

const QString UPower = QStringLiteral("org.freedesktop.UPower");

} // namespace

int BatteryReading::health() const
{
    if (full <= 0 || design <= 0)
        return -1;
    return std::min(100, int(std::lround(100.0 * double(full) / double(design))));
}

bool batteryFault(const QString &health)
{
    static const QStringList fine{QStringLiteral("good"), QStringLiteral("unknown"), QStringLiteral("warm"),
                                  QStringLiteral("cool"), QStringLiteral("calibration required"),
                                  QStringLiteral("no battery")};
    const QString words = health.trimmed().toLower();
    return !words.isEmpty() && !fine.contains(words);
}

QList<BatteryReading> readPowerSupply(const QString &root)
{
    QList<BatteryReading> found;
    const QDir folder(root);
    for (const QString &name : folder.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const QDir dir(folder.filePath(name));
        if (line(dir.filePath(QStringLiteral("type"))) != QLatin1String("Battery"))
            continue;
        // A mouse's, a pen's or a controller's own battery.
        if (line(dir.filePath(QStringLiteral("scope"))) == QLatin1String("Device"))
            continue;
        if (QFile::exists(dir.filePath(QStringLiteral("present"))) && line(dir.filePath(QStringLiteral("present"))) == QLatin1String("0"))
            continue;
        BatteryReading reading;
        // Energy where the battery reports it, charge otherwise; never one
        // against the other.
        reading.full = number(dir, {"energy_full"});
        reading.design = number(dir, {"energy_full_design"});
        if (reading.full <= 0 || reading.design <= 0) {
            reading.full = number(dir, {"charge_full"});
            reading.design = number(dir, {"charge_full_design"});
        }
        reading.cycles = int(number(dir, {"cycle_count"}));
        const QString health = line(dir.filePath(QStringLiteral("health")));
        if (batteryFault(health))
            reading.fault = health;
        if (reading.health() >= 0 || !reading.fault.isEmpty())
            found.append(reading);
    }
    return found;
}

QList<BatteryReading> readUPower(const QDBusConnection &bus)
{
    QList<BatteryReading> found;
    if (!bus.isConnected())
        return found;
    const QDBusReply<QList<QDBusObjectPath>> devices = bus.call(
        QDBusMessage::createMethodCall(UPower, QStringLiteral("/org/freedesktop/UPower"), UPower,
                                       QStringLiteral("EnumerateDevices")),
        QDBus::Block, 2000);
    if (!devices.isValid())
        return found;
    for (const QDBusObjectPath &device : devices.value()) {
        QDBusMessage call = QDBusMessage::createMethodCall(UPower, device.path(),
                                                           QStringLiteral("org.freedesktop.DBus.Properties"),
                                                           QStringLiteral("GetAll"));
        call.setArguments({QStringLiteral("org.freedesktop.UPower.Device")});
        const QDBusReply<QVariantMap> reply = bus.call(call, QDBus::Block, 2000);
        if (!reply.isValid())
            continue;
        const QVariantMap p = reply.value();
        // 2 is a battery; PowerSupply says it powers the computer, not a mouse.
        if (p.value(QStringLiteral("Type")).toUInt() != 2 || !p.value(QStringLiteral("PowerSupply")).toBool()
            || !p.value(QStringLiteral("IsPresent"), true).toBool())
            continue;
        BatteryReading reading;
        // Watt-hours, kept to the milliwatt-hour.
        reading.full = qint64(std::lround(p.value(QStringLiteral("EnergyFull")).toDouble() * 1000));
        reading.design = qint64(std::lround(p.value(QStringLiteral("EnergyFullDesign")).toDouble() * 1000));
        if (reading.full <= 0 || reading.design <= 0) {
            // Only its own percentage.
            const double capacity = p.value(QStringLiteral("Capacity")).toDouble();
            reading.full = capacity > 0 ? qint64(std::lround(capacity)) : 0;
            reading.design = capacity > 0 ? 100 : 0;
        }
        reading.cycles = std::max(0, p.value(QStringLiteral("ChargeCycles")).toInt());
        if (reading.health() >= 0)
            found.append(reading);
    }
    return found;
}

QList<BatteryReading> readBatteries(const QString &root, const QDBusConnection &bus)
{
    const QList<BatteryReading> kernel = readPowerSupply(root);
    return kernel.isEmpty() ? readUPower(bus) : kernel;
}

std::optional<BatteryReading> worstBattery(const QList<BatteryReading> &batteries)
{
    if (batteries.isEmpty())
        return std::nullopt;
    const auto rank = [](const BatteryReading &b) { return b.fault.isEmpty() ? (b.health() < 0 ? 101 : b.health()) : -1; };
    return *std::min_element(batteries.cbegin(), batteries.cend(),
                             [&rank](const BatteryReading &a, const BatteryReading &b) { return rank(a) < rank(b); });
}

QString batterySection(const QList<BatteryReading> &batteries)
{
    QString section = QStringLiteral("@@ battery\n");
    if (const auto battery = worstBattery(batteries)) {
        section += QStringLiteral("%1 %2 %3").arg(battery->full).arg(battery->design).arg(battery->cycles);
        if (!battery->fault.isEmpty())
            section += QLatin1Char(' ') + QString(battery->fault).replace(QLatin1Char('\n'), QLatin1Char(' '));
        section += QLatin1Char('\n');
    }
    return section;
}

} // namespace splitrock
