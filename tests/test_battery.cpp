// SPDX-License-Identifier: GPL-2.0-or-later
//
// The Health check's Battery row: what the battery reports, read from a
// power supply folder of the test's own and from a stand-in UPower on a bus
// of the test's own, and the row it makes. Nothing reads this computer's
// battery or its system bus.
#include "app/Battery.h"
#include "app/HealthCheck.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusObjectPath>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace splitrock;

namespace {

void put(const QString &path, const QString &text)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(text.toUtf8() + '\n');
}

// One power supply folder, as the kernel writes it.
void supply(const QTemporaryDir &root, const QString &name, const QMap<QString, QString> &files)
{
    for (auto it = files.cbegin(); it != files.cend(); ++it)
        put(root.filePath(name + QLatin1Char('/') + it.key()), it.value());
}

HealthCheck::Item row(const QString &section)
{
    for (const HealthCheck::Item &item : HealthCheck::readLook(section + QStringLiteral("@@ end\n"))) {
        if (item.id == QLatin1String("battery"))
            return item;
    }
    return {};
}

} // namespace

// UPower's own device, as much of it as Split Rock reads.
class StandInDevice : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.UPower.Device")
    Q_PROPERTY(uint Type MEMBER type)
    Q_PROPERTY(bool PowerSupply MEMBER powerSupply)
    Q_PROPERTY(bool IsPresent MEMBER present)
    Q_PROPERTY(double EnergyFull MEMBER energyFull)
    Q_PROPERTY(double EnergyFullDesign MEMBER energyFullDesign)
    Q_PROPERTY(double Capacity MEMBER capacity)
    Q_PROPERTY(int ChargeCycles MEMBER cycles)

public:
    uint type = 2;
    bool powerSupply = true;
    bool present = true;
    double energyFull = 0;
    double energyFullDesign = 0;
    double capacity = 0;
    int cycles = -1;
};

class StandInUPower : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.UPower")

public:
    QList<QDBusObjectPath> paths;

public Q_SLOTS:
    QList<QDBusObjectPath> EnumerateDevices() { return paths; }
};

class TestBattery : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void theKernelsFolderGivesWhatItHoldsAgainstNew()
    {
        QTemporaryDir root;
        supply(root, QStringLiteral("AC"), {{QStringLiteral("type"), QStringLiteral("Mains")}});
        supply(root, QStringLiteral("BAT0"), {{QStringLiteral("type"), QStringLiteral("Battery")},
                                             {QStringLiteral("energy_full"), QStringLiteral("48720000")},
                                             {QStringLiteral("energy_full_design"), QStringLiteral("56000000")},
                                             {QStringLiteral("cycle_count"), QStringLiteral("312")},
                                             {QStringLiteral("health"), QStringLiteral("Good")}});
        // A mouse's own battery is not the computer's.
        supply(root, QStringLiteral("hidpp_battery_0"), {{QStringLiteral("type"), QStringLiteral("Battery")},
                                                        {QStringLiteral("scope"), QStringLiteral("Device")},
                                                        {QStringLiteral("energy_full"), QStringLiteral("1")},
                                                        {QStringLiteral("energy_full_design"), QStringLiteral("10")}});
        const QList<BatteryReading> found = readPowerSupply(root.path());
        QCOMPARE(found.size(), 1);
        QCOMPARE(found.first().health(), 87);
        QCOMPARE(found.first().cycles, 312);
        QVERIFY(found.first().fault.isEmpty());

        const HealthCheck::Item item = row(batterySection(found));
        QCOMPARE(item.title, QStringLiteral("Battery"));
        QCOMPARE(item.state, QStringLiteral("good"));
        QCOMPARE(item.detail, QStringLiteral("Holds 87% of its original charge."));
        QVERIFY(item.question.isEmpty());
        QCOMPARE(HealthCheck::tile(item), QString());
    }

    void chargeCountsWhereEnergyIsMissing()
    {
        QTemporaryDir root;
        supply(root, QStringLiteral("BAT1"), {{QStringLiteral("type"), QStringLiteral("Battery")},
                                             {QStringLiteral("charge_full"), QStringLiteral("3100000")},
                                             {QStringLiteral("charge_full_design"), QStringLiteral("5000000")}});
        const QList<BatteryReading> found = readPowerSupply(root.path());
        QCOMPARE(found.size(), 1);
        QCOMPARE(found.first().health(), 62);
    }

    void aWornBatteryIsAProblemToCopyForHelp()
    {
        BatteryReading worn;
        worn.full = 64;
        worn.design = 100;
        worn.cycles = 900;
        const auto item = HealthCheck::batteryItem(worn);
        QVERIFY(item);
        QCOMPARE(item->state, QStringLiteral("bad"));
        QCOMPARE(item->action, QStringLiteral("ask"));
        QCOMPARE(item->detail, QStringLiteral("Holds 64% of its original charge. It runs out sooner than it did new."));
        QVERIFY(item->question.contains(QStringLiteral("after 900 charges")));
        QVERIFY(item->question.contains(QStringLiteral("64%")));
        QCOMPARE(HealthCheck::tile(*item), QStringLiteral("The battery has worn down"));
        // A problem comes first, and Core reads it as one.
        const QList<HealthCheck::Item> items = HealthCheck::readLook(
            QStringLiteral("@@ failed\n@@ disk\n500000000 400000000\n") + batterySection({worn}) + QStringLiteral("@@ end\n"));
        QCOMPARE(items.first().id, QStringLiteral("battery"));
        QVERIFY(HealthCheck::facts(items).startsWith(QStringLiteral("problem\tbattery\tBattery\tHolds 64%")));

        // At the mark it is still good.
        BatteryReading fine;
        fine.full = BatteryWornBelow;
        fine.design = 100;
        QCOMPARE(HealthCheck::batteryItem(fine)->state, QStringLiteral("good"));
    }

    void aFaultIsAProblemWhateverItHolds()
    {
        QTemporaryDir root;
        supply(root, QStringLiteral("BAT0"), {{QStringLiteral("type"), QStringLiteral("Battery")},
                                             {QStringLiteral("energy_full"), QStringLiteral("55000000")},
                                             {QStringLiteral("energy_full_design"), QStringLiteral("56000000")},
                                             {QStringLiteral("health"), QStringLiteral("Unspecified failure")}});
        const QList<BatteryReading> found = readPowerSupply(root.path());
        QCOMPARE(found.size(), 1);
        QCOMPARE(found.first().fault, QStringLiteral("Unspecified failure"));
        const HealthCheck::Item item = row(batterySection(found));
        QCOMPARE(item.state, QStringLiteral("bad"));
        QVERIFY(item.detail.contains(QStringLiteral("unspecified failure")));
        QVERIFY(item.question.contains(QStringLiteral("\"Unspecified failure\"")));
        QCOMPARE(HealthCheck::tile(item), QStringLiteral("The battery reports a fault"));

        // What a charger reports in normal use is no fault.
        QVERIFY(!batteryFault(QStringLiteral("Good")));
        QVERIFY(!batteryFault(QStringLiteral("Warm")));
        QVERIFY(!batteryFault(QStringLiteral("Calibration required")));
        QVERIFY(batteryFault(QStringLiteral("Overheat")));
        QVERIFY(batteryFault(QStringLiteral("Dead")));
    }

    void noBatteryNoRow()
    {
        QTemporaryDir root;
        supply(root, QStringLiteral("AC"), {{QStringLiteral("type"), QStringLiteral("Mains")}});
        QVERIFY(readPowerSupply(root.path()).isEmpty());
        QCOMPARE(batterySection({}), QStringLiteral("@@ battery\n"));
        QCOMPARE(row(batterySection({})).id, QString());
        // A battery that reports nothing it holds is no row either.
        supply(root, QStringLiteral("BAT0"), {{QStringLiteral("type"), QStringLiteral("Battery")}});
        QVERIFY(readPowerSupply(root.path()).isEmpty());
    }

    void upowerStandsInWhereTheKernelsFolderHasNone()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        QVERIFY(bus.isConnected());
        StandInUPower upower;
        StandInDevice battery;
        battery.energyFull = 41.3;
        battery.energyFullDesign = 59.0;
        battery.cycles = 410;
        StandInDevice mouse;
        mouse.powerSupply = false;
        mouse.energyFull = 1;
        mouse.energyFullDesign = 2;
        StandInDevice percentOnly;
        percentOnly.capacity = 91.4;
        StandInDevice mains;
        mains.type = 1;
        const auto exported = QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllProperties;
        QVERIFY(bus.registerObject(QStringLiteral("/org/freedesktop/UPower"), &upower, exported));
        const QList<std::pair<QString, StandInDevice *>> devices{
            {QStringLiteral("/org/freedesktop/UPower/devices/battery_BAT0"), &battery},
            {QStringLiteral("/org/freedesktop/UPower/devices/mouse_0"), &mouse},
            {QStringLiteral("/org/freedesktop/UPower/devices/battery_BAT1"), &percentOnly},
            {QStringLiteral("/org/freedesktop/UPower/devices/line_power_AC"), &mains}};
        for (const auto &[path, device] : devices) {
            QVERIFY(bus.registerObject(path, device, exported));
            upower.paths.append(QDBusObjectPath(path));
        }
        QVERIFY(bus.registerService(QStringLiteral("org.freedesktop.UPower")));

        // Qt answers a call to its own connection's objects in place.
        const QList<BatteryReading> found = readUPower(bus);
        QCOMPARE(found.size(), 2);
        QCOMPARE(found.at(0).health(), 70);
        QCOMPARE(found.at(0).cycles, 410);
        QCOMPARE(found.at(1).health(), 91);
        // The worn one is the one spoken of.
        QCOMPARE(worstBattery(found)->health(), 70);

        // The kernel's folder, where it has a battery, comes first.
        QTemporaryDir root;
        supply(root, QStringLiteral("BAT0"), {{QStringLiteral("type"), QStringLiteral("Battery")},
                                             {QStringLiteral("energy_full"), QStringLiteral("50")},
                                             {QStringLiteral("energy_full_design"), QStringLiteral("100")}});
        QCOMPARE(readPowerSupply(root.path()).first().health(), 50);

        bus.unregisterService(QStringLiteral("org.freedesktop.UPower"));
        QVERIFY(readUPower(QDBusConnection(QStringLiteral("none"))).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestBattery)
#include "test_battery.moc"
