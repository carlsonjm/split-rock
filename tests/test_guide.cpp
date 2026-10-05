// SPDX-License-Identifier: GPL-2.0-or-later
#include "guide/GuideLibrary.h"
#include "guide/SkillShelf.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace splitrock;

namespace {

QStringList ids(const QList<GuideEntry> &entries)
{
    QStringList list;
    for (const GuideEntry &e : entries)
        list.append(e.key());
    return list;
}

GuideLibrary fixtures()
{
    GuideLibrary library;
    library.load({QStringLiteral(GUIDE_FIXTURES "/system"), QStringLiteral(GUIDE_FIXTURES "/later")},
                 QStringLiteral(GUIDE_FIXTURES "/Device"));
    return library;
}

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

class TestGuide : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void readsTheFormat()
    {
        const GuideLibrary library = fixtures();
        const QList<GuideEntry> all = library.entries({{QStringLiteral("plasma")}, {}}, GuideEntry::Kind::Guide);
        const auto wifi = std::find_if(all.cbegin(), all.cend(), [](const GuideEntry &e) { return e.id == QLatin1String("wifi-drops"); });
        QVERIFY(wifi != all.cend());
        QCOMPARE(wifi->title, QStringLiteral("The Wi-Fi keeps dropping"));
        QCOMPARE(wifi->also, QStringList{QStringLiteral("The internet keeps cutting out")});
        QCOMPARE(wifi->why, QStringLiteral("The radio saves power by pausing."));
        QCOMPARE(wifi->steps, (QStringList{QStringLiteral("Open the network icon."),
                                           QStringLiteral("Turn power saving off, then on again.")}));
    }

    void anEntryFoundTwiceKeepsTheFirstRoot()
    {
        const GuideLibrary library = fixtures();
        for (const GuideEntry &e : library.entries({{QStringLiteral("plasma")}, {}}, GuideEntry::Kind::Guide))
            QVERIFY(e.title != QLatin1String("An older copy that must not win"));
    }

    void anUnknownFormatIsSkippedAndNamed()
    {
        const GuideLibrary library = fixtures();
        QVERIFY(!ids(library.entries({{QStringLiteral("plasma")}, {}}, GuideEntry::Kind::Guide))
                     .contains(QStringLiteral("plasma/from-the-future")));
        QCOMPARE(library.problems().size(), 1);
        QVERIFY(library.problems().first().contains(QStringLiteral("format 2")));
    }

    void aDesktopEntryReplacesTheGeneralOneOnlyWhereItHolds()
    {
        const GuideLibrary library = fixtures();
        const GuideContext plainPlasma{{QStringLiteral("plasma")}, {}};
        const GuideContext shuffle{{QStringLiteral("plasma"), QStringLiteral("shuffle")}, {}};
        QVERIFY(ids(library.entries(plainPlasma, GuideEntry::Kind::Guide)).contains(QStringLiteral("plasma/panel-gone")));
        QVERIFY(!ids(library.entries(plainPlasma, GuideEntry::Kind::Guide)).contains(QStringLiteral("shuffle/panel-gone")));
        QVERIFY(!ids(library.entries(shuffle, GuideEntry::Kind::Guide)).contains(QStringLiteral("plasma/panel-gone")));
        QVERIFY(ids(library.entries(shuffle, GuideEntry::Kind::Guide)).contains(QStringLiteral("shuffle/panel-gone")));
    }

    void theDeviceFolderHoldsOnlyOnItsDevice()
    {
        const GuideLibrary library = fixtures();
        const GuideContext other{{QStringLiteral("plasma")}, QStringLiteral("Some Laptop 14")};
        const GuideContext tablet{{QStringLiteral("plasma")}, QStringLiteral("Test Tablet TT1")};
        QVERIFY(!ids(library.entries(other, GuideEntry::Kind::Guide)).contains(QStringLiteral("device/pen-stops")));
        QVERIFY(ids(library.entries(tablet, GuideEntry::Kind::Guide)).contains(QStringLiteral("device/pen-stops")));
        // DEVICE.md describes the device; it is not an entry.
        for (const GuideEntry &e : library.entries(tablet, GuideEntry::Kind::Guide))
            QVERIFY(e.id != QLatin1String("test-tablet"));
    }

    void toursReadTheirSteps()
    {
        const GuideLibrary library = fixtures();
        const QList<GuideEntry> tours = library.entries({{QStringLiteral("plasma")}, {}}, GuideEntry::Kind::Tour);
        QCOMPARE(tours.size(), 1);
        QCOMPARE(tours.first().intro, QStringLiteral("Windows had a taskbar; so does this desktop."));
        QCOMPARE(tours.first().steps, (QStringList{QStringLiteral("Look at the bottom. The taskbar is there."),
                                                   QStringLiteral("Tap the clock. A calendar opens.")}));
    }

    void findsByThePersonsWords()
    {
        const GuideLibrary library = fixtures();
        const GuideContext shuffle{{QStringLiteral("plasma"), QStringLiteral("shuffle")}, QStringLiteral("Test Tablet")};
        QCOMPARE(ids(library.find(QStringLiteral("my internet keeps cutting out"), shuffle)).value(0),
                 QStringLiteral("plasma/wifi-drops"));
        QCOMPARE(ids(library.find(QStringLiteral("Wi-Fi dropping again"), shuffle)).value(0),
                 QStringLiteral("plasma/wifi-drops"));
        QCOMPARE(ids(library.find(QStringLiteral("where did my taskbar go"), shuffle)).value(0),
                 QStringLiteral("shuffle/panel-gone"));
        QCOMPARE(ids(library.find(QStringLiteral("stylus does nothing"), shuffle)).value(0),
                 QStringLiteral("device/pen-stops"));
        QVERIFY(library.find(QStringLiteral("how do I print a letter"), shuffle).isEmpty());
        QVERIFY(library.find(QStringLiteral("the"), shuffle).isEmpty());
    }

    // Split Rock's own entries, as shipped, read cleanly and answer the
    // questions they were written for.
    void theShippedEntries()
    {
        GuideLibrary library;
        library.load({QStringLiteral(PROJECT_DIR)});
        QVERIFY2(library.problems().isEmpty(), qPrintable(library.problems().join(QLatin1Char('\n'))));
        const GuideContext plasma{{QStringLiteral("plasma")}, {}};
        QVERIFY(library.entries(plasma, GuideEntry::Kind::Guide).size() >= 3);
        QVERIFY(!library.entries(plasma, GuideEntry::Kind::Tour).isEmpty());
        QCOMPARE(ids(library.find(QStringLiteral("it won't take my password"), plasma)).value(0),
                 QStringLiteral("plasma/locked-out"));
        QCOMPARE(ids(library.find(QStringLiteral("the on-screen keyboard won't come up when I click"), plasma)).value(0),
                 QStringLiteral("plasma/keyboard-needs-a-finger"));
        QCOMPARE(ids(library.find(QStringLiteral("keyboard keeps popping up by itself"), plasma)).value(0),
                 QStringLiteral("plasma/keyboard-appears-by-itself"));
        // A word many entries share is no answer on its own.
        QVERIFY(library.find(QStringLiteral("my screen is dim"), plasma).isEmpty());
    }

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

    void neverReadsFromTheHome()
    {
        const QString home = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
        for (const QString &root : GuideLibrary::systemRoots())
            QVERIFY(!root.startsWith(home + QLatin1Char('/')));
    }
};

QTEST_GUILESS_MAIN(TestGuide)
#include "test_guide.moc"
