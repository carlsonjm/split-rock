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
    // The fixed problems in This computer.md, newest first, one line each.
    QStringList fixedProblems() const;

    // The topic folder an answer is kept in.
    static QString topicFor(const QString &question, const QString &answer);

private:
    QString read(const QString &relative) const;
    bool writeNew(const QString &relative, const QString &text, QString *error);

    QString m_folder;
};

} // namespace splitrock
