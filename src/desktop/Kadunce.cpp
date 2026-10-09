// SPDX-License-Identifier: GPL-2.0-or-later
#include "desktop/Kadunce.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace splitrock::kadunce {

namespace {

QRect rect(const QJsonObject &o)
{
    return QRect(o.value(QStringLiteral("x")).toInt(), o.value(QStringLiteral("y")).toInt(),
                 o.value(QStringLiteral("width")).toInt(), o.value(QStringLiteral("height")).toInt());
}

QJsonObject context(const QString &reply)
{
    const QJsonObject root = QJsonDocument::fromJson(reply.toUtf8()).object();
    if (root.value(QStringLiteral("schema")).toString() != QLatin1String("co.goodinput.kadunce.workspace-context")
        || root.value(QStringLiteral("version")).toInt() != 1)
        return {};
    return root;
}

// Desktop identities as Kadunce compares them: without case, and without the
// file's suffix.
QString identity(QString id)
{
    id = id.trimmed().toLower();
    if (id.endsWith(QLatin1String(".desktop")))
        id.chop(8);
    return id;
}

} // namespace

GuestPlace parseBegin(const QString &reply)
{
    const QJsonObject root = QJsonDocument::fromJson(reply.toUtf8()).object();
    GuestPlace place;
    if (root.value(QStringLiteral("protocol")).toInt() != CompanionProtocol
        || !root.value(QStringLiteral("accepted")).toBool())
        return place;
    place.output = root.value(QStringLiteral("output")).toString();
    place.card = rect(root.value(QStringLiteral("card")).toObject());
    place.active = rect(root.value(QStringLiteral("active")).toObject());
    place.accepted = place.card.isValid() && !place.output.isEmpty();
    return place;
}

QString presentation(const QString &reply)
{
    return context(reply).value(QStringLiteral("cardStage")).toObject().value(QStringLiteral("presentation")).toString();
}

bool selectedIsApplication(const QString &reply, const QString &appId)
{
    const QJsonObject root = context(reply);
    const QJsonObject stage = root.value(QStringLiteral("cardStage")).toObject();
    const QString selected = stage.value(QStringLiteral("selectedCardId")).toString();
    if (selected.isEmpty() || stage.value(QStringLiteral("presentation")).toString() != QLatin1String("active"))
        return false;
    const QJsonArray applications = root.value(QStringLiteral("applications")).toArray();
    for (const QJsonValue &value : applications) {
        const QJsonObject application = value.toObject();
        if (application.value(QStringLiteral("cardId")).toString() == selected)
            return identity(application.value(QStringLiteral("appId")).toString()) == identity(appId);
    }
    return false;
}

} // namespace splitrock::kadunce
