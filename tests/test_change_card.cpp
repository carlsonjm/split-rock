// SPDX-License-Identifier: GPL-2.0-or-later
#include "app/ChangeCard.h"

#include <QTest>

using splitrock::ChangeCard;

class TestChangeCard : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void commandWithPassword()
    {
        acp::ToolCall call;
        acp::mergeToolCall(call, {{QStringLiteral("toolCallId"), QStringLiteral("c")},
                                  {QStringLiteral("title"), QStringLiteral("Run apt")},
                                  {QStringLiteral("kind"), QStringLiteral("execute")},
                                  {QStringLiteral("rawInput"), QJsonObject{{QStringLiteral("command"),
                                                                            QStringLiteral("sudo apt install x")}}}});
        const ChangeCard card = ChangeCard::describe(call, QStringLiteral("Codex"),
                                                     QStringLiteral("I can install it for you."), QStringLiteral("Install x"));
        QCOMPARE(card.heading, QStringLiteral("Run a command on this computer"));
        QCOMPARE(card.why, QStringLiteral("I can install it for you."));
        QVERIFY(card.password.startsWith(QStringLiteral("Needed.")));
        QVERIFY(card.undo.contains(QStringLiteral("not done")));
        QVERIFY(card.details.contains(QStringLiteral("Command: sudo apt install x")));
        QVERIFY(card.details.contains(QStringLiteral("nothing was changed")));
    }

    void fileEditNamesTheFile()
    {
        acp::ToolCall call;
        acp::mergeToolCall(call, {{QStringLiteral("toolCallId"), QStringLiteral("c")},
                                  {QStringLiteral("kind"), QStringLiteral("edit")},
                                  {QStringLiteral("locations"), QJsonArray{QJsonObject{{QStringLiteral("path"),
                                                                                         QStringLiteral("/home/test/.config/kdeglobals")}}}}});
        const ChangeCard card = ChangeCard::describe(call, QStringLiteral("Codex"), {}, QStringLiteral("Bigger text"));
        QCOMPARE(card.heading, QStringLiteral("Change kdeglobals"));
        QCOMPARE(card.password, QStringLiteral("Split Rock can't check this yet."));
        QVERIFY(card.why.contains(QStringLiteral("Bigger text")));
        QVERIFY(card.details.contains(QStringLiteral("File: /home/test/.config/kdeglobals")));
    }

    void wordsLikeSudoInsideOtherWordsAreNotAPassword()
    {
        acp::ToolCall call;
        call.kind = QStringLiteral("execute");
        call.rawInput = QJsonObject{{QStringLiteral("command"), QJsonArray{QStringLiteral("echo"), QStringLiteral("pseudosu")}}};
        QCOMPARE(call.command(), QStringLiteral("echo pseudosu"));
        QCOMPARE(ChangeCard::describe(call, {}, {}, {}).password, QStringLiteral("Split Rock can't check this yet."));
    }
};

QTEST_GUILESS_MAIN(TestChangeCard)
#include "test_change_card.moc"
