// SPDX-License-Identifier: GPL-2.0-or-later
#include "app/Product.h"

#include <QFile>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>

namespace splitrock {

namespace {
bool s_insideShuffle = false;
}

QString productName()
{
    return s_insideShuffle ? QStringLiteral("Shuffle Core") : QStringLiteral("Split Rock");
}

QString productTitle()
{
    return s_insideShuffle ? QStringLiteral("Shuffle") : QStringLiteral("Split Rock");
}

void setInsideShuffle(bool inside)
{
    s_insideShuffle = inside;
}

bool insideShuffle()
{
    return s_insideShuffle;
}

bool shuffleInstalled()
{
    // Shuffle's bottom surface is the one part every Shuffle install has.
    return !QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                   QStringLiteral("plasma/plasmoids/co.goodinput.shuffle.bottomsurface"),
                                   QStandardPaths::LocateDirectory).isEmpty();
}

QString computerName()
{
    QFile product(QStringLiteral("/sys/class/dmi/id/product_name"));
    if (!product.open(QIODevice::ReadOnly))
        return {};
    const QString name = QString::fromUtf8(product.readAll()).trimmed();
    // What a maker leaves when it fills in nothing.
    static const QStringList blanks{QStringLiteral("System Product Name"), QStringLiteral("To be filled by O.E.M."),
                                    QStringLiteral("Default string"), QStringLiteral("Not Specified")};
    return blanks.contains(name, Qt::CaseInsensitive) ? QString() : name;
}

QString systemName()
{
    const QString desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP");
    QString name;
    if (desktop.contains(QLatin1String("KDE"), Qt::CaseInsensitive)) {
        const QString version = qEnvironmentVariable("KDE_SESSION_VERSION");
        name = version.isEmpty() ? QStringLiteral("KDE Plasma") : QStringLiteral("KDE Plasma %1").arg(version);
    } else {
        name = desktop.isEmpty() ? QStringLiteral("Linux") : desktop.section(QLatin1Char(':'), 0, 0);
    }
    if (s_insideShuffle)
        name += QStringLiteral(" · Shuffle");
    return name;
}

QString markPath(const QString &svg)
{
    static const QRegularExpression data(QStringLiteral("<path[^>]*\\sd=\"([^\"]+)\""));
    QStringList paths;
    auto it = data.globalMatch(svg);
    while (it.hasNext())
        paths.append(it.next().captured(1));
    return paths.join(QLatin1Char(' '));
}

} // namespace splitrock
