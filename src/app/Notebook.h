// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QDate>
#include <QList>
#include <QString>
#include <QStringList>

namespace splitrock {

// What the computer says it is, read and never written.
struct DeviceFacts {
    QString maker;
    QString model;
    QString version;
    QString board;
    QString firmware;
    QString firmwareDate;
    QString processor;
    QString memory;
    QString system;
    QString kernel;

    // Read from the computer, under root ("/" but for tests).
    static DeviceFacts read(const QString &root = QStringLiteral("/"));
};

// The notebook: a plain folder of the person's, where what is worth
// remembering is written (docs/MEMORY.md). Split Rock writes the record of this
// computer and the device's page itself, and keeps answers when asked; every
// write is checked for what is never written down.
class Notebook
{
public:
    explicit Notebook(QString folder);

    // ~/Documents/Notebook, until setup lets the person choose (Milestone 3).
    static QString defaultFolder();

    QString folder() const { return m_folder; }
    // The path is inside the notebook, links resolved.
    bool contains(const QString &path) const;

    // Makes the folder and what Split Rock writes at setup, where missing:
    // ASSISTANT.md, This computer.md with the hardware, and Device/DEVICE.md.
    // Nothing already there is overwritten.
    bool prepare(const DeviceFacts &facts, QString *error = nullptr);

    struct Kept {
        QString title;
        QString topic;
        QString path;
        QDate date;
    };

    // Keeps an answer as a note under Notes/, in a topic folder, with a plain
    // title, the date and the question. Returns its path; empty, with why in
    // error, when it may not be written.
    QString keep(const QString &question, const QString &answer, QString *error = nullptr);
    // Every kept note, newest first.
    QList<Kept> kept() const;
    // Forget: moves a note to the desktop's trash, where it can be restored.
    bool forget(const QString &path, QString *error = nullptr);

    // What the assistant is given when a conversation starts: the page about
    // the person, the top of the record of this computer, the device's page and
    // the kept notes' titles.
    QString memory() const;
    struct Fixed {
        QString when;
        QString text;
    };
    // The fixed problems in This computer.md, newest first.
    QList<Fixed> fixedProblems() const;
    struct DeviceFix {
        QString path;
        QString title;
        QString why;
        QStringList steps;
    };
    // The device's own problems with a known fix, one to a page beside
    // DEVICE.md, by title.
    QList<DeviceFix> deviceFixes() const;
    // The device's problems met and not yet fixed: the lines under "Still
    // open" in DEVICE.md.
    QStringList deviceStillOpen() const;
    // A problem still open, solved: what fixed it, in the person's words,
    // becomes the device's page for that problem, and its line leaves Still
    // open. False, with why in error, when it may not be written.
    bool solveStillOpen(const QString &problem, const QString &fix, QString *error = nullptr);
    // A solved problem came back: it returns to Still open, with what fixed it
    // before, and its page goes to the trash, so the guide no longer offers a
    // fix that stopped holding.
    bool reopenFix(const QString &path, QString *error = nullptr);

    // "Remember that?", answered yes: the sentence goes into About me.md,
    // dated. False, with why in error, when it may not be written.
    bool rememberAboutPerson(const QString &sentence, QString *error = nullptr);
    // "That fixed it": the problem, in the person's words, and what fixed it
    // go at the top of the fixed problems in This computer.md.
    bool recordFixed(const QString &problem, const QString &fix, QString *error = nullptr);
    // A change Split Rock made goes at the top of the changes in This
    // computer.md: the date, what it was, why, how to undo it and how it went.
    bool recordChange(const QString &id, const QString &title, const QString &why, const QString &undo,
                      const QString &status, QString *error = nullptr);
    // How a recorded change went since, such as "Undone 2026-10-05".
    bool markChange(const QString &id, const QString &status, QString *error = nullptr);

    // The topic folder an answer is kept in.
    static QString topicFor(const QString &question, const QString &answer);

private:
    QString read(const QString &relative) const;
    bool writeNew(const QString &relative, const QString &text, QString *error);
    // Replaces a file of the notebook's with text, checked first.
    bool rewrite(const QString &relative, const QString &text, QString *error);
    // Puts entry at the top of a section of This computer.md, newest first.
    bool recordUnder(const QString &heading, const QString &entry, QString *error);

    QString m_folder;
};

} // namespace splitrock
