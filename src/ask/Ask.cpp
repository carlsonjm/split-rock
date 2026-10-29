// SPDX-License-Identifier: GPL-2.0-or-later
#include "ask/Ask.h"

#include "ask/LocalModel.h"
#include "desk/Gate.h"
#include "desk/Model.h"

#include <QFile>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>

namespace splitrock::ask {

namespace {

// The Helpers the desk may name, by the ids Core opens them with, each with
// what it is for, in the words the model sorts by.
const QList<std::pair<QString, QString>> &upkeep()
{
    static const QList<std::pair<QString, QString>> cards{
        {QStringLiteral("update"), QStringLiteral("update the whole computer, or check whether an update worked")},
        {QStringLiteral("space"), QStringLiteral("free up space when the disk is full or nearly full")},
        {QStringLiteral("tidy"), QStringLiteral("tidy up the Downloads folder: old downloads and leftover copies go "
                                                "to the Trash")},
        {QStringLiteral("install"), QStringLiteral("install an app or find one for a job, or remove (uninstall) an app")},
        {QStringLiteral("restore"), QStringLiteral("go back to a restore point, as things were before")},
        {QStringLiteral("health"), QStringLiteral("a health check, when the computer is slow, hot or something seems wrong")},
        {QStringLiteral("community"), QStringLiteral("update apps from the AUR, the community's own apps")},
        {QStringLiteral("changes"), QStringLiteral("what changed on this computer lately, with Undo: for \"undo that\", "
                                                   "\"put it back\", \"something changed\", \"it worked yesterday\" or "
                                                   "\"why did this stop working?\"")},
    };
    return cards;
}

// Where things are, in the names the person sees, so an answer never sends
// them to a menu that isn't there.
// Where things are, one place for each job, as lines for the jobs an ask
// is about: a small model given every line borrows from the wrong one.
QString places(const QString &question)
{
    struct Place {
        // The words of an ask the line is for; empty for every ask.
        const char *about;
        QString line;
        // Words that mean the ask is about something else, as a loud fan
        // isn't about the sound.
        const char *unless = "";
    };
    const bool shuffle = !QStandardPaths::findExecutable(QStringLiteral("shuffle-settings")).isEmpty();
    QList<Place> list;
    if (shuffle) {
        list = {
            {"", QStringLiteral("Apps and files: Search, opened with a tap on the dot at the bottom of the screen or the "
                                "Meta key (the Windows key); type the name. Apps and Files are in it; Files is like File "
                                "Explorer, with Downloads, Documents and Pictures. The dock along the bottom holds apps; "
                                "there is no taskbar or Start menu.")},
            {"", QStringLiteral("Shuffle's own Settings: Meta+I. KDE's System Settings: type its name in Search.")},
            {"wi-?fi|internet|network|hotspot|online|connect",
             QStringLiteral("Wi-Fi: Control Center, opened with a tap on the network icon at the top right. Tap a "
                            "network's name in its list to join. There is no Add network button.")},
            {"hotspot|tether|phone.{0,20}(internet|data)",
             QStringLiteral("A phone's hotspot shows in Control Center's list by name once it's switched on on the "
                            "phone; tap its name to join.")},
            {"bluetooth|headphone|earbud|airpod|mouse|pair|speaker|keyboard",
             QStringLiteral("Bluetooth, headphones, a mouse, AirPods: Control Center's Bluetooth page, with Add new "
                            "device. Control Center opens with a tap on the network icon at the top right.")},
            {"sound|volume|loud|quiet|speaker|headphone|mute|audio|hear",
             QStringLiteral("Volume: the volume slider in Control Center, opened with a tap on the network icon at the "
                            "top right. Control Center has no other sound page; the output device is chosen on the "
                            "Sound page of System Settings."),
             "\\bfans?\\b|noisy|hot\\b"},
            {"bright|dim|dark|screen",
             QStringLiteral("Brightness: the brightness slider in Control Center, opened with a tap on the network icon "
                            "at the top right.")},
        };
    } else {
        list = {
            {"", QStringLiteral("Apps, files and settings: the application launcher at the bottom left, by typing their "
                                "name. Files are in Dolphin, like File Explorer.")},
            {"wi-?fi|internet|network|hotspot|bluetooth|headphone|airpod|mouse|sound|volume|bright|dim",
             QStringLiteral("Wi-Fi, Bluetooth, volume and brightness: the icons at the bottom right.")},
        };
    }
    list += {
        {"fan|noisy|loud|hot|power|battery|quiet",
         QStringLiteral("Fans and power: the fans follow the power profile, which no settings page shows. A fan "
                        "gets loud when the computer works hard, so closing apps that aren't in use quiets it.")},
        {"print|scanner",
         QStringLiteral("Printers: the Printers page of System Settings, with Add Printer.")},
        {"monitor|display|screen|text|font|small|big|scale|night",
         QStringLiteral("Displays, a second monitor, text size and night light: the Display & Monitor page of System "
                        "Settings.")},
        {"password|account|user|picture|photo of me|profile",
         QStringLiteral("Passwords and the account picture: the Users page of System Settings.")},
        {"\\bstart|sign.?in|log.?in|boot|autostart",
         QStringLiteral("Apps that start at sign-in: the Autostart page of System Settings.")},
        {"updat|up to date|upgrade",
         QStringLiteral("Updates: the Check for updates Helper, opened by typing its name in Search, which updates everything at once. Neither Settings "
                        "has an Updates page or section, and Discover isn't used for them.")},
        {"space|full|disk|storage|drive|room",
         QStringLiteral("Disk space: the Free up space Helper, opened by typing its name in Search, shows what "
                        "fills the disk. No settings page shows it.")},
        {"download",
         QStringLiteral("Downloads: the Downloads folder in Files. Tidy up Downloads clears old downloads.")},
        {"install|uninstall|remove|app|program|zoom|chrome|game|software",
         QStringLiteral("Installing or removing an app: the Install an app Helper, told the app's name. It does "
                        "both, so the person needs no store or app list.")},
        {"frozen|freez|not responding|stuck|hang|crash",
         QStringLiteral("A frozen app: its window's close button, then Close when asked. There is no shortcut for it.")},
        {"pdf",
         QStringLiteral("PDFs: double-click one in Files, and it opens in the PDF reader already on this "
                        "computer.")},
        {"word|excel|powerpoint|office|\\.docx?\\b|spreadsheet|windows program|\\.exe",
         QStringLiteral("Word, Excel, Office and other Windows programs don't run here; their files open in ONLYOFFICE "
                        "or LibreOffice, which Install an app finds. Word, Excel and PowerPoint themselves also work in the web "
                        "browser, at office.com, with a Microsoft account.")},
        {"photo|picture|back ?up|backup|copy",
         QStringLiteral("Photos: the Pictures folder in Files. A backup is a copy somewhere else: copy the folder "
                        "to a USB drive in Files, or upload it to a cloud service in the web browser. A copy on this "
                        "computer's own drive isn't a backup.")},
        {"slow|sluggish|lag",
         QStringLiteral("A slow computer: close apps that aren't in use with their windows' close buttons, and run "
                        "the Health check Helper, which looks for what slows it.")},
        {"slow|restart|reboot|wrong|problem|broken|stopped|keeps|crash|error|help",
         QStringLiteral("A problem that stays: the Health check and What changed Helpers, each opened by typing its "
                        "name in Search, and Codex once signed in under Ask Your Computer in Settings. Helpers aren't "
                        "in System Settings. There is no support line or helpdesk to contact.")},
    };
    const QString asked = question.toLower();
    QStringList lines;
    for (const Place &place : list) {
        const QString about = QString::fromLatin1(place.about);
        const QString unless = QString::fromLatin1(place.unless);
        if ((about.isEmpty() || QRegularExpression(about).match(asked).hasMatch())
            && (unless.isEmpty() || !QRegularExpression(unless).match(asked).hasMatch()))
            lines.append(QStringLiteral("- ") + place.line);
    }
    return QStringLiteral("%1Where things are, for this question:\n%2\nName no keyboard shortcut, menu, page, section "
                          "or button that isn't written here, and nothing inside a Helper.")
        .arg(shuffle ? QStringLiteral("Shuffle is this computer's desktop, on KDE Plasma. ") : QString(),
             lines.join(QLatin1Char('\n')));
}

// What this computer runs, from os-release, and where things are.
QString facts(const QString &question)
{
    QFile file(QStringLiteral("/etc/os-release"));
    QString name, like;
    if (file.open(QIODevice::ReadOnly)) {
        for (const QByteArray &line : file.readAll().split('\n')) {
            const QString text = QString::fromUtf8(line);
            const auto value = [&text] { return text.section(QLatin1Char('='), 1).remove(QLatin1Char('"')); };
            if (text.startsWith(QLatin1String("PRETTY_NAME=")))
                name = value();
            else if (text.startsWith(QLatin1String("ID_LIKE=")))
                like = value();
        }
    }
    QString words = name.isEmpty() ? QStringLiteral("This computer runs Linux") : QStringLiteral("This computer runs %1").arg(name);
    if (like.contains(QLatin1String("arch")) || name.contains(QLatin1String("Arch")))
        words += QStringLiteral(", which is based on Arch Linux, with KDE Plasma 6. Ubuntu's apt does not exist here.");
    else
        words += QStringLiteral(", with KDE Plasma 6.");
    words += QLatin1Char('\n') + places(question);
    return words;
}

} // namespace

Ask::Ask(LocalModel *model, const Setup &setup, QObject *parent)
    : QObject(parent)
    , m_model(model)
    , m_setup(setup)
{
    m_shelf.load(setup.skillRoots);
    for (const QString &problem : m_shelf.problems())
        qWarning().noquote() << QStringLiteral("split-rock-ask: ") + problem;
    connect(m_model, &LocalModel::changed, this, [this] {
        Q_EMIT modelChanged(this->model());
        // A question that waited for the download is answered once it's done.
        if (m_current != 0 && m_model->isReady() && !m_engine.isUp() && !m_engine.isStarting())
            begin();
    });
    connect(&m_engine, &Engine::up, this, &Ask::engineUp);
    connect(&m_engine, &Engine::down, this, [this](const QString &why) {
        // The client goes first, so no reply of its can reach the desk.
        m_client.reset();
        m_desk.reset();
        if (m_current != 0)
            finish({{QStringLiteral("problem"), why},
                    {QStringLiteral("text"), QStringLiteral("Ask couldn't start the model on this computer. %1").arg(why)}});
    });
}

Ask::~Ask() = default;

bool Ask::enabled() const
{
    return QSettings(m_setup.settings, QSettings::IniFormat).value(QStringLiteral("Ask/Enabled"), true).toBool();
}

bool Ask::handsToCodex() const
{
    return QSettings(m_setup.settings, QSettings::IniFormat).value(QStringLiteral("Ask/HandToCodex"), false).toBool();
}

bool Ask::codexOnly() const
{
    return QSettings(m_setup.settings, QSettings::IniFormat).value(QStringLiteral("Ask/CodexOnly"), false).toBool();
}

void Ask::setCodexOnly(bool only)
{
    QSettings settings(m_setup.settings, QSettings::IniFormat);
    settings.setValue(QStringLiteral("Ask/CodexOnly"), only);
    settings.sync();
}

desk::Context Ask::context() const
{
    desk::Context context;
    context.skills = m_shelf.skills();
    context.cards = upkeep();
    context.facts = facts(m_question);
    // Codex takes a bigger change once it is signed in; until then the
    // answer says where to sign in.
    if (m_setup.codex.unavailable.isEmpty() && m_setup.codex.signedIn())
        context.handOffTo = m_setup.codex.name;
    return context;
}

QJsonObject Ask::framing() const
{
    desk::Model model{QUrl(), QString()};
    desk::FrontDesk desk(&model, nullptr);
    desk.setContext(context());
    QJsonArray cards;
    for (const auto &[name, what] : upkeep())
        cards.append(QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("what"), what}});
    return {{QStringLiteral("words"), desk.sortingWords()},
            {QStringLiteral("shape"), desk.sortingShape()},
            {QStringLiteral("cards"), cards}};
}

QVariantMap Ask::model() const
{
    return {{QStringLiteral("state"), m_model->state()},
            {QStringLiteral("progress"), m_model->progress()},
            {QStringLiteral("size"), m_model->size()},
            {QStringLiteral("name"), m_model->name()},
            {QStringLiteral("credit"), m_model->credit()},
            {QStringLiteral("problem"), m_model->problem()},
            {QStringLiteral("engine"), hasEngine()}};
}

uint Ask::ask(const QString &question)
{
    if (m_current != 0)
        cancel(m_current);
    const uint id = ++m_next;
    m_current = id;
    m_question = question.trimmed().left(2000);
    m_said.clear();
    m_routed.clear();
    m_helpers.clear();
    m_query.clear();
    m_doIt = {};
    // Answered once the caller has the id.
    QMetaObject::invokeMethod(this, [this, id] {
        if (m_current != id)
            return;
        if (!enabled()) {
            finish({{QStringLiteral("off"), true},
                    {QStringLiteral("text"), QStringLiteral("Ask is switched off in Settings.")}});
            return;
        }
        if (m_question.isEmpty()) {
            finish({});
            return;
        }
        // The safety check needs no model, so it answers before any download,
        // as does an ask with nothing in it to answer.
        if (const std::optional<QString> kind = desk::Gate::check(m_question)) {
            m_routed = QStringLiteral("gate:") + *kind;
            m_said = desk::Gate::answer(*kind);
            Q_EMIT said(id, m_said);
            finish({});
            return;
        }
        if (const std::optional<QString> words = desk::unclear(m_question)) {
            m_routed = QStringLiteral("unclear");
            m_said = *words;
            Q_EMIT said(id, m_said);
            finish({});
            return;
        }
        begin();
    }, Qt::QueuedConnection);
    return id;
}

void Ask::begin()
{
    // Codex only, where Codex is installed: nothing is asked of the model here. The question waits for
    // the person's tap on Ask Codex; the Helper the words name stays listed.
    if (codexOnly() && m_setup.codex.unavailable.isEmpty()) {
        const QString helper = desk::helperFor(m_question, upkeep());
        if (!helper.isEmpty() && !m_helpers.contains(helper))
            m_helpers.append(helper);
        finish({{QStringLiteral("codexOnly"), true},
                {QStringLiteral("text"), QStringLiteral("Ask is set to Codex only. Press Enter or tap Ask Codex to send "
                                                        "this question to Codex, online, on your ChatGPT plan. It "
                                                        "answers in its own window.")}});
        return;
    }
    if (m_setup.server.isEmpty()) {
        if (!m_model->isReady()) {
            m_model->survey();
            if (!m_model->isReady()) {
                // A download under way finishes first; otherwise Ask says
                // once what it needs.
                // The Helper the words name stays listed meanwhile.
                const QString helper = desk::helperFor(m_question, upkeep());
                if (!helper.isEmpty() && !m_helpers.contains(helper))
                    m_helpers.append(helper);
                if (m_model->state() == QLatin1String("downloading") || m_model->state() == QLatin1String("verifying"))
                    return;
                const QString name = m_model->name().isEmpty() ? QStringLiteral("its model") : m_model->name();
                // Without llama-server the model could never run, so the
                // download isn't offered until it is installed.
                if (!hasEngine()) {
                    finish({{QStringLiteral("needsModel"), true},
                            {QStringLiteral("needsEngine"), true},
                            {QStringLiteral("text"),
                             QStringLiteral("llama.cpp is the free engine that runs Ask's AI model on this computer, "
                                            "and it isn't installed. Install it first; then Ask offers the model "
                                            "itself, %1, a one-time download of %2.")
                                 .arg(name, m_model->size())}});
                    return;
                }
                finish({{QStringLiteral("needsModel"), true},
                        {QStringLiteral("text"), QStringLiteral("Ask can answer with %1, an open AI model that runs on "
                                                                "this computer. It's a one-time download of %2. Once "
                                                                "it's here, Ask works offline and nothing you ask "
                                                                "leaves this computer.")
                                                     .arg(name, m_model->size())}});
                return;
            }
        }
    }
    if (m_desk) {
        m_desk->forget();
        m_desk->setContext(context());
        m_desk->ask(m_question);
        return;
    }
    if (m_engine.isStarting())
        return;
    if (!m_setup.server.isEmpty())
        m_engine.use(QUrl(m_setup.server));
    else
        m_engine.start(m_setup.engine, m_model->path(), m_model->layers());
}

void Ask::engineUp()
{
    m_client = std::make_unique<desk::Model>(m_engine.url(), m_engine.key());
    m_desk = std::make_unique<desk::FrontDesk>(m_client.get(), static_cast<desk::Tools *>(this));
    m_desk->setContext(context());
    connect(m_desk.get(), &desk::FrontDesk::said, this, [this](const QString &text) {
        if (m_current == 0)
            return;
        m_said += text;
        Q_EMIT said(m_current, text);
    });
    connect(m_desk.get(), &desk::FrontDesk::routed, this, [this](const QString &how) {
        m_routed = how;
        qInfo().noquote() << QStringLiteral("split-rock-ask: routed: ") + how;
    });
    connect(m_desk.get(), &desk::FrontDesk::finished, this, [this](bool handOff) {
        if (!m_cancelling)
            finish({{QStringLiteral("handOff"), handOff}});
    });
    // The model fills its memory with the start of each kind of request, so
    // the first question is answered sooner.
    if (m_current != 0)
        begin();
    else
        m_desk->warm();
}

void Ask::cancel(uint id)
{
    if (id == 0 || id != m_current)
        return;
    if (m_desk && m_desk->busy()) {
        m_cancelling = true;
        m_desk->cancel();
        m_cancelling = false;
    }
    finish({{QStringLiteral("cancelled"), true}});
}

void Ask::finish(QVariantMap result)
{
    const uint id = std::exchange(m_current, 0);
    if (id == 0)
        return;
    result.insert(QStringLiteral("who"), QStringLiteral("computer"));
    if (!result.contains(QStringLiteral("text")))
        result.insert(QStringLiteral("text"), m_said.trimmed());
    result.insert(QStringLiteral("routed"), m_routed);
    result.insert(QStringLiteral("helpers"), m_helpers);
    result.insert(QStringLiteral("query"), m_query);
    result.insert(QStringLiteral("doIt"),
                  m_doIt.isEmpty() ? QString() : QString::fromUtf8(QJsonDocument(m_doIt).toJson(QJsonDocument::Compact)));
    if (!result.contains(QStringLiteral("handOff")))
        result.insert(QStringLiteral("handOff"), false);
    Q_EMIT answered(id, result);
}

void Ask::openSkill(const QString &id, const std::function<void(const QString &)> &done)
{
    m_shelf.open(id, this, done);
}

bool Ask::propose(const QJsonObject &proposal)
{
    // A Helper, by name, goes under the answer; a Do it card waits there for
    // the person's tap. Nothing runs from here.
    const QString card = proposal.value(QStringLiteral("card")).toString();
    if (!card.isEmpty()) {
        if (!m_helpers.contains(card))
            m_helpers.append(card);
        m_query = proposal.value(QStringLiteral("query")).toString();
        return true;
    }
    if (proposal.value(QStringLiteral("command")).toString().trimmed().isEmpty())
        return false;
    m_doIt = proposal;
    return true;
}

} // namespace splitrock::ask
