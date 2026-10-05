// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

namespace splitrock {

// One settings skill: how to look at, change, undo and check one kind of
// setting, written for the assistant rather than the person.
struct Skill {
    QString source;
    QString id;
    QString title;
    QStringList also;
    QString checked;
    QString path;
    // The read-only commands under "Look around", run outside the fence when
    // the skill is opened, so the assistant sees this computer's own names.
    QString look;
    // Everything below the header, as the assistant reads it.
    QString text;
};

// The shelf of settings skills, read from `skills/<source>/` under the same
// shared data folders as the guide. The assistant starts with only the shelf's
// list, and opens a skill when a question needs it (docs/DECISIONS.md, The
// assistant opens settings skills from a shelf).
class SkillShelf
{
public:
    static constexpr int Format = 1;

    void load(const QStringList &roots);

    const QList<Skill> &skills() const { return m_skills; }
    bool isEmpty() const { return m_skills.isEmpty(); }

    // One line a skill, "id: title", for the assistant's opening words.
    QString shelf() const;

    // The skill with this id, or none.
    const Skill *skill(const QString &id) const;

    // The skill a question most likely needs, or none: Split Rock's hint,
    // which the assistant may ignore.
    const Skill *find(const QString &question) const;

    // Opens a skill for the assistant: its text, then what its look-around
    // commands read on this computer, given to `done` once they have run.
    // Only the skill's own shipped commands run, never the assistant's. An
    // unknown id is answered with the shelf.
    void open(const QString &id, QObject *context, const std::function<void(const QString &)> &done,
              int timeLimit = 10000) const;

    QStringList problems() const { return m_problems; }

    static Skill parse(const QString &path, const QString &text, const QString &source, QString *problem);

private:
    QList<Skill> m_skills;
    QStringList m_problems;
};

} // namespace splitrock
