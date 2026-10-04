// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QRect>
#include <QStringList>

// A message bus of the test's own, started for it and stopped with it, so no
// test reaches the person's session. Two connections to it: one for the
// program under test, one for the stand-in Kadunce.
class PrivateBus
{
public:
    PrivateBus()
    {
        m_daemon.start(QStringLiteral("dbus-daemon"),
                       {QStringLiteral("--session"), QStringLiteral("--nofork"), QStringLiteral("--print-address=1")});
        if (!m_daemon.waitForStarted(5000) || !m_daemon.waitForReadyRead(5000))
            return;
        m_address = QString::fromUtf8(m_daemon.readLine()).trimmed();
    }
    ~PrivateBus()
    {
        QDBusConnection::disconnectFromBus(QStringLiteral("program"));
        QDBusConnection::disconnectFromBus(QStringLiteral("kadunce"));
        m_daemon.kill();
        m_daemon.waitForFinished(3000);
    }
    bool isValid() const { return !m_address.isEmpty(); }
    QDBusConnection program() const { return QDBusConnection::connectToBus(m_address, QStringLiteral("program")); }
    QDBusConnection kadunce() const { return QDBusConnection::connectToBus(m_address, QStringLiteral("kadunce")); }

private:
    QProcess m_daemon;
    QString m_address;
};

// Kadunce as the card meets it: the calls of its companion guest protocol,
// each recorded, and the calls it makes back to the guest.
class FakeKadunce : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "studio.warbler.Kadunce")

public:
    explicit FakeKadunce(QDBusConnection bus)
        : m_bus(std::move(bus))
    {
        m_bus.registerService(QStringLiteral("org.kde.KWin"));
        m_bus.registerObject(QStringLiteral("/Kadunce"), this, QDBusConnection::ExportScriptableSlots);
    }

    QString presentation = QStringLiteral("cardLine");
    bool accept = true;
    QString output;
    QRect card;
    QRect active;
    QString selectedApp;
    QStringList calls;
    QString owner;
    QString ownerPath;
    QString ownerInterface;
    QString token;
    QStringList launching;

    // Calls the guest, as Kadunce does on the object it named at begin.
    void callGuest(const QString &method, const QVariantList &arguments = {})
    {
        QDBusMessage request = QDBusMessage::createMethodCall(owner, ownerPath, ownerInterface, method);
        request.setArguments(arguments);
        m_bus.asyncCall(request);
    }

    void announceChange()
    {
        m_bus.send(QDBusMessage::createSignal(QStringLiteral("/Kadunce"), QStringLiteral("studio.warbler.Kadunce"),
                                              QStringLiteral("workspaceContextChanged")));
    }

    static QJsonObject geometry(const QRect &r)
    {
        return {{QStringLiteral("x"), r.x()}, {QStringLiteral("y"), r.y()},
                {QStringLiteral("width"), r.width()}, {QStringLiteral("height"), r.height()}};
    }

public Q_SLOTS:
    Q_SCRIPTABLE int companionGuestProtocolVersion() { calls << QStringLiteral("version"); return 1; }

    Q_SCRIPTABLE QString workspaceContext()
    {
        calls << QStringLiteral("context");
        QJsonArray applications;
        if (!selectedApp.isEmpty())
            applications.append(QJsonObject{{QStringLiteral("appId"), selectedApp}, {QStringLiteral("cardId"), QStringLiteral("c9")}});
        return QString::fromUtf8(QJsonDocument(QJsonObject{
            {QStringLiteral("schema"), QStringLiteral("studio.warbler.kadunce.workspace-context")},
            {QStringLiteral("version"), 1},
            {QStringLiteral("applications"), applications},
            {QStringLiteral("cardStage"), QJsonObject{
                {QStringLiteral("presentation"), presentation},
                {QStringLiteral("selectedCardId"), selectedApp.isEmpty() ? QStringLiteral("c1") : QStringLiteral("c9")}}},
        }).toJson(QJsonDocument::Compact));
    }

    Q_SCRIPTABLE QString beginCompanionGuest(const QString &ownerService, const QString &objectPath, const QString &interfaceName)
    {
        calls << QStringLiteral("begin");
        owner = ownerService;
        ownerPath = objectPath;
        ownerInterface = interfaceName;
        QJsonObject reply{{QStringLiteral("protocol"), 1}, {QStringLiteral("accepted"), accept}};
        if (accept) {
            reply.insert(QStringLiteral("output"), output);
            reply.insert(QStringLiteral("card"), geometry(card));
            reply.insert(QStringLiteral("active"), geometry(active));
            reply.insert(QStringLiteral("presentationCapability"), 1);
        }
        return QString::fromUtf8(QJsonDocument(reply).toJson(QJsonDocument::Compact));
    }

    Q_SCRIPTABLE bool setLauncherGuestExpanded(bool expanded)
    {
        calls << (expanded ? QStringLiteral("expand") : QStringLiteral("collapse"));
        return true;
    }

    Q_SCRIPTABLE bool prepareLauncherGuestLaunch(const QStringList &applicationIds, const QString &requestToken)
    {
        calls << QStringLiteral("prepare");
        launching = applicationIds;
        token = requestToken;
        return true;
    }

    Q_SCRIPTABLE void cancelLauncherGuestLaunch() { calls << QStringLiteral("cancel"); }
    Q_SCRIPTABLE void endLauncherGuest() { calls << QStringLiteral("end"); }

private:
    QDBusConnection m_bus;
};
