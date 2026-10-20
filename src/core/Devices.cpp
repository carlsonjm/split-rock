// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/Devices.h"

#include <QRegularExpression>

namespace splitrock::core {

QString deviceKindName(DeviceKind kind)
{
    switch (kind) {
    case DeviceKind::Printer:
        return QStringLiteral("printer");
    case DeviceKind::Bluetooth:
        return QStringLiteral("bluetooth");
    case DeviceKind::Drive:
        break;
    }
    return QStringLiteral("drive");
}

QString newDeviceTitle(DeviceKind kind)
{
    switch (kind) {
    case DeviceKind::Printer:
        return QStringLiteral("New printer");
    case DeviceKind::Bluetooth:
        return QStringLiteral("New Bluetooth device");
    case DeviceKind::Drive:
        break;
    }
    return QStringLiteral("New drive");
}

QString deviceSettingsPage(DeviceKind kind)
{
    switch (kind) {
    case DeviceKind::Printer:
        return QStringLiteral("kcm_printer_manager");
    case DeviceKind::Bluetooth:
        return QStringLiteral("kcm_bluetooth");
    case DeviceKind::Drive:
        break;
    }
    return QStringLiteral("kcm_device_automounter");
}

namespace {

// Names come with underscores and runs of spaces from the hardware.
QString tidy(QString name)
{
    name.replace(QLatin1Char('_'), QLatin1Char(' '));
    return name.simplified();
}

} // namespace

std::optional<Device> driveFrom(const QVariantMap &drive)
{
    const QString id = drive.value(QStringLiteral("Id")).toString();
    if (id.isEmpty() || !drive.value(QStringLiteral("Removable")).toBool())
        return std::nullopt;
    const QString name = tidy(drive.value(QStringLiteral("Vendor")).toString() + QLatin1Char(' ')
                              + drive.value(QStringLiteral("Model")).toString());
    return Device{DeviceKind::Drive, id, name.isEmpty() ? QStringLiteral("A drive") : name};
}

std::optional<Device> bluetoothFrom(const QVariantMap &device)
{
    const QString address = device.value(QStringLiteral("Address")).toString();
    if (address.isEmpty() || !device.value(QStringLiteral("Paired")).toBool())
        return std::nullopt;
    QString name = tidy(device.value(QStringLiteral("Alias")).toString());
    if (name.isEmpty())
        name = tidy(device.value(QStringLiteral("Name")).toString());
    return Device{DeviceKind::Bluetooth, address, name.isEmpty() ? address : name};
}

std::optional<Device> printerFrom(const QString &description)
{
    const QString name = tidy(description);
    if (name.isEmpty())
        return std::nullopt;
    return Device{DeviceKind::Printer, name, name};
}

} // namespace splitrock::core
