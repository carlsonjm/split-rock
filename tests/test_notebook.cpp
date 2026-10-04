// SPDX-License-Identifier: GPL-2.0-or-later
//
// The notebook and what is never written down, in a folder of the test's own.
#include "app/Fence.h"
#include "app/NeverWritten.h"
#include "app/Notebook.h"

#include <QDir>
#include <QFile>
#include <QLocale>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace splitrock;

class TestNotebook : public QObject
{
    Q_OBJECT

    static QString read(const QString &path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
    }

    static DeviceFacts fixture() { return DeviceFacts::read(QStringLiteral(FIXTURES_DIR "/computer")); }

private Q_SLOTS:
    void theComputerIsReadAsItReportsItself()
    {
        const DeviceFacts facts = fixture();
        QCOMPARE(facts.maker, QStringLiteral("ASUSTeK COMPUTER INC."));
        QCOMPARE(facts.model, QStringLiteral("ROG Flow Z13 GZ302EA_GZ302EA"));
        QCOMPARE(facts.firmware, QStringLiteral("American Megatrends International, LLC. GZ302EA.308"));
        QCOMPARE(facts.processor, QStringLiteral("AMD RYZEN AI MAX+ 395 w/ Radeon 8060S"));
        QCOMPARE(facts.memory, QStringLiteral("62 GB"));
        QCOMPARE(facts.kernel, QStringLiteral("6.17.1-arch1-1"));
    }

    // Setup writes the rules, the record of this computer and the device's
    // page, and never overwrites what is there.
    void setupWritesWhatSplitRockKeeps()
    {
        QTemporaryDir home;
        Notebook notebook(home.filePath(QStringLiteral("Notebook")));
        QVERIFY(notebook.prepare(fixture()));
        const QString record = read(home.filePath(QStringLiteral("Notebook/This computer.md")));
        QVERIFY(record.contains(QStringLiteral("## Hardware")));
        QVERIFY(record.contains(QStringLiteral("- Model: ROG Flow Z13 GZ302EA_GZ302EA")));
        QVERIFY(record.contains(QStringLiteral("## Changes\n\nNone yet.")));
        QVERIFY(record.contains(QStringLiteral("## Fixed problems\n\nNone yet.")));
        QVERIFY(read(home.filePath(QStringLiteral("Notebook/ASSISTANT.md"))).contains(QStringLiteral("Write only inside this folder")));

        const QString device = read(home.filePath(QStringLiteral("Notebook/Device/DEVICE.md")));
        QVERIFY(device.startsWith(QStringLiteral("---\nid: rog-flow-z13-gz302ea-gz302ea\n")));
        QVERIFY(device.contains(QStringLiteral("matches: \"ROG Flow Z13 GZ302EA_GZ302EA\"")));
        QVERIFY(device.contains(QStringLiteral("format: 1\n---")));

        // What the person or the assistant wrote stays.
        QFile edited(home.filePath(QStringLiteral("Notebook/This computer.md")));
        QVERIFY(edited.open(QIODevice::Append));
        edited.write("\n- 3 October: the pen stopped; its battery was flat. Charged it.\n");
        edited.close();
        QVERIFY(notebook.prepare(fixture()));
        QVERIFY(read(home.filePath(QStringLiteral("Notebook/This computer.md"))).contains(QStringLiteral("the pen stopped")));
        QCOMPARE(notebook.fixedProblems().size(), 1);
        QCOMPARE(notebook.fixedProblems().first().text,
                 QStringLiteral("3 October: the pen stopped; its battery was flat. Charged it."));
    }

    // Keep this files an answer by topic, with a plain title, the date and the
    // question; Forget sends it to the trash.
    void keepAndForget()
    {
        QTemporaryDir home;
        Notebook notebook(home.filePath(QStringLiteral("Notebook")));
        QVERIFY(notebook.prepare(fixture()));
        QString error;
        const QString path = notebook.keep(QStringLiteral("How do I connect to Wi-Fi?"),
                                           QStringLiteral("Tap the network icon.\n\n1. Choose your network."), &error);
        QVERIFY2(!path.isEmpty(), qPrintable(error));
        QCOMPARE(path, home.filePath(QStringLiteral("Notebook/Notes/Network/How do I connect to Wi-Fi.md")));
        const QString note = read(path);
        QVERIFY(note.contains(QStringLiteral("title: \"How do I connect to Wi-Fi\"")));
        QVERIFY(note.contains(QStringLiteral("question: \"How do I connect to Wi-Fi?\"")));
        QVERIFY(note.contains(QStringLiteral("date: ") + QDate::currentDate().toString(Qt::ISODate)));
        QVERIFY(note.endsWith(QStringLiteral("1. Choose your network.\n")));

        // The same question again gets a note of its own.
        const QString second = notebook.keep(QStringLiteral("How do I connect to Wi-Fi?"), QStringLiteral("Again."));
        QVERIFY(second.endsWith(QStringLiteral("How do I connect to Wi-Fi 2.md")));
        QCOMPARE(notebook.kept().size(), 2);
        QVERIFY(notebook.memory().contains(QStringLiteral("- How do I connect to Wi-Fi (Notes/Network/")));

        QVERIFY(notebook.forget(second, &error));
        QVERIFY(!QFile::exists(second));
        QCOMPARE(notebook.kept().size(), 1);
        // Only kept notes are forgotten.
        QVERIFY(!notebook.forget(home.filePath(QStringLiteral("Notebook/This computer.md")), &error));
        QVERIFY(QFile::exists(home.filePath(QStringLiteral("Notebook/This computer.md"))));
    }

    // That fixed it: newest first, in place of "None yet", dated, in the
    // person's own words.
    void fixedProblemsAreRecorded()
    {
        QTemporaryDir home;
        Notebook notebook(home.filePath(QStringLiteral("Notebook")));
        QVERIFY(notebook.prepare(fixture()));
        QVERIFY(notebook.recordFixed(QStringLiteral("My headphones keep cutting out."),
                                     QStringLiteral("Turned Wi-Fi power saving off.")));
        QVERIFY(notebook.recordFixed(QStringLiteral("The pen stops."), QStringLiteral("Charged its battery.")));
        const QString today = QLocale::c().toString(QDate::currentDate(), QStringLiteral("d MMMM yyyy"));
        const auto problems = notebook.fixedProblems();
        QCOMPARE(problems.size(), 2);
        QCOMPARE(problems.at(0).when, today);
        QCOMPARE(problems.at(0).text, QStringLiteral("\u201CThe pen stops.\u201D Charged its battery."));
        QCOMPARE(problems.at(1).text, QStringLiteral("\u201CMy headphones keep cutting out.\u201D Turned Wi-Fi power saving off."));
        const QString record = read(home.filePath(QStringLiteral("Notebook/This computer.md")));
        QVERIFY(!record.contains(QStringLiteral("## Fixed problems\n\nNone yet.")));
        QVERIFY(record.contains(QStringLiteral("## Changes\n\nNone yet.")));
        QVERIFY(record.contains(QStringLiteral("- Model: ROG Flow Z13")));
        QString error;
        QVERIFY(!notebook.recordFixed(QStringLiteral("Wi-Fi"), QStringLiteral("The password is \"hunter22\"."), &error));
        QCOMPARE(notebook.fixedProblems().size(), 2);
    }

    // "Remember that?", answered yes: About me.md gains the line, dated.
    void rememberedAboutThePerson()
    {
        QTemporaryDir home;
        Notebook notebook(home.filePath(QStringLiteral("Notebook")));
        QVERIFY(notebook.prepare(fixture()));
        QVERIFY(notebook.rememberAboutPerson(QStringLiteral("Likes one step at a time.")));
        QVERIFY(notebook.rememberAboutPerson(QStringLiteral("Their daughter helps with the printer.")));
        const QString page = read(home.filePath(QStringLiteral("Notebook/About me.md")));
        QVERIFY(page.startsWith(QStringLiteral("# About me\n")));
        QVERIFY(page.contains(QStringLiteral("- Likes one step at a time. (")));
        QVERIFY(page.contains(QStringLiteral("- Their daughter helps with the printer. (")));
        QString error;
        QVERIFY(!notebook.rememberAboutPerson(QStringLiteral("Their PIN is 4821!"), &error));
        QVERIFY(!read(home.filePath(QStringLiteral("Notebook/About me.md"))).contains(QStringLiteral("4821")));
        QVERIFY(notebook.memory().contains(QStringLiteral("Likes one step at a time.")));
    }

    void topicsAreChosenFromTheWords()
    {
        QCOMPARE(Notebook::topicFor(QStringLiteral("Make the writing bigger"), {}), QStringLiteral("Display"));
        QCOMPARE(Notebook::topicFor(QStringLiteral("A window says my computer is infected"), {}),
                 QStringLiteral("Accounts and safety"));
        QCOMPARE(Notebook::topicFor(QStringLiteral("My headphones keep cutting out"), {}), QStringLiteral("Sound"));
        QCOMPARE(Notebook::topicFor(QStringLiteral("What is this?"), QStringLiteral("Nothing in particular.")),
                 QStringLiteral("Other"));
    }

    // Passwords, card numbers, bank details and codes are never written down.
    void secretsAreNeverWrittenDown()
    {
        const QStringList refused{
            QStringLiteral("My card is 4111 1111 1111 1111"),
            QStringLiteral("card 5500-0000-0000-0004 expires soon"),
            QStringLiteral("Wi-Fi password: Summer2026!"),
            QStringLiteral("the password is \"hunter22\""),
            QStringLiteral("my PIN is 4821!"),
            QStringLiteral("Your verification code is 482913"),
            QStringLiteral("IBAN GB82 WEST 1234 5698 7654 32"),
            QStringLiteral("account number: 12345678"),
        };
        for (const QString &text : refused)
            QVERIFY2(!neverWrittenReason(text).isEmpty(), qPrintable(text));

        const QStringList allowed{
            QStringLiteral("The password is stored in KDE Wallet."),
            QStringLiteral("Error code 404 means the page was not found."),
            QStringLiteral("Scaling to 125% makes everything bigger."),
            QStringLiteral("Firmware GZ302EA.308, from 05/20/2026"),
            QStringLiteral("Type its password when the system asks."),
            QStringLiteral("The serial number is 1234567890123."),
        };
        for (const QString &text : allowed)
            QVERIFY2(neverWrittenReason(text).isEmpty(), qPrintable(text));

        QTemporaryDir home;
        Notebook notebook(home.filePath(QStringLiteral("Notebook")));
        QVERIFY(notebook.prepare(fixture()));
        QString error;
        QVERIFY(notebook.keep(QStringLiteral("Remember my card"), QStringLiteral("It is 4111 1111 1111 1111."), &error).isEmpty());
        QVERIFY(error.contains(QStringLiteral("never writes down")));
        QVERIFY(!QDir(home.filePath(QStringLiteral("Notebook/Notes"))).exists());
    }

    // The fence hides the home folder, then shows the notebook and the
    // assistant's own folder to write, after everything that could cover
    // them.
    void theFenceIsBuiltInOrder()
    {
        QTemporaryDir home;
        const QString notebook = home.filePath(QStringLiteral("Notebook"));
        const QString own = home.filePath(QStringLiteral("state/codex"));
        const acp::LaunchSpec launch = fenced({QStringLiteral("/usr/bin/node"), {QStringLiteral("adapter.js")}, {}, home.path()},
                                              {{own, notebook}, {home.filePath(QStringLiteral("adapters"))}});
        const QStringList args = launch.arguments;
        QCOMPARE(args.mid(0, 3), (QStringList{QStringLiteral("--ro-bind"), QStringLiteral("/"), QStringLiteral("/")}));
        const qsizetype hidden = args.indexOf(QDir::homePath(), args.indexOf(QStringLiteral("--tmpfs")));
        QVERIFY(hidden > 0);
        QCOMPARE(args.at(hidden - 1), QStringLiteral("--tmpfs"));
        const qsizetype writes = args.indexOf(notebook);
        QVERIFY(writes > hidden);
        QCOMPARE(args.at(writes - 1), QStringLiteral("--bind"));
        QCOMPARE(args.at(args.indexOf(own) - 1), QStringLiteral("--bind"));
        QVERIFY(args.contains(QStringLiteral("--unshare-pid")));
        QVERIFY(args.contains(QStringLiteral("--die-with-parent")));
        QCOMPARE(args.mid(args.size() - 3),
                 (QStringList{QStringLiteral("--"), QStringLiteral("/usr/bin/node"), QStringLiteral("adapter.js")}));
        // Nothing the computer lacks is asked to be hidden.
        if (!QFileInfo(QStringLiteral("/run/dbus")).isDir())
            QVERIFY(!args.contains(QStringLiteral("/run/dbus")));
    }

    // Inside means inside, links and climbing out included.
    void insideTheNotebookOnly()
    {
        QTemporaryDir home;
        Notebook notebook(home.filePath(QStringLiteral("Notebook")));
        QVERIFY(notebook.prepare(fixture()));
        QVERIFY(notebook.contains(home.filePath(QStringLiteral("Notebook/Device/new.md"))));
        QVERIFY(notebook.contains(home.filePath(QStringLiteral("Notebook/Notes/New topic/new.md"))));
        QVERIFY(!notebook.contains(home.filePath(QStringLiteral("Notebook/../outside.md"))));
        QVERIFY(!notebook.contains(home.filePath(QStringLiteral("Notebook-other/a.md"))));
        QVERIFY(QFile::link(home.path(), home.filePath(QStringLiteral("Notebook/out"))));
        QVERIFY(!notebook.contains(home.filePath(QStringLiteral("Notebook/out/escape.md"))));
    }
};

QTEST_GUILESS_MAIN(TestNotebook)
#include "test_notebook.moc"
