// SPDX-License-Identifier: GPL-2.0-or-later
#include "app/PackageNames.h"

#include "app/Product.h"

#include <QList>
#include <QRegularExpression>

#include <utility>

namespace splitrock {

QString knownPackageName(const QString &package)
{
    static const QList<std::pair<QRegularExpression, QString>> names{
        {QRegularExpression(QStringLiteral("^(man-pages|man-db)$")), QStringLiteral("Manual pages")},
        {QRegularExpression(QStringLiteral("^limine$")), QStringLiteral("Startup menu (Limine)")},
        {QRegularExpression(QStringLiteral("^grub$")), QStringLiteral("Startup menu (GRUB)")},
        {QRegularExpression(QStringLiteral("^linux(-cachyos[a-z0-9-]*|-lts|-zen|-hardened)?$")),
         QStringLiteral("The system's core (Linux)")},
        {QRegularExpression(QStringLiteral("^linux-firmware")), QStringLiteral("Device firmware")},
        {QRegularExpression(QStringLiteral("^hunspell")), QStringLiteral("Spell checking (Hunspell)")},
        {QRegularExpression(QStringLiteral("^(lib32-)?mesa$")), QStringLiteral("Graphics drivers (Mesa)")},
        {QRegularExpression(QStringLiteral("^(lib32-)?nvidia")), QStringLiteral("Graphics drivers (NVIDIA)")},
        {QRegularExpression(QStringLiteral("^pipewire")), QStringLiteral("Sound (PipeWire)")},
        {QRegularExpression(QStringLiteral("^networkmanager$")), QStringLiteral("Network connections")},
        {QRegularExpression(QStringLiteral("^bluez")), QStringLiteral("Bluetooth")},
        {QRegularExpression(QStringLiteral("^(plasma-workspace|plasma-desktop|kwin)$")), QStringLiteral("Plasma desktop")},
    };
    // The suite's own parts, by the names Shuffle gives them inside it.
    static const QMap<QString, std::pair<QString, QString>> suite{
        {QStringLiteral("kadunce"), {QStringLiteral("Shuffle Cards"), QStringLiteral("Kadunce")}},
        {QStringLiteral("tettegouche"), {QStringLiteral("Shuffle Search"), QStringLiteral("Tettegouche")}},
        {QStringLiteral("temperance"), {QStringLiteral("Shuffle Status"), QStringLiteral("Temperance")}},
        {QStringLiteral("gooseberry"), {QStringLiteral("Shuffle Notes"), QStringLiteral("Gooseberry")}},
        {QStringLiteral("shuffle-keyboard"), {QStringLiteral("Shuffle Keyboard"), QStringLiteral("Shuffle Keyboard")}},
        {QStringLiteral("shuffle-plasma"), {QStringLiteral("Shuffle"), QStringLiteral("Shuffle")}},
    };
    if (const auto it = suite.constFind(package); it != suite.constEnd())
        return insideShuffle() ? it->first : it->second;
    if (package == QLatin1String("split-rock") || package.startsWith(QLatin1String("split-rock-")))
        return productName();
    for (const auto &[pattern, name] : names) {
        if (pattern.match(package).hasMatch())
            return name;
    }
    return QString();
}

QMap<QString, QString> readLauncherNames(const QStringList &lines)
{
    QMap<QString, QString> names;
    for (const QString &text : lines) {
        const qsizetype tab = text.indexOf(QLatin1Char('\t'));
        const QString name = tab > 0 ? text.mid(tab + 1).trimmed() : QString();
        if (!name.isEmpty())
            names.insert(text.left(tab), name);
    }
    return names;
}

QString launcherNamesScript()
{
    return QStringLiteral(R"SH(sort -u | while IFS= read -r p; do
    case $p in ''|-*|*[!a-z0-9@._+-]*) continue ;; esac
    d=$(pacman -Qlq "$p" 2>/dev/null | grep -m 1 -E '^/usr/share/applications/[^/]+\.desktop$')
    [ -n "$d" ] || continue
    n=$(sed -n 's/^Name=//p' "$d" 2>/dev/null | head -n 1)
    [ -n "$n" ] && printf '%s\t%s\n' "$p" "$n"
done)SH");
}

QString packageName(const QString &package, const QMap<QString, QString> &launchers)
{
    const QString launcher = launchers.value(package);
    return launcher.isEmpty() ? knownPackageName(package) : launcher;
}

} // namespace splitrock
