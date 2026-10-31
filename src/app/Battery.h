// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QDBusConnection>
#include <QList>
#include <QString>

#include <optional>

namespace splitrock {

// The battery's wear, for the Health check's Battery row (docs/CORE.md
// § Health): how much it holds now against when it was new, and whether it
// reports a fault. Read from the kernel's power supply folder where it is
// there, and otherwise from UPower on the system bus. Only reads.
struct BatteryReading {
    // What it holds when full now, and held when new, in the same unit
    // (µWh, µAh, or whole points when only a percentage is reported).
    qint64 full = 0;
    qint64 design = 0;
    // How many times it has been charged through, or 0 where unknown.
    int cycles = 0;
    // The fault it reports in the kernel's own words, such as "Overheat";
    // empty when it reports none.
    QString fault;

    // What it holds of what it did when new, in whole points, at most 100;
    // -1 when unknown.
    int health() const;
};

// Each battery that powers the computer under root, the kernel's
// /sys/class/power_supply: a mouse's or a pen's battery is left out.
QList<BatteryReading> readPowerSupply(const QString &root = QStringLiteral("/sys/class/power_supply"));
// The same from UPower: each battery that powers the computer.
QList<BatteryReading> readUPower(const QDBusConnection &bus);
// The kernel's power supply folder first, then UPower.
QList<BatteryReading> readBatteries(const QString &root, const QDBusConnection &bus);

// The one the Health check speaks of: the most worn, or one reporting a
// fault. Nothing when the computer has no battery or reports nothing usable.
std::optional<BatteryReading> worstBattery(const QList<BatteryReading> &batteries);

// The look's "@@ battery" section for it: full, design, cycles and the
// fault, one line; only the heading when there is none.
QString batterySection(const QList<BatteryReading> &batteries);

// Under this much of what it held when new, the battery is a problem.
constexpr int BatteryWornBelow = 70;

// Whether a fault the kernel reports is a real problem: "Good", "Unknown",
// and the temperature and calibration notes a charger reports in normal use
// are not.
bool batteryFault(const QString &health);

} // namespace splitrock
