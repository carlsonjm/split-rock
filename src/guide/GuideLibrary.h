// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

namespace splitrock {

// The words of a text that say what it is about, folded so that "passwords"
// meets "password", without the words too common to say so.
QSet<QString> topicWords(const QString &text);

// The header of a guide entry, a tour or a skill: the "key: value" lines
// between the file's first two "---" lines, the items listed under "also",
// and the line the header ends on. The last value given for a key holds.
struct FileHeader {
    QHash<QString, QString> fields;
    QStringList also;
    int end = 0;
};

// Reads the header at the top of lines into header; false, with *problem set,
// when the file has none.
bool readHeader(const QStringList &lines, FileHeader *header, QString *problem);

// One entry of the troubleshooting guide, or one tour, as written in the format
// docs/DECISIONS.md lays down.
struct GuideEntry {
    enum class Kind { Guide, Tour };

    Kind kind = Kind::Guide;
    // The folder it came from: "plasma", a desktop's name, or "device".
    QString source;
    QString id;
    QString title;
    QStringList also;
    QString applies;
    QString replaces;
    QString checked;
    QString path;

    // A guide entry's three parts.
    QString seen;
    QString why;
    QStringList steps;

    // A tour's opening words; its steps are in `steps`, the action first.
    QString intro;

    QString key() const { return source + QLatin1Char('/') + id; }
};

// What the guide needs to know about this computer to choose its entries.
struct GuideContext {
    // Lower-case names of the desktops whose entries are installed, such as
    // "shuffle". "plasma" is always present.
    QSet<QString> desktops;
    // The product name the hardware reports, such as "ROG Flow Z13 GZ302EA".
    QString deviceProduct;
};

// The guide and the tour, read from plain files. The in-box layer: it needs
// no account and no network.
class GuideLibrary
{
public:
    // The format versions this reader understands. An entry in any other is
    // skipped, so an older Split Rock is never misled by a newer desktop.
    static constexpr int Format = 1;

    // Reads every `guide/<source>/` and `tour/<source>/` under each root, in
    // order, then the entries in the computer's own device folder, if given.
    void load(const QStringList &roots, const QString &deviceFolder = {});

    // The shared data folders Split Rock's own and a desktop's entries are
    // installed in, never one in the person's home.
    static QStringList systemRoots();

    // The entries that hold on this computer, with every entry another one
    // replaces left out.
    QList<GuideEntry> entries(const GuideContext &context, GuideEntry::Kind kind) const;

    // The guide entries that best answer a question, best first. Only entries
    // whose title or other wordings share enough words with it are returned.
    QList<GuideEntry> find(const QString &question, const GuideContext &context, int limit = 3) const;

    // Files that could not be read as entries, with why, for the checks.
    QStringList problems() const { return m_problems; }

    // The desktops whose entries were found, lower case, "plasma" included.
    QSet<QString> sources() const;

    static GuideEntry parse(const QString &path, const QString &text, const QString &source,
                            GuideEntry::Kind kind, QString *problem);

private:
    void loadFolder(const QString &folder, const QString &source, GuideEntry::Kind kind);

    QList<GuideEntry> m_entries;
    QStringList m_problems;
};

} // namespace splitrock
