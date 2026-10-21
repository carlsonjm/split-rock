// SPDX-License-Identifier: GPL-2.0-or-later
#include "ask/Ask.h"

#include "ask/LocalModel.h"
#include "desk/Gate.h"
#include "desk/Model.h"

#include <QFile>
#include <QJsonDocument>
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
QString places()
{
    // KDE's System Settings pages, as Plasma 6 names them.
    const QString pages = QStringLiteral(
        "System Settings has pages named Wi-Fi & Networking, Bluetooth, Sound, Display & Monitor (the screen's size, "
        "text scale and night light), Printers, Users (passwords and the account picture), Power Management, Mouse & "
        "Touchpad, Keyboard, Autostart and Default Applications. No page shows what fills the disk: the Free up space "
        "Helper does. Updates come from the Check for updates Helper, or Discover.");
    if (!QStandardPaths::findExecutable(QStringLiteral("shuffle-settings")).isEmpty())
        return QStringLiteral(
            "Shuffle is this computer's desktop, on KDE Plasma. Use its names. Search opens with a tap on the dot at the "
            "bottom of the screen, or the Meta key (the Windows key); it finds apps, files and settings by name, and "
            "Apps and Files are in it (Files is like File Explorer, with Downloads, Documents and Pictures). Wi-Fi, "
            "Bluetooth, sound and brightness are in Control Center: tap the network icon at the top right of the "
            "screen; its Bluetooth page has Add new device, for a mouse or headphones. Shuffle's own Settings open "
            "with Meta+I. KDE's System Settings opens from Search by its name. The Helpers are listed in Search, "
            "under the answer. ") + pages;
    return QStringLiteral(
        "Apps, files and settings open from the application launcher at the bottom left of the screen, by typing "
        "their name. Wi-Fi, Bluetooth, sound and brightness are in the icons at the bottom right. Files are in "
        "Dolphin, like File Explorer. ") + pages;
}

// What this computer runs, from os-release, and where things are.
QString facts()
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
        words += QStringLiteral(", which is based on Arch Linux, with KDE Plasma 6. Software comes from Discover, "
                                "Plasma's app store; Ubuntu's apt does not exist here.");
    else
        words += QStringLiteral(", with KDE Plasma 6. Software comes from Discover, Plasma's app store.");
    words += QLatin1Char('\n') + places();
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

desk::Context Ask::context() const
{
    desk::Context context;
    context.skills = m_shelf.skills();
    context.cards = upkeep();
    context.facts = facts();
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
            {QStringLiteral("problem"), m_model->problem()}};
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
                finish({{QStringLiteral("needsModel"), true},
                        {QStringLiteral("text"), QStringLiteral("To answer on this computer, Ask needs a one-time "
                                                                "download of %1. Nothing you ask leaves the computer.")
                                                     .arg(m_model->size())}});
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
