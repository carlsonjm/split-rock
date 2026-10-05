// SPDX-License-Identifier: GPL-2.0-or-later
#include "guide/GuideLibrary.h"

#include "app/AnswerText.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QStandardPaths>

#include <algorithm>
#include <utility>

namespace splitrock {

namespace {

// Words too common to say what a question is about.
const QSet<QString> &stopWords()
{
    static const QSet<QString> words = [] {
        QSet<QString> set;
        for (const char *w : {"a", "an", "and", "any", "are", "as", "at", "be", "but", "by", "can", "can't",
                              "cannot", "did", "didn't", "do", "does", "doesn't", "don't", "for", "from",
                              "get", "got", "had", "has", "have", "how", "i", "i'm", "i've", "if", "in", "into",
                              "is", "isn't", "it", "it's", "its", "just", "keep", "keeps", "me", "my", "no",
                              "not", "now", "of", "on", "or", "please", "so", "some", "still", "that", "the",
                              "then", "there", "this", "to", "up", "was", "wasn't", "what", "when", "where",
                              "which", "why", "will", "with", "won't", "would", "you", "your"})
            set.insert(QString::fromLatin1(w));
        return set;
    }();
    return words;
}

} // namespace

QSet<QString> topicWords(const QString &text)
{
    static const QRegularExpression word(QStringLiteral("[a-z0-9]+(?:['’-][a-z0-9]+)*"));
    QSet<QString> words;
    auto it = word.globalMatch(text.toLower());
    while (it.hasNext()) {
        QString w = it.next().captured(0);
        w.replace(QChar(0x2019), QLatin1Char('\''));
        if (stopWords().contains(w))
            continue;
        if (w.size() > 3 && w.endsWith(QLatin1Char('s')) && !w.endsWith(QLatin1String("ss")))
            w.chop(1);
        words.insert(w);
    }
    return words;
}

namespace {

// Steps numbered from 1, each with the lines that continue it, as plain words.
QStringList numberedSteps(const QString &block)
{
    static const QRegularExpression start(QStringLiteral("^\\d+\\.\\s+(.*)$"));
    QStringList steps;
    for (const QString &line : block.split(QLatin1Char('\n'))) {
        const QRegularExpressionMatch match = start.match(line);
        if (match.hasMatch())
            steps.append(match.captured(1).trimmed());
        else if (!steps.isEmpty() && !line.trimmed().isEmpty())
            steps.last() += QLatin1Char(' ') + line.trimmed();
    }
    for (QString &step : steps)
        step = AnswerText::plain(step);
    return steps;
}

QString paragraphText(const QString &block)
{
    QStringList paragraphs;
    for (const QString &paragraph : block.split(QStringLiteral("\n\n"))) {
        const QString text = AnswerText::plain(paragraph);
        if (!text.isEmpty())
            paragraphs.append(text);
    }
    return paragraphs.join(QStringLiteral("\n\n"));
}

bool holds(const GuideEntry &entry, const GuideContext &context)
{
    if (entry.applies.isEmpty())
        return true;
    if (context.desktops.contains(entry.applies.toLower()))
        return true;
    return !context.deviceProduct.isEmpty()
        && context.deviceProduct.contains(entry.applies, Qt::CaseInsensitive);
}

} // namespace

GuideEntry GuideLibrary::parse(const QString &path, const QString &text, const QString &source,
                               GuideEntry::Kind kind, QString *problem)
{
    GuideEntry entry;
    entry.kind = kind;
    entry.source = source;
    entry.path = path;

    const QStringList lines = text.split(QLatin1Char('\n'));
    if (lines.isEmpty() || lines.first() != QLatin1String("---") || lines.indexOf(QStringLiteral("---"), 1) < 0) {
        *problem = QStringLiteral("no header");
        return {};
    }
    const int end = int(lines.indexOf(QStringLiteral("---"), 1));
    static const QRegularExpression pair(QStringLiteral("^([a-z]+):\\s*(.*)$"));
    static const QRegularExpression item(QStringLiteral("^\\s+-\\s+(.*)$"));
    QString key;
    int format = 0;
    for (int i = 1; i < end; ++i) {
        const QString &line = lines.at(i);
        const QRegularExpressionMatch listItem = item.match(line);
        if (listItem.hasMatch() && key == QLatin1String("also")) {
            entry.also.append(listItem.captured(1).trimmed());
            continue;
        }
        const QRegularExpressionMatch match = pair.match(line);
        if (!match.hasMatch())
            continue;
        key = match.captured(1);
        const QString value = match.captured(2).trimmed();
        if (key == QLatin1String("id"))
            entry.id = value;
        else if (key == QLatin1String("title"))
            entry.title = value;
        else if (key == QLatin1String("applies"))
            entry.applies = value;
        else if (key == QLatin1String("replaces"))
            entry.replaces = value;
        else if (key == QLatin1String("checked"))
            entry.checked = value;
        else if (key == QLatin1String("format"))
            format = value.toInt();
    }
    if (format != Format) {
        *problem = QStringLiteral("format %1 is not one this reader knows").arg(format);
        return {};
    }
    if (entry.id.isEmpty() || entry.title.isEmpty()) {
        *problem = QStringLiteral("no id or title");
        return {};
    }

    const QString body = lines.mid(end + 1).join(QLatin1Char('\n'));
    if (kind == GuideEntry::Kind::Tour) {
        const qsizetype first = body.indexOf(QRegularExpression(QStringLiteral("^1\\.\\s"), QRegularExpression::MultilineOption));
        entry.intro = paragraphText(first < 0 ? body : body.left(first));
        entry.steps = numberedSteps(first < 0 ? QString() : body.mid(first));
    } else {
        static const QRegularExpression heading(QStringLiteral("^## (.+)$"), QRegularExpression::MultilineOption);
        QHash<QString, QString> sections;
        auto it = heading.globalMatch(body);
        QList<QRegularExpressionMatch> found;
        while (it.hasNext())
            found.append(it.next());
        for (int i = 0; i < found.size(); ++i) {
            const qsizetype from = found.at(i).capturedEnd();
            const qsizetype to = i + 1 < found.size() ? found.at(i + 1).capturedStart() : body.size();
            sections.insert(found.at(i).captured(1).trimmed(), body.mid(from, to - from));
        }
        entry.seen = paragraphText(sections.value(QStringLiteral("What you see")));
        entry.why = paragraphText(sections.value(QStringLiteral("Why")));
        entry.steps = numberedSteps(sections.value(QStringLiteral("What to do")));
    }
    if (entry.steps.isEmpty()) {
        *problem = QStringLiteral("no numbered steps");
        return {};
    }
    return entry;
}

void GuideLibrary::loadFolder(const QString &folder, const QString &source, GuideEntry::Kind kind)
{
    const QDir dir(folder);
    const QStringList files = dir.entryList({QStringLiteral("*.md")}, QDir::Files, QDir::Name);
    for (const QString &name : files) {
        if (name == QLatin1String("README.md") || name == QLatin1String("DEVICE.md"))
            continue;
        QFile file(dir.filePath(name));
        if (!file.open(QIODevice::ReadOnly)) {
            m_problems.append(dir.filePath(name) + QStringLiteral(": could not be read"));
            continue;
        }
        QString problem;
        const GuideEntry entry = parse(dir.filePath(name), QString::fromUtf8(file.readAll()), source, kind, &problem);
        if (!problem.isEmpty()) {
            m_problems.append(dir.filePath(name) + QStringLiteral(": ") + problem);
            continue;
        }
        // The first root wins for an entry found twice, as the shared data
        // folders are searched in order.
        const bool duplicate = std::any_of(m_entries.cbegin(), m_entries.cend(), [&entry](const GuideEntry &e) {
            return e.kind == entry.kind && e.key() == entry.key();
        });
        if (!duplicate)
            m_entries.append(entry);
    }
}

void GuideLibrary::load(const QStringList &roots, const QString &deviceFolder)
{
    m_entries.clear();
    m_problems.clear();
    for (const QString &root : roots) {
        for (const auto &[kindFolder, kind] : {std::pair{QStringLiteral("guide"), GuideEntry::Kind::Guide},
                                               std::pair{QStringLiteral("tour"), GuideEntry::Kind::Tour}}) {
            const QDir base(QDir(root).filePath(kindFolder));
            if (!base.exists())
                continue;
            for (const QString &source : base.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
                loadFolder(base.filePath(source), source.toLower(), kind);
        }
    }
    if (!deviceFolder.isEmpty() && QFileInfo(deviceFolder).isDir())
        loadFolder(deviceFolder, QStringLiteral("device"), GuideEntry::Kind::Guide);
}

QStringList GuideLibrary::systemRoots()
{
    const QString home = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    QStringList roots;
    for (const QString &location : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation)) {
        if (location == home)
            continue;
        const QString root = QDir(location).filePath(QStringLiteral("split-rock"));
        if (QFileInfo(root).isDir())
            roots.append(root);
    }
    return roots;
}

QSet<QString> GuideLibrary::sources() const
{
    QSet<QString> names{QStringLiteral("plasma")};
    for (const GuideEntry &entry : m_entries)
        names.insert(entry.source);
    return names;
}

QList<GuideEntry> GuideLibrary::entries(const GuideContext &context, GuideEntry::Kind kind) const
{
    QSet<QString> replaced;
    for (const GuideEntry &entry : m_entries) {
        if (entry.kind == kind && !entry.replaces.isEmpty() && holds(entry, context))
            replaced.insert(entry.replaces);
    }
    QList<GuideEntry> result;
    for (const GuideEntry &entry : m_entries) {
        if (entry.kind == kind && holds(entry, context) && !replaced.contains(entry.key()))
            result.append(entry);
    }
    return result;
}

QList<GuideEntry> GuideLibrary::find(const QString &question, const GuideContext &context, int limit) const
{
    const QSet<QString> asked = topicWords(question);
    if (asked.isEmpty())
        return {};
    struct Scored {
        int score;
        GuideEntry entry;
    };
    const QList<GuideEntry> candidates = entries(context, GuideEntry::Kind::Guide);
    QList<QSet<QString>> namedBy;
    QHash<QString, int> naming;
    for (const GuideEntry &entry : candidates) {
        // The person's own wordings count most; what they see counts too.
        QSet<QString> named = topicWords(entry.title);
        for (const QString &wording : entry.also)
            named.unite(topicWords(wording));
        for (const QString &word : named)
            ++naming[word];
        namedBy.append(named);
    }
    QList<Scored> scored;
    for (qsizetype i = 0; i < candidates.size(); ++i) {
        const GuideEntry &entry = candidates.at(i);
        const QSet<QString> shared = QSet<QString>(asked).intersect(namedBy.at(i));
        const int strong = int(shared.size());
        const int weak = int(QSet<QString>(asked).intersect(topicWords(entry.seen)).size());
        if (strong == 0)
            continue;
        // One word in common answers only when no other entry uses it:
        // "screen" alone says nothing about which problem the person has.
        if (strong == 1 && naming.value(*shared.cbegin()) > 1)
            continue;
        const int score = 3 * strong + weak;
        if (score >= 4 || strong * 2 >= asked.size())
            scored.append({score, entry});
    }
    std::stable_sort(scored.begin(), scored.end(), [](const Scored &a, const Scored &b) { return a.score > b.score; });
    QList<GuideEntry> result;
    for (const Scored &s : scored) {
        if (result.size() >= limit)
            break;
        result.append(s.entry);
    }
    return result;
}

} // namespace splitrock
