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
// Split Rock runs the change itself, outside the assistant's fence, as the
// person and never as the administrator (docs/DECISIONS.md, No card runs a
// command as the administrator): a proposal that reaches for the administrator
// is refused, and the change runs where sudo, pkexec and the system bus can't
// lift it. Changes to the system are made by Split Rock's own Helpers instead.
// The person's own files the change names are copied aside first. Each change
// is kept, with how to undo it, in a folder of its own.
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
        // The snapshot pair taken around a system change, from before cards
        // stopped making them, or -1.
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
    ~ChangeRunner() override;

    static QString defaultStore();
    // What tells the running desktop to read again the settings a change
    // writes, run after the change and after Undo; empty when none is known.
    static QString reloadFor(const ChangeProposal &change);
    // The proposal with the undo and check Split Rock knows for it: a change
    // to the desktop's look is undone by putting the look of now back as a
    // whole, live, whatever way back the assistant gave.
    static ChangeProposal withKnownUndo(const ChangeProposal &change, const Appearance &now);
    // How the card says a proposal would be undone.
    static QString undoPromise(const ChangeProposal &change);
    // The command with the yes already given to package tools that would
    // stop to ask it: the person said yes on the card, and no one is there to
    // answer a question in the middle of the change. pacman's installs,
    // removals and upgrades get --noconfirm, Flatpak's -y --noninteractive.
    static QString unattended(const QString &command);

    // What a change, its undo and its check run inside: bubblewrap at bwrap,
    // with no way to gain rights, and the system bus hidden, or replaced by
    // systemBus, a filtered one. Empty when bwrap is, and then nothing is made.
    static QStringList guard(const QString &bwrap, const QString &systemBus);
    static QString bubblewrap();
    // The system services the filtered system bus reaches.
    static QStringList systemServices();

    // The program that asks the system's password prompt, for putting back
    // an old system change's snapshots; tests replace it.
    void setElevate(const QStringList &program) { m_elevate = program; }
    // The program that holds the administrator's jobs (app/Admin.h).
    void setAdmin(const QStringList &program) { m_admin = program; }
    // How long a change may run before it is stopped.
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
    // The command run inside the guard, or empty when it can't be.
    QStringList guarded(const QString &command);
    // The filtered system bus's socket, started for a change and stopped once
    // it and its check are done; empty when there is no system bus to filter.
    QString startSystemBus();
    void stopSystemBus();
    void run(const QStringList &program, int timeLimit, std::function<void(int exitCode, const QString &output)> done);
    // Runs the desktop's reload, then the check, as the person, and hands
    // on the check's output.
    void afterChange(const ChangeProposal &change, bool withCheck, std::function<void(const QString &checked)> done);

    QString m_store;
    QStringList m_elevate{QStringLiteral("pkexec")};
    QStringList m_admin;
    QString m_bwrap;
    QProcess *m_proxy = nullptr;
    QString m_proxySocket;
    int m_timeLimit = 120000;
    QProcess *m_process = nullptr;
    // The change's own command is done, and the desktop's reload or the check
    // is still running.
    bool m_finishing = false;
};

} // namespace splitrock
