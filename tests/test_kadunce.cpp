// SPDX-License-Identifier: GPL-2.0-or-later
//
// Reading Kadunce's replies, as its docs/TETTEGOUCHE-CONTEXT.md defines them.
#include "desktop/Kadunce.h"

#include <QTest>

using namespace splitrock;

class TestKadunce : public QObject
{
    Q_OBJECT

    static QString context(const QString &presentation, const QString &selected, const QString &appId)
    {
        return QStringLiteral(R"({"schema":"co.goodinput.kadunce.workspace-context","version":1,)"
                              R"("applications":[{"windowId":"w1","appId":"org.kde.dolphin","cardId":"c1"},)"
                              R"({"windowId":"w2","appId":"%3","cardId":"c2"}],)"
                              R"("cardStage":{"active":true,"presentation":"%1","selectedCardId":"%2"}})")
            .arg(presentation, selected, appId);
    }

private Q_SLOTS:
    void anAcceptedBeginGivesThePlace()
    {
        const auto place = kadunce::parseBegin(QStringLiteral(
            R"({"protocol":1,"accepted":true,"presentationCapability":1,"output":"eDP-1",)"
            R"("card":{"x":250,"y":90,"width":780,"height":560},"active":{"x":10,"y":10,"width":1260,"height":716}})"));
        QVERIFY(place.accepted);
        QCOMPARE(place.output, QStringLiteral("eDP-1"));
        QCOMPARE(place.card, QRect(250, 90, 780, 560));
        QCOMPARE(place.active, QRect(10, 10, 1260, 716));
    }

    void aRefusedOrForeignBeginIsNoPlace()
    {
        QVERIFY(!kadunce::parseBegin(QStringLiteral(R"({"protocol":1,"accepted":false})")).accepted);
        // The launcher's protocol is not the companion's.
        QVERIFY(!kadunce::parseBegin(QStringLiteral(
            R"({"protocol":3,"accepted":true,"output":"eDP-1","card":{"x":0,"y":0,"width":9,"height":9}})")).accepted);
        QVERIFY(!kadunce::parseBegin(QStringLiteral(R"({"protocol":1,"accepted":true,"output":"eDP-1"})")).accepted);
        QVERIFY(!kadunce::parseBegin(QStringLiteral("not json")).accepted);
    }

    void thePresentationIsRead()
    {
        QCOMPARE(kadunce::presentation(context(QStringLiteral("cardLine"), {}, {})), QStringLiteral("cardLine"));
        QCOMPARE(kadunce::presentation(context(QStringLiteral("active"), {}, {})), QStringLiteral("active"));
        // Another version is not guessed at.
        QCOMPARE(kadunce::presentation(QString(context(QStringLiteral("active"), {}, {})).replace(
                     QStringLiteral("\"version\":1"), QStringLiteral("\"version\":2"))),
                 QString());
    }

    void theSelectedCardIsMatchedByApplication()
    {
        QVERIFY(kadunce::selectedIsApplication(context(QStringLiteral("active"), QStringLiteral("c2"), QStringLiteral("split-rock")),
                                               QStringLiteral("split-rock")));
        QVERIFY(kadunce::selectedIsApplication(context(QStringLiteral("active"), QStringLiteral("c2"), QStringLiteral("Split-Rock.desktop")),
                                               QStringLiteral("split-rock")));
        QVERIFY(!kadunce::selectedIsApplication(context(QStringLiteral("active"), QStringLiteral("c1"), QStringLiteral("split-rock")),
                                                QStringLiteral("split-rock")));
        // Selected, but not shown: Spread is still up.
        QVERIFY(!kadunce::selectedIsApplication(context(QStringLiteral("cardLine"), QStringLiteral("c2"), QStringLiteral("split-rock")),
                                                QStringLiteral("split-rock")));
    }
};

QTEST_GUILESS_MAIN(TestKadunce)
#include "test_kadunce.moc"
