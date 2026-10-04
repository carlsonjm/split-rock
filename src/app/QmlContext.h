// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "app/Product.h"

#include <QFile>
#include <QQmlContext>

namespace splitrock {

// What every screen reads besides the conversation: the name the person sees,
// the mark drawn from its one file, and what the full view says of this
// computer.
inline void exposeProduct(QQmlContext *context)
{
    context->setContextProperty(QStringLiteral("productName"), productName());
    context->setContextProperty(QStringLiteral("productTitle"), productTitle());
    QFile mark(QStringLiteral(":/assets/mark.svg"));
    const QString svg = mark.open(QIODevice::ReadOnly) ? QString::fromUtf8(mark.readAll()) : QString();
    context->setContextProperty(QStringLiteral("markPath"), markPath(svg));
    context->setContextProperty(QStringLiteral("computerName"), computerName());
    context->setContextProperty(QStringLiteral("systemName"), systemName());
}

} // namespace splitrock
