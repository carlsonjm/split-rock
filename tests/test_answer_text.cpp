// SPDX-License-Identifier: GPL-2.0-or-later
#include "app/AnswerText.h"

#include <QTest>

using splitrock::AnswerText;

class TestAnswerText : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void headlineThenSteps()
    {
        const AnswerText a = AnswerText::parse(QStringLiteral(
            "Two things usually fix it:\n\n1. Turn off **power saving**.\n2. Move to the `faster` mode.\n\nAsk again if not."));
        QCOMPARE(a.headline, QStringLiteral("Two things usually fix it:"));
        QCOMPARE(a.steps, (QStringList{QStringLiteral("Turn off power saving."), QStringLiteral("Move to the faster mode.")}));
        QCOMPARE(a.more, QStringList{QStringLiteral("Ask again if not.")});
    }

    void bulletsAndWrappedSteps()
    {
        const AnswerText a = AnswerText::parse(QStringLiteral(
            "Here is how:\n- Open the [settings](https://example.invalid) app\n  and choose Display.\n* Set the scale."));
        QCOMPARE(a.steps, (QStringList{QStringLiteral("Open the settings app and choose Display."),
                                       QStringLiteral("Set the scale.")}));
    }

    void stepsFirstHaveNoHeadline()
    {
        const AnswerText a = AnswerText::parse(QStringLiteral("1. One\n2. Two\n\nDone."));
        QCOMPARE(a.headline, QString());
        QCOMPARE(a.steps.size(), 2);
        QCOMPARE(a.more, QStringList{QStringLiteral("Done.")});
    }

    void plainParagraphs()
    {
        const AnswerText a = AnswerText::parse(QStringLiteral("# Answer\nIt is a scam.\n\nDon't click anything."));
        QCOMPARE(a.headline, QStringLiteral("Answer It is a scam."));
        QCOMPARE(a.more, QStringList{QStringLiteral("Don't click anything.")});
        QVERIFY(a.steps.isEmpty());
    }

    void codeFencesAndRulesDropOut()
    {
        const AnswerText a = AnswerText::parse(QStringLiteral("Run this:\n```\nls\n```\n---\nThen stop."));
        QCOMPARE(a.headline, QStringLiteral("Run this:"));
        QCOMPARE(a.more, (QStringList{QStringLiteral("ls"), QStringLiteral("Then stop.")}));
        QVERIFY(!a.headline.contains(QLatin1Char('`')));
    }

    void partialTextWhileArriving()
    {
        const AnswerText a = AnswerText::parse(QStringLiteral("Sure.\n\n1. Op"));
        QCOMPARE(a.headline, QStringLiteral("Sure."));
        QCOMPARE(a.steps, QStringList{QStringLiteral("Op")});
    }

    void emphasisKeepsWords()
    {
        QCOMPARE(AnswerText::plain(QStringLiteral("a *b* and __c__ in snake_case_name")),
                 QStringLiteral("a b and c in snake_case_name"));
    }
};

QTEST_GUILESS_MAIN(TestAnswerText)
#include "test_answer_text.moc"
