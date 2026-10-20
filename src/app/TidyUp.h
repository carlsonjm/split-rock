// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "app/Chore.h"

#include <QSet>
#include <QVariantList>

namespace splitrock {

// The Tidy up Downloads card: what has piled up in the Downloads folder and on
// the Desktop, in groups, each file with its own tick; then one tap moves
// what is ticked to the Trash, where it can still be put back.
//
// Only those two folders, and only the files directly in them. It reads names,
// sizes and dates, and reads a file through only to tell that a copy, "report
// (1).pdf", is the same as its original. Only the plainly safe is ticked:
// copies that match their original, Windows programs, and installers of apps
// already installed. Old files and screenshots are listed for the person to
// tick. An AppImage opened in the last month is run from where it is, so it is
// never ticked. Nothing needs a password: these are the person's own files.
//
// Its stages: looking, ready, nothing, moving, done or failed.
class TidyUp : public Chore
{
    Q_OBJECT
    // Each group: id, title, detail, size (in words), kb, count, chosen
    // (any of its files), tone and files; each file: path, name, detail,
    // size, kb, chosen and kept (never ticked by itself).
    Q_PROPERTY(QVariantList groups READ groupList NOTIFY changed)
    Q_PROPERTY(qint64 chosenKb READ chosenKb NOTIFY changed)
    Q_PROPERTY(QString chosenSize READ chosenSize NOTIFY changed)
    Q_PROPERTY(int chosenCount READ chosenCount NOTIFY changed)
    // What the two folders hold, all of it, folders inside them too.
    Q_PROPERTY(QString heldSize READ heldSize NOTIFY changed)
    // The same in words: "Downloads and Desktop hold 7.4 GB."
    Q_PROPERTY(QString heldWords READ heldWords NOTIFY changed)
    // Once moved: how many files went, and their size.
    Q_PROPERTY(int moved READ moved NOTIFY changed)
    Q_PROPERTY(QString movedSize READ movedSize NOTIFY changed)
    // Files that couldn't be moved, when some couldn't; empty otherwise.
    Q_PROPERTY(QString missed READ missed NOTIFY changed)
    Q_PROPERTY(QString why READ why NOTIFY changed)

public:
    struct File {
        QString path;
        QString name;
        QString detail;
        qint64 kb = 0;
        bool chosen = false;
        bool kept = false;
    };

    struct Group {
        QString id;
        QString title;
        QString detail;
        QString tone;
        QList<File> files;
    };

    struct Look {
        QList<Group> groups;
        qint64 heldKb = 0;
    };

    explicit TidyUp(QObject *parent = nullptr);

    bool busy() const override;
    QList<Group> groups() const { return m_look.groups; }
    QVariantList groupList() const;
    qint64 chosenKb() const;
    QString chosenSize() const { return size(chosenKb()); }
    int chosenCount() const;
    QString heldSize() const { return size(m_look.heldKb); }
    QString heldWords() const;
    int moved() const { return m_moved; }
    QString movedSize() const { return size(m_movedKb); }
    QString missed() const { return m_missed; }
    QString why() const { return m_why; }

    Q_INVOKABLE void check();
    // Ticks or unticks every file of a group that isn't kept.
    Q_INVOKABLE void choose(const QString &group, bool chosen);
    Q_INVOKABLE void chooseFile(const QString &path, bool chosen);
    Q_INVOKABLE void start();
    Q_INVOKABLE void openTrash();

    // What the look printed, read into groups, as of now in seconds since
    // 1970.
    static Look readLook(const QString &output, qint64 now);
    // The app an installer's name is for, as its package is named:
    // "zoom" from "zoom-6.2.0-1-x86_64.pkg.tar.zst" or "zoom_amd64.deb";
    // empty when it isn't a Linux installer.
    static QString installerFor(const QString &fileName);

    // The folders to look in; Downloads and the Desktop otherwise.
    void setFolders(const QStringList &folders) { m_folders = folders; }
    void setLookScript(const QString &script) { m_lookScript = script; }
    // What opens the Trash.
    void setTrashOpener(const QStringList &program) { m_trashOpener = program; }

private:
    void looked(const QString &output);

    Look m_look;
    int m_moved = 0;
    qint64 m_movedKb = 0;
    QString m_missed;
    QString m_why;

    QStringList m_folders;
    QString m_lookScript;
    QStringList m_trashOpener{QStringLiteral("xdg-open"), QStringLiteral("trash:/")};
};

} // namespace splitrock
