// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QString>
#include <QVariantMap>

#include <optional>

namespace splitrock::core {

// Devices, Core's fifth topic (docs/CORE.md § Devices): a printer, a drive
// that can be unplugged or a Bluetooth device, the first time the computer
// sees it. Core says so once, at the ticker, and a tap opens KDE's own
// settings page for that kind of device.

enum class DeviceKind { Printer, Drive, Bluetooth };

struct Device {
    DeviceKind kind = DeviceKind::Drive;
    // What it is known by from one plug-in to the next: a drive's id, a
    // Bluetooth address, a printer's model.
    QString id;
    // What the person would call it, such as "SanDisk Cruzer Blade".
    QString name;
};

// "printer", "drive" or "bluetooth", for the saved state and the logbook.
QString deviceKindName(DeviceKind kind);
// "New printer", "New drive", "New Bluetooth device".
QString newDeviceTitle(DeviceKind kind);
// The KDE settings page for that kind: kcm_printer_manager,
// kcm_device_automounter or kcm_bluetooth.
QString deviceSettingsPage(DeviceKind kind);

// From UDisks2's Drive properties: a drive the person plugs in, which
// UDisks says can be removed. Nothing for the computer's own drives.
std::optional<Device> driveFrom(const QVariantMap &drive);
// From BlueZ's Device1 properties: a device once it is paired. Nothing
// before, as a pairing that never finished is no device of theirs.
std::optional<Device> bluetoothFrom(const QVariantMap &device);
// From the description systemd gives a device unit that wants
// printer.target, which udev marks on every printer it sees.
std::optional<Device> printerFrom(const QString &description);

} // namespace splitrock::core
