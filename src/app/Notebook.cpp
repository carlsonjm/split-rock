// SPDX-License-Identifier: GPL-2.0-or-later
#include "app/Notebook.h"

#include "app/NeverWritten.h"
#include "app/Product.h"
#include "guide/GuideLibrary.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSysInfo>

#include <algorithm>

namespace splitrock {

namespace {

QString readLine(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()).trimmed() : QString();
}

// What a maker leaves when it fills in nothing.
QString filled(const QString &value)
{
    static const QStringList blanks{QStringLiteral("System Product Name"), QStringLiteral("To be filled by O.E.M."),
                                    QStringLiteral("Default string"), QStringLiteral("Not Specified"),
                                    QStringLiteral("System manufacturer"), QStringLiteral("System Version"),
                                    QStringLiteral("None")};
    return blanks.contains(value, Qt::CaseInsensitive) ? QString() : value;
}

QString yamlQuoted(QString text)
{
    text.replace(QLatin1Char('\\'), QStringLiteral("\\\\")).replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QLatin1Char('"') + text.simplified() + QLatin1Char('"');
}

QString headerValue(const QString &text, const QString &key)
{
    const QRegularExpression line(QStringLiteral("^%1:\\s*(.*)$").arg(QRegularExpression::escape(key)),
                                  QRegularExpression::MultilineOption);
    QString value = line.match(text).captured(1).trimmed();
    if (value.size() >= 2 && value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')))
        value = value.mid(1, value.size() - 2).replace(QStringLiteral("\\\""), QStringLiteral("\""));
    return value;
}

QString clip(const QString &text, int lines)
{
    const QStringList all = text.split(QLatin1Char('\n'));
    return all.mid(0, lines).join(QLatin1Char('\n')).trimmed();
}

const QString AssistantRules = QStringLiteral(R"(# How to work in this notebook

This folder is the person's notebook. %1 wrote this page at setup; it holds the
rules you follow here.

- Write only inside this folder. Nothing else on the computer is yours to
  change; a change to the computer goes through %1's card.
- Never write down passwords, card numbers, bank details or codes sent by text
  or email, even when asked. %1 refuses them too.
- `This computer.md` is %1's own record of this computer and its undo record.
  Read it; do not change it.
- `About me.md` is about the person, and %1 writes it. When something worth
  remembering about the person comes up, such as how they like to be helped,
  the apps they use or who helps them, end your answer with a line of its own:
  "Remember that? " and one short sentence to remember. %1 asks the person and
  writes it only after a yes.
- `Device/` is this computer's own manual. When you learn something about this
  device, such as a problem and what fixed it, add an entry beside
  `Device/DEVICE.md` in the guide's format: a header with id, title, also,
  applies, checked and format, then What you see, Why and What to do. A problem
  met and not yet fixed goes on a line of its own under "Still open" in
  `Device/DEVICE.md`, until a fix is proven. Add the maker's documentation you
  find under "The maker's documentation" in `Device/DEVICE.md`.
- `Notes/` holds answers the person kept. Change one only when asked.
- When something here helps answer a question, say so: "Last time, the printer
  needed its driver reinstalled."
)");

} // namespace

DeviceFacts DeviceFacts::read(const QString &root)
{
    const QDir base(root);
    const auto dmi = [&base](const char *name) {
        return filled(readLine(base.filePath(QStringLiteral("sys/class/dmi/id/") + QLatin1String(name))));
    };
    DeviceFacts facts;
    facts.maker = dmi("sys_vendor");
    facts.model = dmi("product_name");
    facts.version = dmi("product_version");
    const QString boardMaker = dmi("board_vendor");
    const QString boardName = dmi("board_name");
    facts.board = (boardMaker + QLatin1Char(' ') + boardName).trimmed();
    facts.firmware = (dmi("bios_vendor") + QLatin1Char(' ') + dmi("bios_version")).trimmed();
    facts.firmwareDate = dmi("bios_date");

    QFile cpu(base.filePath(QStringLiteral("proc/cpuinfo")));
    if (cpu.open(QIODevice::ReadOnly)) {
        static const QRegularExpression model(QStringLiteral("^model name\\s*:\\s*(.+)$"), QRegularExpression::MultilineOption);
        facts.processor = model.match(QString::fromUtf8(cpu.readAll())).captured(1).trimmed();
    }
    QFile memory(base.filePath(QStringLiteral("proc/meminfo")));
    if (memory.open(QIODevice::ReadOnly)) {
        static const QRegularExpression total(QStringLiteral("^MemTotal:\\s*(\\d+)\\s*kB"), QRegularExpression::MultilineOption);
        const qint64 kilobytes = total.match(QString::fromUtf8(memory.readAll())).captured(1).toLongLong();
        if (kilobytes > 0)
            facts.memory = QStringLiteral("%1 GB").arg(qRound(kilobytes / 1024.0 / 1024.0));
    }
    if (root == QLatin1String("/")) {
        facts.system = QSysInfo::prettyProductName();
        facts.kernel = QSysInfo::kernelVersion();
    } else {
        facts.system = readLine(base.filePath(QStringLiteral("etc/system-name")));
        facts.kernel = readLine(base.filePath(QStringLiteral("proc/sys/kernel/osrelease")));
    }
    return facts;
}

Notebook::Notebook(QString folder)
    : m_folder(QDir::cleanPath(QDir(folder).absolutePath()))
{
}

QString Notebook::defaultFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/Notebook");
}

bool Notebook::contains(const QString &path) const
{
    const QString root = QFileInfo(m_folder).canonicalFilePath();
    QFileInfo info(QDir::cleanPath(QDir(m_folder).absoluteFilePath(path)));
    // A file still to be written is judged by its folder.
    while (!info.exists() && info.absolutePath() != info.absoluteFilePath())
        info = QFileInfo(info.absolutePath());
    const QString real = info.canonicalFilePath();
    return !root.isEmpty() && (real == root || real.startsWith(root + QLatin1Char('/')));
}

QString Notebook::read(const QString &relative) const
{
    QFile file(QDir(m_folder).filePath(relative));
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

bool Notebook::writeNew(const QString &relative, const QString &text, QString *error)
{
    const QString path = QDir(m_folder).filePath(relative);
    if (QFileInfo::exists(path))
        return true;
    const QString refused = neverWrittenReason(text);
    if (!refused.isEmpty()) {
        if (error)
            *error = refused;
        return false;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    if (!contains(path)) {
        if (error)
            *error = QStringLiteral("%1 writes only inside the notebook.").arg(productName());
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(text.toUtf8()) < 0 || !file.commit()) {
        if (error)
            *error = QStringLiteral("The notebook could not be written: %1").arg(file.errorString());
        return false;
    }
    return true;
}

bool Notebook::prepare(const DeviceFacts &facts, QString *error)
{
    if (!QDir().mkpath(m_folder)) {
        if (error)
            *error = QStringLiteral("The notebook folder could not be made: %1").arg(m_folder);
        return false;
    }
    if (!writeNew(QStringLiteral("ASSISTANT.md"), AssistantRules.arg(productName()), error))
        return false;

    QStringList hardware;
    const auto fact = [&hardware](const QString &label, const QString &value) {
        if (!value.isEmpty())
            hardware.append(QStringLiteral("- %1: %2").arg(label, value));
    };
    fact(QStringLiteral("Maker"), facts.maker);
    fact(QStringLiteral("Model"), facts.model);
    fact(QStringLiteral("Version"), facts.version);
    fact(QStringLiteral("Board"), facts.board);
    fact(QStringLiteral("Firmware"), facts.firmwareDate.isEmpty() ? facts.firmware
                                                                  : QStringLiteral("%1, %2").arg(facts.firmware, facts.firmwareDate));
    fact(QStringLiteral("Processor"), facts.processor);
    fact(QStringLiteral("Memory"), facts.memory);
    fact(QStringLiteral("System"), facts.system);
    fact(QStringLiteral("Kernel"), facts.kernel);
    if (hardware.isEmpty())
        hardware.append(QStringLiteral("- The computer reports nothing about itself."));

    const QString today = QDate::currentDate().toString(Qt::ISODate);
    const QString record = QStringLiteral(
        "# This computer\n\n"
        "%1 keeps this page itself: what this computer is, every change made to it\n"
        "with how to undo it, and every problem fixed. Started %2.\n\n"
        "## Hardware\n\n%3\n\n"
        "## Changes\n\nNone yet.\n\n"
        "## Fixed problems\n\nNone yet.\n")
        .arg(productName(), today, hardware.join(QLatin1Char('\n')));
    if (!writeNew(QStringLiteral("This computer.md"), record, error))
        return false;

    QString id = facts.model.toLower();
    id.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
    id = id.mid(id.startsWith(QLatin1Char('-')) ? 1 : 0);
    if (id.endsWith(QLatin1Char('-')))
        id.chop(1);
    const QString name = facts.model.isEmpty() ? QStringLiteral("This computer") : facts.model;
    const QString device = QStringLiteral(
        "---\n"
        "id: %1\n"
        "name: %2\n"
        "maker: %3\n"
        "matches: %4\n"
        "checked: %5\n"
        "format: 1\n"
        "---\n\n"
        "## How it is recognised\n\n%6\n\n"
        "## What it is\n\n%7\n\n"
        "## What is known\n\n"
        "Nothing yet. Problems met on this computer, and what fixed them, are kept\n"
        "beside this page, one to a file, in the guide's format.\n\n"
        "## Still open\n\nNothing yet.\n\n"
        "## The maker's documentation\n\nNone found yet.\n")
        .arg(id.isEmpty() ? QStringLiteral("this-computer") : id, yamlQuoted(name), yamlQuoted(facts.maker),
             yamlQuoted(facts.model), today,
             facts.model.isEmpty() ? QStringLiteral("The hardware does not report its name.")
                                   : QStringLiteral("By the product name the hardware reports: %1.").arg(facts.model),
             hardware.join(QLatin1Char('\n')));
    return writeNew(QStringLiteral("Device/DEVICE.md"), device, error);
}

QString Notebook::topicFor(const QString &question, const QString &answer)
{
    struct Topic {
        const char *name;
        const char *words;
    };
    static const Topic topics[] = {
        {"Accounts and safety", "scam|virus|infected|phishing|password|account|login|sign in|email|hacked|suspicious"},
        {"Network", "wi-?fi|wifi|internet|network|ethernet|vpn|router|hotspot|connection"},
        {"Bluetooth", "bluetooth|pair|earbuds|airpods"},
        {"Sound", "sound|audio|speaker|volume|microphone|mic|headphone"},
        {"Printing", "print|printer|scan|scanner"},
        {"Display", "screen|display|monitor|brightness|scaling|resolution|bigger|text size|font|night colou?r"},
        {"Battery and power", "battery|power|charg|sleep|suspend|hibernat|shut ?down|restart"},
        {"Keyboard, pen and touch", "keyboard|pen|stylus|touch|touchpad|mouse|trackpad"},
        {"Updates and apps", "update|upgrade|install|software|discover|flatpak|app|program|browser|firefox"},
        {"Files", "file|folder|document|usb|drive|disk|backup|photo"},
        {"Desktop", "panel|desktop|window|workspace|taskbar|wallpaper|widget|search"},
    };
    for (const QString &text : {question, answer}) {
        for (const Topic &topic : topics) {
            const QRegularExpression words(QStringLiteral("\\b(%1)").arg(QLatin1String(topic.words)),
                                           QRegularExpression::CaseInsensitiveOption);
            if (words.match(text).hasMatch())
                return QLatin1String(topic.name);
        }
    }
    return QStringLiteral("Other");
}

QString Notebook::keep(const QString &question, const QString &answer, QString *error)
{
    const QString refused = neverWrittenReason(question + QLatin1Char('\n') + answer);
    if (!refused.isEmpty()) {
        if (error)
            *error = refused;
        return {};
    }
    QString title = question.simplified();
    while (title.endsWith(QLatin1Char('?')) || title.endsWith(QLatin1Char('.')))
        title.chop(1);
    if (title.isEmpty())
        title = QStringLiteral("Kept answer");
    if (title.size() > 70)
        title = title.left(70).section(QLatin1Char(' '), 0, -2);
    title[0] = title.at(0).toUpper();
    QString file = title;
    file.replace(QRegularExpression(QStringLiteral("[/\\\\:*?\"<>|]")), QStringLiteral("-"));

    const QString topic = topicFor(question, answer);
    QString relative = QStringLiteral("Notes/%1/%2.md").arg(topic, file);
    for (int n = 2; QFileInfo::exists(QDir(m_folder).filePath(relative)); ++n)
        relative = QStringLiteral("Notes/%1/%2 %3.md").arg(topic, file).arg(n);

    const QString note = QStringLiteral("---\ntitle: %1\ndate: %2\nquestion: %3\nkept-by: %4\n---\n\n%5\n")
                             .arg(yamlQuoted(title), QDate::currentDate().toString(Qt::ISODate), yamlQuoted(question),
                                  productName(), answer.trimmed());
    if (!writeNew(relative, note, error))
        return {};
    return QDir(m_folder).filePath(relative);
}

QList<Notebook::Kept> Notebook::kept() const
{
    QList<Kept> notes;
    QDirIterator it(QDir(m_folder).filePath(QStringLiteral("Notes")), {QStringLiteral("*.md")}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QString text = QString::fromUtf8(file.read(4096));
        Kept note;
        note.path = path;
        note.title = headerValue(text, QStringLiteral("title"));
        if (note.title.isEmpty())
            note.title = QFileInfo(path).completeBaseName();
        note.topic = QFileInfo(path).dir().dirName();
        note.date = QDate::fromString(headerValue(text, QStringLiteral("date")), Qt::ISODate);
        if (!note.date.isValid())
            note.date = QFileInfo(path).lastModified().date();
        notes.append(note);
    }
    std::sort(notes.begin(), notes.end(), [](const Kept &a, const Kept &b) {
        return a.date != b.date ? a.date > b.date : a.title < b.title;
    });
    return notes;
}

bool Notebook::forget(const QString &path, QString *error)
{
    const QString notes = QDir(m_folder).filePath(QStringLiteral("Notes"));
    const QString canonical = QFileInfo(path).canonicalFilePath();
    const QString notesRoot = QFileInfo(notes).canonicalFilePath();
    if (canonical.isEmpty() || notesRoot.isEmpty() || !canonical.startsWith(notesRoot + QLatin1Char('/'))) {
        if (error)
            *error = QStringLiteral("Only a kept note can be forgotten.");
        return false;
    }
    // The trash lives in the person's data folder, which Qt does not make
    // when it is missing, as on a new account; without it nothing is trashed.
    QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation));
    if (!QFile::moveToTrash(canonical)) {
        if (error)
            *error = QStringLiteral("The note could not be moved to the trash.");
        return false;
    }
    return true;
}

QString Notebook::memory() const
{
    QStringList parts;
    const QString about = read(QStringLiteral("About me.md")).trimmed();
    if (!about.isEmpty())
        parts.append(QStringLiteral("About the person (About me.md):\n%1").arg(clip(about, 60)));
    const QString record = read(QStringLiteral("This computer.md")).trimmed();
    if (!record.isEmpty())
        parts.append(QStringLiteral("This computer (the top of This computer.md):\n%1").arg(clip(record, 50)));
    const QString device = read(QStringLiteral("Device/DEVICE.md")).trimmed();
    if (!device.isEmpty())
        parts.append(QStringLiteral("The device (Device/DEVICE.md):\n%1").arg(clip(device, 60)));
    // The device's own problems and fixes, one to a file beside DEVICE.md.
    QStringList known;
    const QDir deviceFolder(QDir(m_folder).filePath(QStringLiteral("Device")));
    for (const QString &name : deviceFolder.entryList({QStringLiteral("*.md")}, QDir::Files, QDir::Name)) {
        if (name == QLatin1String("DEVICE.md"))
            continue;
        QFile file(deviceFolder.filePath(name));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        QString title = headerValue(QString::fromUtf8(file.read(4096)), QStringLiteral("title"));
        if (title.isEmpty())
            title = QFileInfo(name).completeBaseName();
        known.append(QStringLiteral("- %1 (Device/%2)").arg(title, name));
    }
    if (!known.isEmpty())
        parts.append(QStringLiteral("What is known about this device; open one when the question touches it:\n%1")
                         .arg(known.mid(0, 50).join(QLatin1Char('\n'))));
    QStringList titles;
    for (const Kept &note : kept().mid(0, 50))
        titles.append(QStringLiteral("- %1 (%2)").arg(note.title, QDir(m_folder).relativeFilePath(note.path)));
    if (!titles.isEmpty())
        parts.append(QStringLiteral("Kept notes, newest first; open one only when the question touches it:\n%1")
                         .arg(titles.join(QLatin1Char('\n'))));
    if (parts.isEmpty())
        return {};
    return QStringLiteral("The person's notebook is at %1. Its rules are in ASSISTANT.md there. When something worth "
                          "remembering about the person comes up, end your answer with a line of its own: \"Remember "
                          "that? \" and one short sentence to remember; %2 asks them and writes it only after a yes. "
                          "Do not write About me.md yourself. What the notebook holds now:\n\n%3")
        .arg(m_folder, productName(), parts.join(QStringLiteral("\n\n")));
}

QList<Notebook::DeviceFix> Notebook::deviceFixes() const
{
    QList<DeviceFix> fixes;
    const QDir folder(QDir(m_folder).filePath(QStringLiteral("Device")));
    for (const QString &name : folder.entryList({QStringLiteral("*.md")}, QDir::Files, QDir::Name)) {
        if (name == QLatin1String("DEVICE.md"))
            continue;
        QFile file(folder.filePath(name));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        QString problem;
        const GuideEntry entry = GuideLibrary::parse(file.fileName(), QString::fromUtf8(file.readAll()),
                                                     QStringLiteral("device"), GuideEntry::Kind::Guide, &problem);
        if (!problem.isEmpty() || entry.title.isEmpty())
            continue;
        fixes.append({entry.title, entry.why, entry.steps});
    }
    return fixes;
}

QStringList Notebook::deviceStillOpen() const
{
    const QString page = read(QStringLiteral("Device/DEVICE.md"));
    const qsizetype start = page.indexOf(QLatin1String("\n## Still open"));
    if (start < 0)
        return {};
    QStringList open;
    const QStringList lines = page.mid(start + 1).split(QLatin1Char('\n')).mid(1);
    for (const QString &line : lines) {
        if (line.startsWith(QLatin1String("## ")))
            break;
        if (line.startsWith(QLatin1String("- ")))
            open.append(line.mid(2).trimmed());
        else if (!open.isEmpty() && line.startsWith(QLatin1String("  ")))
            open.last() += QLatin1Char(' ') + line.trimmed();
    }
    return open;
}

QList<Notebook::Fixed> Notebook::fixedProblems() const
{
    const QString record = read(QStringLiteral("This computer.md"));
    const qsizetype start = record.indexOf(QLatin1String("\n## Fixed problems"));
    if (start < 0)
        return {};
    static const QRegularExpression dated(QStringLiteral("^(\\d{1,2} [A-Za-z]+ \\d{4}): (.+)$"));
    QList<Fixed> problems;
    const QStringList lines = record.mid(start + 1).split(QLatin1Char('\n')).mid(1);
    for (const QString &line : lines) {
        if (line.startsWith(QLatin1String("## ")))
            break;
        if (!line.startsWith(QLatin1String("- ")))
            continue;
        const QString entry = line.mid(2).trimmed();
        const QRegularExpressionMatch match = dated.match(entry);
        problems.append(match.hasMatch() ? Fixed{match.captured(1), match.captured(2)} : Fixed{QString(), entry});
    }
    return problems;
}

bool Notebook::rewrite(const QString &relative, const QString &text, QString *error)
{
    const QString refused = neverWrittenReason(text);
    if (!refused.isEmpty()) {
        if (error)
            *error = refused;
        return false;
    }
    const QString path = QDir(m_folder).filePath(relative);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!contains(path) || !file.open(QIODevice::WriteOnly) || file.write(text.toUtf8()) < 0 || !file.commit()) {
        if (error)
            *error = QStringLiteral("The notebook could not be written.");
        return false;
    }
    return true;
}

namespace {
QString today()
{
    return QLocale::c().toString(QDate::currentDate(), QStringLiteral("d MMMM yyyy"));
}
} // namespace

bool Notebook::rememberAboutPerson(const QString &sentence, QString *error)
{
    const QString line = sentence.simplified();
    if (line.isEmpty())
        return false;
    QString page = read(QStringLiteral("About me.md"));
    if (page.trimmed().isEmpty())
        page = QStringLiteral("# About me\n\nWhat %1 remembers about you, each written only after you said yes.\n\n")
                   .arg(productName());
    if (!page.endsWith(QLatin1Char('\n')))
        page += QLatin1Char('\n');
    page += QStringLiteral("- %1 (%2)\n").arg(line, today());
    // Only the new line is judged: what is already there was judged when it
    // was written.
    const QString refused = neverWrittenReason(line);
    if (!refused.isEmpty()) {
        if (error)
            *error = refused;
        return false;
    }
    return rewrite(QStringLiteral("About me.md"), page, error);
}

bool Notebook::recordFixed(const QString &problem, const QString &fix, QString *error)
{
    const QString entry = QStringLiteral("- %1: “%2” %3").arg(today(), problem.simplified(), fix.simplified());
    const QString refused = neverWrittenReason(problem + QLatin1Char('\n') + fix);
    if (!refused.isEmpty()) {
        if (error)
            *error = refused;
        return false;
    }
    QString record = read(QStringLiteral("This computer.md"));
    const QString heading = QStringLiteral("## Fixed problems");
    qsizetype at = record.indexOf(QLatin1Char('\n') + heading);
    if (at < 0) {
        if (!record.endsWith(QLatin1Char('\n')))
            record += QLatin1Char('\n');
        record += QLatin1Char('\n') + heading + QStringLiteral("\n\n");
        at = record.indexOf(QLatin1Char('\n') + heading);
    }
    // Newest first, under the heading; "None yet." goes with the first.
    qsizetype body = at + 1 + heading.size();
    while (body < record.size() && record.at(body) == QLatin1Char('\n'))
        ++body;
    if (record.mid(body).startsWith(QLatin1String("None yet.")))
        record.remove(body, record.indexOf(QLatin1Char('\n'), body) < 0 ? record.size() - body
                                                                        : record.indexOf(QLatin1Char('\n'), body) + 1 - body);
    record.insert(body, entry + QLatin1Char('\n'));
    return rewrite(QStringLiteral("This computer.md"), record, error);
}

} // namespace splitrock
