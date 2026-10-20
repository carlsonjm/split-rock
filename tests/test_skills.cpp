// SPDX-License-Identifier: GPL-2.0-or-later
// The settings skills Ask's front desk opens: the shipped shelf, and a skill
// opened with what its look-around reads on this computer.

#include "guide/SkillShelf.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace splitrock;

namespace {

// A shelf of one skill, whose look-around is `look`.
SkillShelf shelfWith(const QTemporaryDir &root, const QString &look)
{
    QDir().mkpath(root.filePath(QStringLiteral("skills/plasma")));
    QFile file(root.filePath(QStringLiteral("skills/plasma/look.md")));
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QStringLiteral("---\nid: look\ntitle: The desktop's look\nalso:\n  - Make it dark\nchecked: unreleased\n"
                                  "format: 1\n---\n\n## Look around\n\n```sh\n%1\n```\n\n## Change\n\n"
                                  "Use plasma-apply-lookandfeel.\n").arg(look).toUtf8());
    }
    file.close();
    SkillShelf shelf;
    shelf.load({root.path()});
    return shelf;
}

} // namespace

class TestSkills : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    // The settings skills, as shipped, read cleanly, and the hint finds the
    // one a question needs, or none.
    void theShippedSkills()
    {
        SkillShelf shelf;
        shelf.load({QStringLiteral(PROJECT_DIR)});
        QVERIFY2(shelf.problems().isEmpty(), qPrintable(shelf.problems().join(QLatin1Char('\n'))));
        QVERIFY(shelf.skill(QStringLiteral("look")));
        QVERIFY(shelf.skill(QStringLiteral("pointer")));
        QVERIFY(shelf.skill(QStringLiteral("fans")));
        for (const Skill &skill : shelf.skills())
            QVERIFY2(!skill.look.trimmed().isEmpty(), qPrintable(skill.id));
        QVERIFY(shelf.shelf().contains(QStringLiteral("pointer: ")));
        const auto hint = [&shelf](const char *question) {
            const Skill *skill = shelf.find(QString::fromLatin1(question));
            return skill ? skill->id : QString();
        };
        QCOMPARE(hint("Make it dark please"), QStringLiteral("look"));
        QCOMPARE(hint("Make the pointer easier to see"), QStringLiteral("pointer"));
        QCOMPARE(hint("the fan is so loud"), QStringLiteral("fans"));
        QCOMPARE(hint("how do I print a letter"), QString());
    }

    void aSkillOpensWithWhatItReadsHere()
    {
        QTemporaryDir root;
        const SkillShelf shelf = shelfWith(root, QStringLiteral("echo \"Theme now: Breeze\""));
        QVERIFY2(shelf.problems().isEmpty(), qPrintable(shelf.problems().join(QLatin1Char('\n'))));
        QString answer;
        QObject context;
        shelf.open(QStringLiteral("look"), &context, [&answer](const QString &text) { answer = text; });
        QTRY_VERIFY(!answer.isEmpty());
        QVERIFY(answer.startsWith(QStringLiteral("# The desktop's look\n")));
        QVERIFY(answer.contains(QStringLiteral("Use plasma-apply-lookandfeel.")));
        QVERIFY(answer.contains(QStringLiteral("## On this computer now\n\nTheme now: Breeze")));

        // An id not on the shelf is answered with the shelf, and runs nothing.
        answer.clear();
        shelf.open(QStringLiteral("wifi"), &context, [&answer](const QString &text) { answer = text; });
        QVERIFY(answer.contains(QStringLiteral("no settings skill called \"wifi\"")));
        QVERIFY(answer.contains(QStringLiteral("look: The desktop's look")));
    }

    void aLookThatKeepsRunningIsStopped()
    {
        QTemporaryDir root;
        const SkillShelf shelf = shelfWith(root, QStringLiteral("echo started; sleep 30"));
        QString answer;
        QObject context;
        shelf.open(QStringLiteral("look"), &context, [&answer](const QString &text) { answer = text; }, 300);
        QTRY_VERIFY_WITH_TIMEOUT(!answer.isEmpty(), 5000);
        QVERIFY(answer.contains(QStringLiteral("Stopped: it was still running")));
    }

    void aSkillWithoutALookIsNamed()
    {
        QString problem;
        SkillShelf::parse(QStringLiteral("x.md"),
                          QStringLiteral("---\nid: x\ntitle: X\nformat: 1\n---\n\n## Change\n\nSomething.\n"),
                          QStringLiteral("plasma"), &problem);
        QVERIFY(problem.contains(QStringLiteral("Look around")));
    }
};

QTEST_GUILESS_MAIN(TestSkills)
#include "test_skills.moc"
