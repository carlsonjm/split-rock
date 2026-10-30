// SPDX-License-Identifier: GPL-2.0-or-later
#include "guide/SkillShelf.h"

#include "guide/GuideLibrary.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>

#include <algorithm>

namespace splitrock {

Skill SkillShelf::parse(const QString &path, const QString &text, const QString &source, QString *problem)
{
    Skill skill;
    skill.source = source;
    skill.path = path;

    const QStringList lines = text.split(QLatin1Char('\n'));
    FileHeader header;
    if (!readHeader(lines, &header, problem))
        return {};
    const int end = header.end;
    skill.id = header.fields.value(QStringLiteral("id"));
    skill.title = header.fields.value(QStringLiteral("title"));
    skill.checked = header.fields.value(QStringLiteral("checked"));
    skill.also = header.also;
    const int format = header.fields.value(QStringLiteral("format")).toInt();
    if (format != Format) {
        *problem = QStringLiteral("format %1 is not one this reader knows").arg(format);
        return {};
    }
    if (skill.id.isEmpty() || skill.title.isEmpty()) {
        *problem = QStringLiteral("no id or title");
        return {};
    }
    skill.text = lines.mid(end + 1).join(QLatin1Char('\n')).trimmed();
    // A skill may ship a helper beside it, named from {here}: the folder it
    // was read from, wherever it is installed.
    skill.text.replace(QStringLiteral("{here}"), QFileInfo(path).absolutePath());

    // The look is the first sh block under "## Look around".
    static const QRegularExpression look(QStringLiteral("^## Look around\\s*\\n+```sh\\n(.*?)^```"),
                                         QRegularExpression::MultilineOption
                                             | QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpressionMatch found = look.match(skill.text);
    if (!found.hasMatch()) {
        *problem = QStringLiteral("no sh block under \"Look around\"");
        return {};
    }
    skill.look = found.captured(1);
    return skill;
}

void SkillShelf::load(const QStringList &roots)
{
    m_skills.clear();
    m_problems.clear();
    for (const QString &root : roots) {
        const QDir base(QDir(root).filePath(QStringLiteral("skills")));
        if (!base.exists())
            continue;
        for (const QString &source : base.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            const QDir dir(base.filePath(source));
            for (const QString &name : dir.entryList({QStringLiteral("*.md")}, QDir::Files, QDir::Name)) {
                if (name == QLatin1String("README.md"))
                    continue;
                QFile file(dir.filePath(name));
                if (!file.open(QIODevice::ReadOnly)) {
                    m_problems.append(dir.filePath(name) + QStringLiteral(": could not be read"));
                    continue;
                }
                QString problem;
                const Skill skill = parse(dir.filePath(name), QString::fromUtf8(file.readAll()), source.toLower(), &problem);
                if (!problem.isEmpty()) {
                    m_problems.append(dir.filePath(name) + QStringLiteral(": ") + problem);
                    continue;
                }
                // The first root wins for a skill found twice, as the shared
                // data folders are searched in order.
                if (!this->skill(skill.id))
                    m_skills.append(skill);
            }
        }
    }
}

QString SkillShelf::shelf() const
{
    QStringList lines;
    for (const Skill &skill : m_skills)
        lines.append(QStringLiteral("%1: %2").arg(skill.id, skill.title));
    return lines.join(QLatin1Char('\n'));
}

const Skill *SkillShelf::skill(const QString &id) const
{
    const auto it = std::find_if(m_skills.cbegin(), m_skills.cend(), [&id](const Skill &s) { return s.id == id; });
    return it == m_skills.cend() ? nullptr : &*it;
}

const Skill *SkillShelf::find(const QString &question) const
{
    const QSet<QString> asked = topicWords(question);
    if (asked.isEmpty())
        return nullptr;
    const Skill *best = nullptr;
    int bestScore = 0;
    bool tied = false;
    for (const Skill &skill : m_skills) {
        QSet<QString> named = topicWords(skill.title);
        for (const QString &wording : skill.also)
            named.unite(topicWords(wording));
        const int score = int(QSet<QString>(asked).intersect(named).size());
        if (score > bestScore) {
            best = &skill;
            bestScore = score;
            tied = false;
        } else if (score == bestScore && score > 0) {
            tied = true;
        }
    }
    // A tie says nothing about which one the question needs.
    return tied ? nullptr : best;
}

void SkillShelf::open(const QString &id, QObject *context, const std::function<void(const QString &)> &done,
                      int timeLimit) const
{
    const Skill *found = skill(id);
    if (!found) {
        done(QStringLiteral("There is no settings skill called \"%1\". The shelf:\n%2\n").arg(id, shelf()));
        return;
    }
    const Skill skill = *found;
    auto *process = new QProcess(context);
    process->setProcessChannelMode(QProcess::MergedChannels);
    auto *limit = new QTimer(process);
    limit->setSingleShot(true);
    QObject::connect(limit, &QTimer::timeout, process, &QProcess::kill);
    const auto answer = [skill](const QString &seen) {
        return QStringLiteral("# %1\n\n%2\n\n## On this computer now\n\n%3\n").arg(skill.title, skill.text, seen);
    };
    QObject::connect(process, &QProcess::finished, context, [process, answer, done] {
        QString seen = QString::fromUtf8(process->readAll()).trimmed().right(6000);
        if (process->exitStatus() != QProcess::NormalExit)
            seen += QStringLiteral("\n(Stopped: it was still running when its time was up.)");
        done(answer(seen.trimmed().isEmpty() ? QStringLiteral("(nothing was read)") : seen.trimmed()));
        process->deleteLater();
    });
    QObject::connect(process, &QProcess::errorOccurred, context, [process, answer, done](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        done(answer(QStringLiteral("(It could not be read.)")));
        process->deleteLater();
    });
    process->start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), skill.look});
    process->closeWriteChannel();
    limit->start(timeLimit);
}

} // namespace splitrock
