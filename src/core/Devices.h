// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QVariantMap>

#include <optional>

namespace splitrock::core {

// Devices, Core's fifth topic (docs/CORE.md § Devices): a printer, a drive
// that can be unplugged, a Bluetooth device, a game controller or a second
// screen, the first time the computer sees it. Core says so once, at the ticker, and a tap opens KDE's own
// settings page for that kind of device.

enum class DeviceKind { Printer, Drive, Bluetooth, Controller, Screen };

// Every kind, in the order Core lists them.
constexpr DeviceKind DeviceKinds[] = {DeviceKind::Printer, DeviceKind::Drive, DeviceKind::Bluetooth,
                                      DeviceKind::Controller, DeviceKind::Screen};

struct Device {
    DeviceKind kind = DeviceKind::Drive;
    // What it is known by from one plug-in to the next: a drive's id, a
    // Bluetooth address, a printer's model.
    QString id;
    // What the person would call it, such as "SanDisk Cruzer Blade".
    QString name;
};

// "printer", "drive", "bluetooth", "controller" or "screen", for the saved
// state and the logbook.
QString deviceKindName(DeviceKind kind);
// The kind by that name; Drive for any other.
DeviceKind deviceKindNamed(const QString &name);
// What the logbook calls them all: "printers", "Bluetooth devices".
QString deviceKindPlural(DeviceKind kind);
// "New printer", "New drive", "New Bluetooth device", "New game controller",
// "New screen".
QString newDeviceTitle(DeviceKind kind);
// The KDE settings page for that kind: kcm_printer_manager,
// kcm_device_automounter, kcm_bluetooth, kcm_gamecontroller or kcm_kscreen.
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

// The game controllers the kernel lists under root, /sys/class/input: each
// joystick device, known by its maker's and model's numbers and its own
// serial where it gives one.
QList<Device> controllersIn(const QString &root = QStringLiteral("/sys/class/input"));
// The screens plugged in under root, /sys/class/drm: each connector that
// says it is connected, known by its EDID's maker, model and serial, and
// named by the name the screen gives itself there.
QList<Device> screensIn(const QString &root = QStringLiteral("/sys/class/drm"));
// From a screen's EDID: its id and name, or nothing for one too short.
std::optional<Device> screenFrom(const QByteArray &edid, const QString &connector);

} // namespace splitrock::core
