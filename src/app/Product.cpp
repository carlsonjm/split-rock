// SPDX-License-Identifier: GPL-2.0-or-later
#include "app/Product.h"

#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>

namespace splitrock {

namespace {
bool s_insideShuffle = false;
}

QString productName()
{
    return s_insideShuffle ? QStringLiteral("Genie") : QStringLiteral("Split Rock");
}

QString productTitle()
{
    return s_insideShuffle ? QStringLiteral("Shuffle Genie") : QStringLiteral("Split Rock");
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
                                   QStringLiteral("plasma/plasmoids/studio.warbler.shuffle.bottomsurface"),
                                   QStandardPaths::LocateDirectory).isEmpty();
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
