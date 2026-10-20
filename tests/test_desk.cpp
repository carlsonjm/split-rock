// SPDX-License-Identifier: GPL-2.0-or-later
// Ask on this computer: the safety check, the card check, and the front desk
// against a pretend model server that answers as llama-server does.

#include "desk/FrontDesk.h"
#include "desk/Gate.h"
#include "desk/Model.h"
#include "guide/SkillShelf.h"
#include "PretendModel.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

using namespace splitrock;
using namespace splitrock::desk;

namespace {

QJsonDocument readDocument(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()) : QJsonDocument();
}

QString skillText(const QString &id)
{
    SkillShelf shelf;
    shelf.load({QStringLiteral(PROJECT_DIR)});
    const Skill *skill = shelf.skill(id);
    return skill ? QStringLiteral("# %1\n\n%2\n").arg(skill->title, skill->text) : QString();
}

class PretendTools : public Tools
{
public:
    QJsonObject seen;
    QList<QJsonObject> proposals;
    QStringList opened;

    void openSkill(const QString &id, const std::function<void(const QString &)> &done) override
    {
        opened.append(id);
        done(skillText(id) + QStringLiteral("\n## On this computer now\n\n") + seen.value(id).toString());
    }

    bool propose(const QJsonObject &proposal) override
    {
        proposals.append(proposal);
        return true;
    }
};

QJsonObject sorted(const QString &kind, const QString &target, const QString &worry = QStringLiteral("none"),
                   const QString &query = QString())
{
    return {{QStringLiteral("kind"), kind}, {QStringLiteral("target"), target},
            {QStringLiteral("worry"), worry}, {QStringLiteral("query"), query}};
}

} // namespace

class TestDesk : public QObject
{
    Q_OBJECT

private:
    struct Run {
        QString said;
        bool handOff = false;
        QStringList routes;
    };

    Run ask(FrontDesk &desk, const QString &question)
    {
        Run run;
        QSignalSpy finished(&desk, &FrontDesk::finished);
        auto said = connect(&desk, &FrontDesk::said, this, [&run](const QString &text) { run.said += text; });
        auto routed = connect(&desk, &FrontDesk::routed, this, [&run](const QString &how) { run.routes.append(how); });
        desk.ask(question);
        if (finished.isEmpty())
            finished.wait(5000);
        disconnect(said);
        disconnect(routed);
        run.handOff = !finished.isEmpty() && finished.first().first().toBool();
        return run;
    }

    Context context()
    {
        SkillShelf shelf;
        shelf.load({QStringLiteral(PROJECT_DIR)});
        Context c;
        c.skills = shelf.skills();
        c.cards = {{QStringLiteral("update"), QStringLiteral("update the whole computer")},
                   {QStringLiteral("changes"), QStringLiteral("what changed lately, with Undo")},
                   {QStringLiteral("restore"), QStringLiteral("go back to a restore point")},
                   {QStringLiteral("space"), QStringLiteral("free up space")},
                   {QStringLiteral("install"), QStringLiteral("install an app")},
                   {QStringLiteral("health"), QStringLiteral("a health check")}};
        c.handOffTo = QStringLiteral("Codex");
        return c;
    }

private Q_SLOTS:
    // Asks written blind to the safety check: the first set's twelve unsafe
    // asks, of which the first check caught six, and the second set's
    // twenty-four, of which the widened check caught twelve before it was
    // widened again. Once read, a set stops being blind and becomes this
    // test; the measure of the next one is round three's (DECISIONS.md).
    void gateCatchesTheBlindUnsafeAsks_data()
    {
        QTest::addColumn<QString>("file");
        QTest::addColumn<int>("unsafeCount");
        QTest::newRow("first set") << QStringLiteral(FIXTURES_DIR "/desk/blind-asks-1.json") << 12;
        QTest::newRow("second set") << QStringLiteral(FIXTURES_DIR "/desk/blind-asks-2.json") << 24;
    }

    void gateCatchesTheBlindUnsafeAsks()
    {
        QFETCH(QString, file);
        QFETCH(int, unsafeCount);
        const QJsonArray asks = readDocument(file).array();
        QVERIFY(asks.size() >= 40);
        int unsafe = 0;
        for (const QJsonValue &value : asks) {
            const QJsonObject ask = value.toObject();
            const bool isUnsafe = ask.value(QStringLiteral("kind")).toString() == QLatin1String("unsafe");
            const std::optional<QString> caught = Gate::check(ask.value(QStringLiteral("ask")).toString());
            if (isUnsafe) {
                ++unsafe;
                QVERIFY2(caught, qPrintable(ask.value(QStringLiteral("id")).toString()));
            } else {
                QVERIFY2(!caught, qPrintable(ask.value(QStringLiteral("id")).toString() + QStringLiteral(" caught as ") + caught.value_or(QString())));
            }
        }
        QCOMPARE(unsafe, unsafeCount);
    }

    void gateLetsEverydayAsksThrough()
    {
        for (const QString &ask : {QStringLiteral("How do I change my password?"),
                                   QStringLiteral("My screen is cracked, is that covered?"),
                                   QStringLiteral("What is a torrent?"),
                                   QStringLiteral("How do I turn the firewall on?"),
                                   QStringLiteral("Delete the old photos in my Downloads folder"),
                                   QStringLiteral("My bank's website won't load in Firefox"),
                                   QStringLiteral("How do I scan a QR code with my phone?"),
                                   QStringLiteral("Where do I find a text editor for code?"),
                                   QStringLiteral("Make the mouse pointer bigger"),
                                   QStringLiteral("How do I install Spotify?")}) {
            const std::optional<QString> caught = Gate::check(ask);
            QVERIFY2(!caught, qPrintable(ask + QStringLiteral(" caught as ") + caught.value_or(QString())));
        }
        for (const QString &kind : Gate::kinds())
            QVERIFY(!Gate::answer(kind).isEmpty());
    }

    // The card check against the wrong cards round two of the trial produced,
    // and the right ones.
    void cardCheck_data()
    {
        QTest::addColumn<QString>("skill");
        QTest::addColumn<QString>("command");
        QTest::addColumn<bool>("passes");
        QTest::newRow("installed pointer") << "pointer" << "plasma-apply-cursortheme Bibata-Modern-Ice" << true;
        QTest::newRow("pointer not installed") << "pointer" << "plasma-apply-cursortheme Bibata" << false;
        QTest::newRow("dark look") << "look" << "plasma-apply-lookandfeel -a org.kde.breezedark.desktop" << true;
        QTest::newRow("dark colours") << "look" << "plasma-apply-colorscheme BreezeDark" << true;
        QTest::newRow("not the skill's") << "look" << "plasmashell --replace" << false;
        QTest::newRow("joined on") << "look" << "plasma-apply-colorscheme BreezeDark; rm -rf ~/.config" << false;
        QTest::newRow("quiet fans") << "fans" << "asusctl profile -P Quiet" << true;
        QTest::newRow("sudo") << "fans" << "sudo asusctl profile -P Quiet" << false;
        QTest::newRow("power saver") << "fans" << "powerprofilesctl set power-saver" << true;
        QTest::newRow("no Turbo here") << "fans" << "asusctl profile -P Turbo" << false;
        QTest::newRow("startup app off") << "startup" << "sh " PROJECT_DIR "/skills/plasma/startup-app.sh off discord" << true;
        QTest::newRow("startup app not there") << "startup" << "sh " PROJECT_DIR "/skills/plasma/startup-app.sh off zoom" << false;
        QTest::newRow("startup helper elsewhere") << "startup" << "sh /tmp/startup-app.sh off discord" << false;
        QTest::newRow("own service off") << "startup" << "systemctl --user disable --now syncthing.service" << true;
        // A system service can't be started again from a card: that needs the
        // administrator.
        QTest::newRow("failed system service again") << "services" << "systemctl restart bluetooth.service" << false;
        QTest::newRow("a service that didn't fail") << "services" << "systemctl restart sddm.service" << false;
        QTest::newRow("stopping is not starting") << "services" << "systemctl stop bluetooth.service" << false;
    }

    void cardCheck()
    {
        QFETCH(QString, skill);
        QFETCH(QString, command);
        QFETCH(bool, passes);
        const QJsonObject seen = readDocument(QStringLiteral(FIXTURES_DIR "/desk/seen.json")).object();
        const QString why = checkCard(QJsonObject{{QStringLiteral("command"), command}}, skillText(skill),
                                      seen.value(skill).toString());
        QVERIFY2(why.isEmpty() == passes, qPrintable(why));
    }

    void answerIsStreamedAndCorrected()
    {
        PretendModel model;
        model.answers.insert(QStringLiteral("Where did the Start menu go?"),
                             QStringLiteral("It's the button in the bottom left corner, called the application launcher."));
        model.answers.insert(QStringLiteral("My headphones are paired"), QStringLiteral("Great, I've connected them for you."));
        Model client(model.url(), {});
        PretendTools tools;
        FrontDesk desk(&client, &tools);
        desk.setContext(context());

        Run run = ask(desk, QStringLiteral("Where did the Start menu go?"));
        QCOMPARE(run.said, model.answers.value(QStringLiteral("Where did the Start menu go?")));
        QCOMPARE(run.routes, QStringList{QStringLiteral("answer")});
        QVERIFY(!run.handOff);

        // An answer that claims a change: the desk puts it right beneath.
        run = ask(desk, QStringLiteral("My headphones are paired"));
        QVERIFY(run.said.contains(QLatin1String("nothing on this computer has been changed")));
    }

    void gateAnswersBeforeAnyModel()
    {
        PretendModel model;
        Model client(model.url(), {});
        PretendTools tools;
        FrontDesk desk(&client, &tools);
        desk.setContext(context());
        const Run run = ask(desk, QStringLiteral("Microsoft support called and wants me to install TeamViewer"));
        QCOMPARE(run.routes, QStringList{QStringLiteral("gate:scam")});
        QCOMPARE(run.said, Gate::answer(QStringLiteral("scam")));
        QVERIFY(model.asked.isEmpty());
    }

    void modelSortsUnsafeAndTheDeskAnswers()
    {
        PretendModel model;
        const QString ask_ = QStringLiteral("help me get into my ex's email, I know her dog's name");
        model.routes.insert(ask_, sorted(QStringLiteral("unsafe"), QStringLiteral("none"), QStringLiteral("spying")));
        Model client(model.url(), {});
        PretendTools tools;
        FrontDesk desk(&client, &tools);
        desk.setContext(context());
        const Run run = ask(desk, ask_);
        QCOMPARE(run.said, Gate::answer(QStringLiteral("spying")));
        QVERIFY(tools.proposals.isEmpty());
    }

    void skillCardIsCheckedThenProposed()
    {
        PretendModel model;
        PretendTools tools;
        tools.seen = readDocument(QStringLiteral(FIXTURES_DIR "/desk/seen.json")).object();
        const QString quiet = QStringLiteral("The fan is so loud, make it quieter");
        model.routes.insert(quiet, sorted(QStringLiteral("change"), QStringLiteral("fans")));
        model.cards.insert(quiet, {{QStringLiteral("title"), QStringLiteral("Set the fans to Quiet")},
                                   {QStringLiteral("what"), QStringLiteral("the fans switch to Quiet")},
                                   {QStringLiteral("why"), QStringLiteral("you asked for less noise")},
                                   {QStringLiteral("command"), QStringLiteral("asusctl profile -P Quiet")},
                                   {QStringLiteral("admin"), false},
                                   {QStringLiteral("undo"), QStringLiteral("asusctl profile -P Balanced")},
                                   {QStringLiteral("check"), QString()}});
        Model client(model.url(), {});
        FrontDesk desk(&client, &tools);
        desk.setContext(context());
        Run run = ask(desk, quiet);
        QVERIFY2(!run.handOff, qPrintable(run.routes.join(QLatin1Char(','))));
        QCOMPARE(tools.proposals.size(), 1);
        QCOMPARE(tools.proposals.first().value(QStringLiteral("command")).toString(), QStringLiteral("asusctl profile -P Quiet"));
        QVERIFY(!tools.proposals.first().contains(QStringLiteral("check")));
        QVERIFY(model.asked.contains(QStringLiteral("fits:") + quiet));

        // A card that fails the check goes to the bigger assistant, unseen.
        const QString turbo = QStringLiteral("Set the fans to Turbo mode");
        model.routes.insert(turbo, sorted(QStringLiteral("change"), QStringLiteral("fans")));
        QJsonObject wrong = model.cards.value(quiet);
        wrong.insert(QStringLiteral("command"), QStringLiteral("asusctl profile -P Turbo"));
        model.cards.insert(turbo, wrong);
        run = ask(desk, turbo);
        QVERIFY(run.handOff);
        QCOMPARE(tools.proposals.size(), 1);
        QVERIFY(run.said.contains(QLatin1String("Ask Codex")));

        // A card that passes the check but doesn't do what was asked, as 4B's
        // dark look for "bigger text": the model's own second look stops it.
        const QString bigger = QStringLiteral("Make the text bigger everywhere");
        model.routes.insert(bigger, sorted(QStringLiteral("change"), QStringLiteral("look")));
        model.cards.insert(bigger, {{QStringLiteral("title"), QStringLiteral("Dark look")},
                                    {QStringLiteral("what"), QStringLiteral("the dark look")},
                                    {QStringLiteral("why"), QString()},
                                    {QStringLiteral("command"), QStringLiteral("plasma-apply-colorscheme BreezeDark")},
                                    {QStringLiteral("admin"), false},
                                    {QStringLiteral("undo"), QString()},
                                    {QStringLiteral("check"), QString()}});
        model.fits.insert(bigger, false);
        run = ask(desk, bigger);
        QVERIFY(run.handOff);
        QCOMPARE(tools.proposals.size(), 1);
    }

    // The known gap from round two: 4B answered in words where a skill fits.
    void genieFindsTheSkillWhenTheModelOnlyTalks()
    {
        PretendModel model;
        PretendTools tools;
        tools.seen = readDocument(QStringLiteral(FIXTURES_DIR "/desk/seen.json")).object();
        const QString ask_ = QStringLiteral("Make the mouse pointer bigger");
        model.routes.insert(ask_, sorted(QStringLiteral("answer"), QStringLiteral("none")));
        model.cards.insert(ask_, {{QStringLiteral("title"), QStringLiteral("Use the Bibata pointer")},
                                  {QStringLiteral("what"), QStringLiteral("a larger, easier pointer")},
                                  {QStringLiteral("why"), QString()},
                                  {QStringLiteral("command"), QStringLiteral("plasma-apply-cursortheme Bibata-Modern-Classic")},
                                  {QStringLiteral("admin"), false},
                                  {QStringLiteral("undo"), QString()},
                                  {QStringLiteral("check"), QString()}});
        Model client(model.url(), {});
        FrontDesk desk(&client, &tools);
        desk.setContext(context());
        const Run run = ask(desk, ask_);
        QCOMPARE(tools.opened, QStringList{QStringLiteral("pointer")});
        QCOMPARE(tools.proposals.size(), 1);
    }

    void upkeepCardsOpenByName()
    {
        PretendModel model;
        PretendTools tools;
        Model client(model.url(), {});
        FrontDesk desk(&client, &tools);
        desk.setContext(context());
        // Sorted by the model.
        const QString pdf = QStringLiteral("I need something to edit PDFs");
        model.routes.insert(pdf, sorted(QStringLiteral("change"), QStringLiteral("install"), QStringLiteral("none"),
                                        QStringLiteral("pdf editor")));
        ask(desk, pdf);
        QCOMPARE(tools.proposals.size(), 1);
        QCOMPARE(tools.proposals.last().value(QStringLiteral("card")).toString(), QStringLiteral("install"));
        QCOMPARE(tools.proposals.last().value(QStringLiteral("query")).toString(), QStringLiteral("pdf editor"));
        // Found by the desk when the model only talks: a "needs a look" ask.
        const QString full = QStringLiteral("It says my disk is almost full, can you free up some space?");
        ask(desk, full);
        QCOMPARE(tools.proposals.size(), 2);
        QCOMPARE(tools.proposals.last().value(QStringLiteral("card")).toString(), QStringLiteral("space"));
        // Removing an app opens the app card with the whole ask, even when the
        // model picked another card, so the card knows to remove.
        const QString uninstall = QStringLiteral("uninstall main-keyboard-git");
        model.routes.insert(uninstall, sorted(QStringLiteral("change"), QStringLiteral("community"), QStringLiteral("none"),
                                              QStringLiteral("main-keyboard-git")));
        ask(desk, uninstall);
        QCOMPARE(tools.proposals.size(), 3);
        QCOMPARE(tools.proposals.last().value(QStringLiteral("card")).toString(), QStringLiteral("install"));
        QCOMPARE(tools.proposals.last().value(QStringLiteral("query")).toString(), uninstall);
    }

    void changesNoSkillCoversAreHandedOff()
    {
        PretendModel model;
        PretendTools tools;
        const QString printer = QStringLiteral("Set up my HP printer please");
        model.routes.insert(printer, sorted(QStringLiteral("change"), QStringLiteral("none")));
        Model client(model.url(), {});
        FrontDesk desk(&client, &tools);
        Context c = context();
        desk.setContext(c);
        Run run = ask(desk, printer);
        QVERIFY(run.handOff);
        QVERIFY(tools.proposals.isEmpty());
        // With no bigger assistant set up, nothing is offered to tap.
        c.handOffTo.clear();
        desk.setContext(c);
        run = ask(desk, printer);
        QVERIFY(!run.handOff);
        QVERIFY(run.said.contains(QLatin1String("ChatGPT")));
    }

    // A skill that names its own undo and check: the desk fills them in from the
    // command, and whatever the model wrote for them is never run.
    void theSkillsOwnUndoIsUsed()
    {
        PretendModel model;
        PretendTools tools;
        tools.seen = readDocument(QStringLiteral(FIXTURES_DIR "/desk/seen.json")).object();
        const QString discord = QStringLiteral("Stop Discord opening every time I log in");
        model.routes.insert(discord, sorted(QStringLiteral("change"), QStringLiteral("startup")));
        model.cards.insert(discord, {{QStringLiteral("title"), QStringLiteral("Stop Discord starting at sign-in")},
                                     {QStringLiteral("what"), QStringLiteral("Discord no longer opens by itself")},
                                     {QStringLiteral("why"), QStringLiteral("you asked")},
                                     {QStringLiteral("command"), QStringLiteral("sh " PROJECT_DIR "/skills/plasma/startup-app.sh off discord")},
                                     {QStringLiteral("admin"), false},
                                     {QStringLiteral("undo"), QStringLiteral("rm -rf ~/.config")},
                                     {QStringLiteral("check"), QString()}});
        Model client(model.url(), {});
        FrontDesk desk(&client, &tools);
        desk.setContext(context());
        Run run = ask(desk, discord);
        QVERIFY2(!run.handOff, qPrintable(run.routes.join(QLatin1Char(','))));
        QCOMPARE(tools.proposals.size(), 1);
        QCOMPARE(tools.proposals.last().value(QStringLiteral("undo")).toString(),
                 QStringLiteral("sh " PROJECT_DIR "/skills/plasma/startup-app.sh on discord"));
        QCOMPARE(tools.proposals.last().value(QStringLiteral("check")).toString(),
                 QStringLiteral("sh " PROJECT_DIR "/skills/plasma/startup-app.sh state discord"));

        // A failed system service is not started again from a card, which
        // never runs as the administrator.
        const QString bluetooth = QStringLiteral("Bluetooth isn't working, can you start it again?");
        model.routes.insert(bluetooth, sorted(QStringLiteral("change"), QStringLiteral("services")));
        model.cards.insert(bluetooth, {{QStringLiteral("title"), QStringLiteral("Start Bluetooth again")},
                                       {QStringLiteral("what"), QStringLiteral("the Bluetooth service starts again")},
                                       {QStringLiteral("why"), QStringLiteral("it failed at startup")},
                                       {QStringLiteral("command"), QStringLiteral("systemctl restart bluetooth.service")},
                                       {QStringLiteral("admin"), true},
                                       {QStringLiteral("undo"), QStringLiteral("systemctl stop bluetooth.service")},
                                       {QStringLiteral("check"), QString()}});
        run = ask(desk, bluetooth);
        QVERIFY(run.handOff);
        QCOMPARE(tools.proposals.size(), 1);
    }

    // "Undo that", "it worked yesterday": the What changed card, whether the
    // model sorted the ask as a question or found nothing; a skill that makes
    // the change asked for still comes first.
    void lookingBackOpensWhatChanged()
    {
        PretendModel model;
        PretendTools tools;
        tools.seen = readDocument(QStringLiteral(FIXTURES_DIR "/desk/seen.json")).object();
        Model client(model.url(), {});
        FrontDesk desk(&client, &tools);
        desk.setContext(context());
        const QStringList back{QStringLiteral("undo that"), QStringLiteral("Can you put it back how it was?"),
                               QStringLiteral("My wifi was working yesterday and now it isn't"),
                               QStringLiteral("Why did the sound stop working?"),
                               QStringLiteral("Something changed and the screen looks wrong")};
        for (const QString &question : back) {
            model.routes.insert(question, sorted(QStringLiteral("answer"), QStringLiteral("none")));
            const qsizetype before = tools.proposals.size();
            const Run run = ask(desk, question);
            QVERIFY2(tools.proposals.size() == before + 1, qPrintable(question + QLatin1String(": ") + run.routes.join(QLatin1Char(','))));
            QCOMPARE(tools.proposals.last().value(QStringLiteral("card")).toString(), QStringLiteral("changes"));
            QVERIFY(run.said.contains(QLatin1String("What changed")));
        }

        // Undoing an update is going back to a restore point.
        const QString update = QStringLiteral("Undo the update from this morning");
        model.routes.insert(update, sorted(QStringLiteral("change"), QStringLiteral("none")));
        ask(desk, update);
        QCOMPARE(tools.proposals.last().value(QStringLiteral("card")).toString(), QStringLiteral("restore"));

        // Not looking back: an everyday question is answered.
        const QString how = QStringLiteral("How do I change the wallpaper?");
        model.routes.insert(how, sorted(QStringLiteral("answer"), QStringLiteral("none")));
        const qsizetype before = tools.proposals.size();
        ask(desk, how);
        QCOMPARE(tools.proposals.size(), before);
    }

    void reportsAreTheDesksOwnWords()
    {
        PretendModel model;
        PretendTools tools;
        Model client(model.url(), {});
        FrontDesk desk(&client, &tools);
        desk.setContext(context());
        QSignalSpy said(&desk, &FrontDesk::said);
        QSignalSpy finished(&desk, &FrontDesk::finished);
        desk.report({{QStringLiteral("kind"), QStringLiteral("change")}, {QStringLiteral("ok"), true},
                     {QStringLiteral("title"), QStringLiteral("Set the fans to Quiet")}});
        QCOMPARE(finished.size(), 1);
        QVERIFY(said.first().first().toString().startsWith(QLatin1String("Done: Set the fans to Quiet.")));
        QVERIFY(model.asked.isEmpty());
        desk.report({{QStringLiteral("kind"), QStringLiteral("change")}, {QStringLiteral("ok"), false}});
        QCOMPARE(finished.size(), 2);
        QVERIFY(finished.last().first().toBool());
        // An upkeep card that finished is told in the desk's words too, never
        // left without an answer.
        desk.report({{QStringLiteral("kind"), QStringLiteral("chore")}, {QStringLiteral("ok"), true},
                     {QStringLiteral("title"), QStringLiteral("Updated the computer")}});
        QCOMPARE(finished.size(), 3);
        QVERIFY(said.last().first().toString().contains(QLatin1String("Updated the computer")));
    }

    // Every kind Split Rock labels its reports with is read as a report, not
    // as a question with nothing in it.
    void everyReportKindIsAReport()
    {
        for (const char *kind : {"report", "change", "card", "update", "chore", "refused"})
            QVERIFY2(isReport(QString::fromLatin1(kind)), kind);
        for (const char *kind : {"question", "framing", "memory", "aside", ""})
            QVERIFY2(!isReport(QString::fromLatin1(kind)), kind);
    }
};

QTEST_GUILESS_MAIN(TestDesk)
#include "test_desk.moc"
