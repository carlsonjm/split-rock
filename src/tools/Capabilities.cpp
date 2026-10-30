// SPDX-License-Identifier: GPL-2.0-or-later
#include "tools/Capabilities.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

#include <cmath>
#include <limits>

namespace splitrock::tools {

namespace {

// The only version of the contract this reads.
constexpr int ContractVersion = 0;
const QStringList Types{QStringLiteral("s"), QStringLiteral("u"), QStringLiteral("i"), QStringLiteral("b")};
const QStringList Risks{QStringLiteral("read"), QStringLiteral("write"), QStringLiteral("outward")};

bool isName(const QString &text)
{
    static const QRegularExpression name(QStringLiteral("^[A-Za-z][A-Za-z0-9_]{0,63}$"));
    return name.match(text).hasMatch();
}

// A whole number from the bus or a model's JSON, which hand one over as any
// of several types; never a string, a truth value or a fraction.
std::optional<qint64> wholeNumber(const QVariant &value)
{
    switch (value.typeId()) {
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::Short:
    case QMetaType::UShort:
    case QMetaType::UChar:
    case QMetaType::LongLong:
        return value.toLongLong();
    case QMetaType::ULongLong:
        if (value.toULongLong() > quint64(std::numeric_limits<qint64>::max()))
            return std::nullopt;
        return qint64(value.toULongLong());
    case QMetaType::Double: {
        const double number = value.toDouble();
        if (number != std::floor(number) || std::abs(number) > 9.0e15)
            return std::nullopt;
        return qint64(number);
    }
    default:
        return std::nullopt;
    }
}

// A pattern whose closing $ means the very end: as written, $ also matches
// before a last line break, which would let "n1\n" through as an id.
QString endsAtTheEnd(const QString &pattern)
{
    if (pattern.endsWith(QLatin1Char('$')) && !pattern.endsWith(QLatin1String("\\$")))
        return pattern.chopped(1) + QStringLiteral("\\z");
    return pattern;
}

} // namespace

QVariantMap Tool::toMap() const
{
    QVariantList args;
    for (const Argument &argument : arguments) {
        QVariantMap a{{QStringLiteral("name"), argument.name},
                      {QStringLiteral("type"), argument.type},
                      {QStringLiteral("required"), argument.required}};
        if (!argument.fallback.isUndefined() && !argument.fallback.isNull())
            a.insert(QStringLiteral("default"), argument.fallback.toVariant());
        if (argument.maxLength >= 0)
            a.insert(QStringLiteral("maxLength"), argument.maxLength);
        if (argument.min)
            a.insert(QStringLiteral("min"), *argument.min);
        if (argument.max)
            a.insert(QStringLiteral("max"), *argument.max);
        if (!argument.pattern.isEmpty())
            a.insert(QStringLiteral("pattern"), argument.pattern);
        args.append(a);
    }
    // Where the app answers stays with the broker: a client needs only what
    // the tool does.
    return {{QStringLiteral("name"), name},
            {QStringLiteral("app"), app},
            {QStringLiteral("appName"), appName},
            {QStringLiteral("risk"), risk},
            {QStringLiteral("codex"), codex},
            {QStringLiteral("summary"), summary},
            {QStringLiteral("args"), args}};
}

Tool Tool::fromMap(const QVariantMap &map)
{
    Tool tool;
    tool.name = map.value(QStringLiteral("name")).toString();
    tool.app = map.value(QStringLiteral("app")).toString();
    tool.appName = map.value(QStringLiteral("appName")).toString();
    tool.risk = map.value(QStringLiteral("risk")).toString();
    tool.codex = map.value(QStringLiteral("codex")).toBool();
    tool.summary = map.value(QStringLiteral("summary")).toString();
    for (const QVariant &value : map.value(QStringLiteral("args")).toList()) {
        const QVariantMap a = value.toMap();
        Argument argument;
        argument.name = a.value(QStringLiteral("name")).toString();
        argument.type = a.value(QStringLiteral("type")).toString();
        argument.required = a.value(QStringLiteral("required")).toBool();
        if (a.contains(QStringLiteral("default")))
            argument.fallback = QJsonValue::fromVariant(a.value(QStringLiteral("default")));
        argument.maxLength = a.value(QStringLiteral("maxLength"), -1).toInt();
        if (a.contains(QStringLiteral("min")))
            argument.min = a.value(QStringLiteral("min")).toLongLong();
        if (a.contains(QStringLiteral("max")))
            argument.max = a.value(QStringLiteral("max")).toLongLong();
        argument.pattern = a.value(QStringLiteral("pattern")).toString();
        tool.arguments.append(argument);
    }
    return tool;
}

QJsonObject Tool::argumentShape() const
{
    QJsonObject properties;
    QJsonArray required;
    for (const Argument &argument : arguments) {
        QJsonObject shape;
        if (argument.type == QLatin1String("s")) {
            shape.insert(QStringLiteral("type"), QStringLiteral("string"));
            if (argument.maxLength >= 0)
                shape.insert(QStringLiteral("maxLength"), argument.maxLength);
            if (!argument.pattern.isEmpty())
                shape.insert(QStringLiteral("pattern"), argument.pattern);
        } else if (argument.type == QLatin1String("b")) {
            shape.insert(QStringLiteral("type"), QStringLiteral("boolean"));
        } else {
            shape.insert(QStringLiteral("type"), QStringLiteral("integer"));
            const qint64 floor = argument.type == QLatin1String("u") ? 0 : std::numeric_limits<int>::min();
            shape.insert(QStringLiteral("minimum"), std::max(floor, argument.min.value_or(floor)));
            if (argument.max)
                shape.insert(QStringLiteral("maximum"), *argument.max);
        }
        properties.insert(argument.name, shape);
        if (argument.required)
            required.append(argument.name);
    }
    return {{QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("properties"), properties},
            {QStringLiteral("required"), required},
            {QStringLiteral("additionalProperties"), false}};
}

QStringList Capabilities::defaultFolders()
{
    QStringList folders;
    const QString suffix = QStringLiteral("/good-input/capabilities");
    folders.append(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + suffix);
    QString dirs = qEnvironmentVariable("XDG_DATA_DIRS");
    if (dirs.isEmpty())
        dirs = QStringLiteral("/usr/local/share:/usr/share");
    for (const QString &dir : dirs.split(QLatin1Char(':'), Qt::SkipEmptyParts)) {
        const QString folder = QDir::cleanPath(dir) + suffix;
        if (!folders.contains(folder))
            folders.append(folder);
    }
    return folders;
}

Capabilities::Capabilities(const QStringList &folders)
    : m_folders(folders)
{
}

void Capabilities::read()
{
    m_tools.clear();
    m_problems.clear();
    QSet<QString> apps;
    QSet<QString> names;
    for (const QString &folder : std::as_const(m_folders)) {
        const QDir dir(folder);
        const QFileInfoList files = dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files | QDir::Readable, QDir::Name);
        for (const QFileInfo &info : files) {
            QFile file(info.absoluteFilePath());
            // A description is a few kilobytes; anything far bigger isn't one.
            if (info.size() > 256 * 1024 || !file.open(QIODevice::ReadOnly)) {
                m_problems.append(QStringLiteral("%1: not read").arg(info.fileName()));
                continue;
            }
            QString problem;
            const QList<Tool> found = parse(file.readAll(), &problem);
            if (!problem.isEmpty()) {
                m_problems.append(QStringLiteral("%1: %2").arg(info.fileName(), problem));
                continue;
            }
            if (found.isEmpty())
                continue;
            // The person's own folder comes first, and the first file for an
            // app is the one that counts.
            const QString app = found.first().app;
            if (apps.contains(app))
                continue;
            apps.insert(app);
            for (const Tool &tool : found) {
                if (names.contains(tool.name))
                    continue;
                names.insert(tool.name);
                m_tools.append(tool);
            }
        }
    }
}

std::optional<Tool> Capabilities::tool(const QString &name) const
{
    for (const Tool &tool : m_tools) {
        if (tool.name == name)
            return tool;
    }
    return std::nullopt;
}

QList<Tool> Capabilities::parse(const QByteArray &text, QString *problem)
{
    const auto fail = [problem](const QString &why) {
        if (problem)
            *problem = why;
        return QList<Tool>();
    };
    QJsonParseError error;
    const QJsonObject file = QJsonDocument::fromJson(text, &error).object();
    if (error.error != QJsonParseError::NoError)
        return fail(QStringLiteral("not JSON: %1").arg(error.errorString()));
    if (file.value(QStringLiteral("version")).toInt(-1) != ContractVersion)
        return fail(QStringLiteral("version %1 isn't one this reads").arg(file.value(QStringLiteral("version")).toInt(-1)));
    const QString app = file.value(QStringLiteral("app")).toString();
    if (!isName(app))
        return fail(QStringLiteral("no app name"));
    const QJsonObject bus = file.value(QStringLiteral("bus")).toObject();
    const QString service = bus.value(QStringLiteral("service")).toString();
    const QString path = bus.value(QStringLiteral("path")).toString();
    const QString interface = bus.value(QStringLiteral("interface")).toString();
    if (service.isEmpty() || !path.startsWith(QLatin1Char('/')) || interface.isEmpty())
        return fail(QStringLiteral("no service, path and interface on the bus"));

    QList<Tool> tools;
    for (const QJsonValue &value : file.value(QStringLiteral("tools")).toArray()) {
        const QJsonObject t = value.toObject();
        Tool tool;
        tool.name = t.value(QStringLiteral("name")).toString();
        tool.app = app;
        tool.appName = file.value(QStringLiteral("name")).toString(app).simplified();
        tool.service = service;
        tool.path = path;
        tool.interface = interface;
        tool.method = t.value(QStringLiteral("method")).toString();
        tool.risk = t.value(QStringLiteral("risk")).toString();
        tool.codex = t.value(QStringLiteral("codex")).toBool(false);
        tool.summary = t.value(QStringLiteral("summary")).toString().simplified();
        // Each tool is named for its own app, so one app can't stand in for
        // another's.
        if (!tool.name.startsWith(app + QLatin1Char('.')) || !isName(tool.name.mid(app.size() + 1)))
            return fail(QStringLiteral("a tool named \"%1\", not \"%2.something\"").arg(tool.name, app));
        if (!isName(tool.method))
            return fail(QStringLiteral("%1 names no method").arg(tool.name));
        if (!Risks.contains(tool.risk))
            return fail(QStringLiteral("%1 has no risk the contract knows").arg(tool.name));
        QSet<QString> seen;
        for (const QJsonValue &a : t.value(QStringLiteral("args")).toArray()) {
            const QJsonObject arg = a.toObject();
            Argument argument;
            argument.name = arg.value(QStringLiteral("name")).toString();
            argument.type = arg.value(QStringLiteral("type")).toString();
            argument.required = arg.value(QStringLiteral("required")).toBool(false);
            argument.fallback = arg.value(QStringLiteral("default"));
            argument.maxLength = arg.value(QStringLiteral("maxLength")).toInt(-1);
            if (arg.contains(QStringLiteral("min")))
                argument.min = arg.value(QStringLiteral("min")).toInteger();
            if (arg.contains(QStringLiteral("max")))
                argument.max = arg.value(QStringLiteral("max")).toInteger();
            argument.pattern = arg.value(QStringLiteral("pattern")).toString();
            if (!isName(argument.name) || seen.contains(argument.name))
                return fail(QStringLiteral("%1 has an argument with no name of its own").arg(tool.name));
            if (!Types.contains(argument.type))
                return fail(QStringLiteral("%1's %2 has a type the contract doesn't know").arg(tool.name, argument.name));
            if (!argument.pattern.isEmpty() && !QRegularExpression(argument.pattern).isValid())
                return fail(QStringLiteral("%1's %2 has a pattern that isn't one").arg(tool.name, argument.name));
            seen.insert(argument.name);
            tool.arguments.append(argument);
        }
        tools.append(tool);
    }
    if (problem)
        problem->clear();
    return tools;
}

QString Capabilities::check(const Tool &tool, const QVariantMap &arguments, QVariantList *positional)
{
    for (auto it = arguments.cbegin(); it != arguments.cend(); ++it) {
        const bool known = std::any_of(tool.arguments.cbegin(), tool.arguments.cend(),
                                       [&it](const Argument &a) { return a.name == it.key(); });
        if (!known)
            return QStringLiteral("%1 takes no \"%2\".").arg(tool.name, it.key().left(40));
    }
    QVariantList values;
    for (const Argument &argument : tool.arguments) {
        QVariant value = arguments.value(argument.name);
        if (!arguments.contains(argument.name)) {
            if (argument.required)
                return QStringLiteral("%1 needs \"%2\".").arg(tool.name, argument.name);
            value = argument.fallback.isUndefined() || argument.fallback.isNull() ? QVariant() : argument.fallback.toVariant();
            if (!value.isValid()) {
                // No default: the type's own empty value.
                if (argument.type == QLatin1String("s"))
                    value = QString();
                else if (argument.type == QLatin1String("b"))
                    value = false;
                else
                    value = 0;
            }
        }
        if (argument.type == QLatin1String("s")) {
            if (value.typeId() != QMetaType::QString)
                return QStringLiteral("%1's \"%2\" must be words.").arg(tool.name, argument.name);
            const QString text = value.toString();
            if (argument.maxLength >= 0 && text.size() > argument.maxLength)
                return QStringLiteral("%1's \"%2\" is longer than %3 characters.").arg(tool.name, argument.name).arg(argument.maxLength);
            if (!argument.pattern.isEmpty() && !QRegularExpression(endsAtTheEnd(argument.pattern)).match(text).hasMatch())
                return QStringLiteral("%1's \"%2\" isn't in the form it takes.").arg(tool.name, argument.name);
            values.append(text);
        } else if (argument.type == QLatin1String("b")) {
            if (value.typeId() != QMetaType::Bool)
                return QStringLiteral("%1's \"%2\" must be true or false.").arg(tool.name, argument.name);
            values.append(value.toBool());
        } else {
            const std::optional<qint64> number = wholeNumber(value);
            const bool unsignedType = argument.type == QLatin1String("u");
            const qint64 low = unsignedType ? 0 : std::numeric_limits<int>::min();
            const qint64 high = unsignedType ? qint64(std::numeric_limits<uint>::max()) : std::numeric_limits<int>::max();
            if (!number || *number < low || *number > high)
                return QStringLiteral("%1's \"%2\" must be a whole number.").arg(tool.name, argument.name);
            if ((argument.min && *number < *argument.min) || (argument.max && *number > *argument.max))
                return QStringLiteral("%1's \"%2\" is out of range.").arg(tool.name, argument.name);
            values.append(unsignedType ? QVariant(uint(*number)) : QVariant(int(*number)));
        }
    }
    if (positional)
        *positional = values;
    return {};
}

} // namespace splitrock::tools
