// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "app/Appearance.h"
#include "app/Change.h"

#include <QDateTime>
#include <QObject>

#include <functional>
#include <optional>

class QProcess;

namespace splitrock {

// Makes a change the person approved on the Do it card, and puts it back.
//
// Split Rock runs the change itself, outside the assistant's fence: as the
// person, or, for a change to the system, through the system's own password
// prompt, one action at a time (docs/DECISIONS.md, Administrator rights only
// through the system's prompt). Before a system change it takes a snapshot pair
// where the disk has snapper set up, so Undo can put back exactly the system
// files the change touched. The person's own files the change names are copied
// aside first. Each change is kept, with how to undo it, in a folder of its own.
class ChangeRunner : public QObject
{
    Q_OBJECT

public:
    struct Copy {
        QString original;
        QString copy;
        // The original did not exist, so Undo removes it.
        bool absent = false;
    };

    struct Record {
        QString id;
        QDateTime made;
        ChangeProposal proposal;
        // making, done, failed, undone or undoFailed.
        QString status;
        int exitCode = -1;
        QString output;
        // What the proposal's check read back after the change.
        QString checked;
        // The snapshot pair taken around a system change, or -1.
        int pre = -1;
        int post = -1;
        QList<Copy> copies;
        QDateTime undone;

        // How Undo would put it back, in plain words, or empty when it can't.
        QString undoWay() const;
        bool canUndo() const { return !undoWay().isEmpty() && (status == QLatin1String("done") || status == QLatin1String("failed")); }
        QJsonObject toJson() const;
        static Record fromJson(const QJsonObject &object);
    };

    // store: the folder each change is kept in, one folder a change.
    explicit ChangeRunner(const QString &store = defaultStore(), QObject *parent = nullptr);

    static QString defaultStore();
    // Whether this computer takes system snapshots Split Rock can use.
    static bool systemSnapshots();
    // What tells the running desktop to read again the settings a change
    // writes, run after the change and after Undo; empty when none is known.
    static QString reloadFor(const ChangeProposal &change);
    // The proposal with the undo and check Split Rock knows for it: a change
    // to the desktop's look is undone by putting the look of now back as a
    // whole, live, whatever way back the assistant gave.
    static ChangeProposal withKnownUndo(const ChangeProposal &change, const Appearance &now);
    // How the card says a proposal would be undone.
    static QString undoPromise(const ChangeProposal &change);

    // The program that asks the system's password prompt and runs the rest as
    // the administrator; tests replace it.
    void setElevate(const QStringList &program) { m_elevate = program; }
    void setSnapshots(bool on) { m_snapshots = on; }
    // How long a change may run before it is stopped; a system change gets
    // longer, for the password prompt.
    void setTimeLimit(int milliseconds) { m_timeLimit = milliseconds; }

    bool busy() const { return m_process != nullptr || m_finishing; }
    // Starts making the change; made() follows. before is given the record
    // once the person's files are copied and before anything runs, to write
    // the change down; when it returns false nothing is made. Empty when
    // nothing was started.
    QString make(const ChangeProposal &change, const std::function<bool(const Record &)> &before = {});
    // Starts putting a change back; undone() follows. False, with why, when it
    // can't be.
    bool undo(const QString &id, QString *error = nullptr);

    QList<Record> records() const;
    std::optional<Record> record(const QString &id) const;

Q_SIGNALS:
    void made(const splitrock::ChangeRunner::Record &record);
    void undone(const splitrock::ChangeRunner::Record &record);

private:
    void save(const Record &record) const;
    void run(const QStringList &program, int timeLimit, std::function<void(int exitCode, const QString &output)> done);
    // Runs the desktop's reload, then the check, as the person, and hands
    // on the check's output.
    void afterChange(const ChangeProposal &change, bool withCheck, std::function<void(const QString &checked)> done);

    QString m_store;
    QStringList m_elevate{QStringLiteral("pkexec")};
    bool m_snapshots = true;
    int m_timeLimit = 120000;
    QProcess *m_process = nullptr;
    // The change's own command is done, and the desktop's reload or the check
    // is still running.
    bool m_finishing = false;
};

} // namespace splitrock
