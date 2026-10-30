// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/Devices.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>

namespace splitrock::core {

QString deviceKindName(DeviceKind kind)
{
    switch (kind) {
    case DeviceKind::Printer:
        return QStringLiteral("printer");
    case DeviceKind::Bluetooth:
        return QStringLiteral("bluetooth");
    case DeviceKind::Controller:
        return QStringLiteral("controller");
    case DeviceKind::Screen:
        return QStringLiteral("screen");
    case DeviceKind::Drive:
        break;
    }
    return QStringLiteral("drive");
}

DeviceKind deviceKindNamed(const QString &name)
{
    for (DeviceKind kind : DeviceKinds) {
        if (deviceKindName(kind) == name)
            return kind;
    }
    return DeviceKind::Drive;
}

QString deviceKindPlural(DeviceKind kind)
{
    switch (kind) {
    case DeviceKind::Bluetooth:
        return QStringLiteral("Bluetooth devices");
    case DeviceKind::Controller:
        return QStringLiteral("game controllers");
    default:
        break;
    }
    return deviceKindName(kind) + QLatin1Char('s');
}

QString newDeviceTitle(DeviceKind kind)
{
    switch (kind) {
    case DeviceKind::Printer:
        return QStringLiteral("New printer");
    case DeviceKind::Bluetooth:
        return QStringLiteral("New Bluetooth device");
    case DeviceKind::Controller:
        return QStringLiteral("New game controller");
    case DeviceKind::Screen:
        return QStringLiteral("New screen");
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
    case DeviceKind::Controller:
        return QStringLiteral("kcm_gamecontroller");
    case DeviceKind::Screen:
        return QStringLiteral("kcm_kscreen");
    case DeviceKind::Drive:
        break;
    }
    return QStringLiteral("kcm_device_automounter");
}

namespace {

QByteArray readAll(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QString firstLine(const QString &path)
{
    return QString::fromUtf8(readAll(path)).section(QLatin1Char('\n'), 0, 0).trimmed();
}

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

QList<Device> controllersIn(const QString &root)
{
    QList<Device> found;
    const QDir input(root);
    // The kernel gives each joystick a js node of its own beside its event
    // node; js is only ever a joystick or a game pad.
    for (const QString &node : input.entryList({QStringLiteral("js*")}, QDir::Dirs | QDir::System | QDir::NoDotAndDotDot, QDir::Name)) {
        const QString device = input.filePath(node + QStringLiteral("/device"));
        const QString name = tidy(firstLine(device + QStringLiteral("/name")));
        const QString vendor = firstLine(device + QStringLiteral("/id/vendor"));
        const QString product = firstLine(device + QStringLiteral("/id/product"));
        const QString serial = firstLine(device + QStringLiteral("/uniq"));
        if (vendor.isEmpty() && product.isEmpty() && name.isEmpty())
            continue;
        QString id = vendor + QLatin1Char(':') + product;
        if (!serial.isEmpty())
            id += QLatin1Char(':') + serial;
        else if (vendor.isEmpty() && product.isEmpty())
            id = name;
        bool again = false;
        for (const Device &d : found)
            again = again || d.id == id;
        if (!again)
            found.append({DeviceKind::Controller, id, name.isEmpty() ? QStringLiteral("A game controller") : name});
    }
    return found;
}

std::optional<Device> screenFrom(const QByteArray &edid, const QString &connector)
{
    if (edid.size() < 128)
        return std::nullopt;
    const auto byte = [&edid](int i) { return quint8(edid.at(i)); };
    // Three letters for the maker, five bits each.
    const quint16 maker = quint16(byte(8) << 8 | byte(9));
    QString makerCode;
    for (int shift : {10, 5, 0})
        makerCode += QChar(QLatin1Char(char('A' - 1 + ((maker >> shift) & 0x1f))));
    const quint16 model = quint16(byte(11) << 8 | byte(10));
    const quint32 serial = quint32(byte(15)) << 24 | quint32(byte(14)) << 16 | quint32(byte(13)) << 8 | byte(12);
    QString name;
    QString serialText;
    // Four descriptors of 18 bytes; 0xFC holds the screen's name, 0xFF its
    // serial in words.
    for (int at = 54; at + 18 <= 126; at += 18) {
        if (byte(at) != 0 || byte(at + 1) != 0 || byte(at + 2) != 0)
            continue;
        const QString text = QString::fromLatin1(edid.mid(at + 5, 13)).section(QLatin1Char('\n'), 0, 0).trimmed();
        if (byte(at + 3) == 0xfc)
            name = tidy(text);
        else if (byte(at + 3) == 0xff)
            serialText = text;
    }
    const QString id = QStringLiteral("%1-%2-%3")
                           .arg(makerCode)
                           .arg(model, 4, 16, QLatin1Char('0'))
                           .arg(serialText.isEmpty() ? QString::number(serial) : serialText);
    if (name.isEmpty())
        name = connector.isEmpty() ? QStringLiteral("A screen") : QStringLiteral("A screen on %1").arg(connector);
    return Device{DeviceKind::Screen, id, name};
}

QList<Device> screensIn(const QString &root)
{
    QList<Device> found;
    const QDir drm(root);
    // card0-HDMI-A-1, card1-eDP-1: each connector of each graphics card.
    for (const QString &entry : drm.entryList({QStringLiteral("card*-*")}, QDir::Dirs | QDir::System | QDir::NoDotAndDotDot, QDir::Name)) {
        const QString folder = drm.filePath(entry);
        if (firstLine(folder + QStringLiteral("/status")) != QLatin1String("connected"))
            continue;
        const QString connector = entry.section(QLatin1Char('-'), 1);
        const auto screen = screenFrom(readAll(folder + QStringLiteral("/edid")), connector);
        if (!screen)
            continue;
        bool again = false;
        for (const Device &d : found)
            again = again || d.id == screen->id;
        if (!again)
            found.append(*screen);
    }
    return found;
}

} // namespace splitrock::core
