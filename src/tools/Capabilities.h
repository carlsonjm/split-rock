// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <optional>

namespace splitrock::tools {

// One argument of a tool, as its app's description file gives it.
struct Argument {
    QString name;
    // A D-Bus signature letter: s, u, i or b.
    QString type;
    bool required = false;
    QJsonValue fallback;
    // -1 when the file sets no limit.
    int maxLength = -1;
    std::optional<qint64> min;
    std::optional<qint64> max;
    QString pattern;
};

// One tool an app offers (docs/BROKER.md § Tools): what it is called, which
// of the app's methods on the bus it calls, and how much it may do.
struct Tool {
    // "notes.search": the app's name, a dot and the tool's own.
    QString name;
    QString app;
    // The app as the person knows it: "Notes".
    QString appName;
    QString service;
    QString path;
    QString interface;
    QString method;
    // read, write or outward.
    QString risk;
    // Whether what it gives may go to Codex.
    bool codex = false;
    QString summary;
    QList<Argument> arguments;

    // As Tools() gives it on the bus, and as the front desk and Codex's
    // adapter read it.
    QVariantMap toMap() const;
    static Tool fromMap(const QVariantMap &map);
    // Its arguments as a JSON Schema object: what the model's enforced form
    // allows, and what Codex is shown.
    QJsonObject argumentShape() const;
};

// The tools every installed app describes in its file, read from the folders
// the capabilities contract names (docs/BROKER.md § Tools). An app's file is
// its label, read whether or not the app is running; uninstalling the app
// removes the file, and its tools go with it on the next read.
class Capabilities
{
public:
    // Where the files are, first wins by app: the person's own data folder,
    // then each of the system's.
    static QStringList defaultFolders();

    explicit Capabilities(const QStringList &folders = defaultFolders());

    // Reads every file again. A file that doesn't fit the contract is left
    // out whole, and says why in problems().
    void read();
    QList<Tool> tools() const { return m_tools; }
    std::optional<Tool> tool(const QString &name) const;
    QStringList problems() const { return m_problems; }

    // A tool from one description file's text, or why it can't be used.
    static QList<Tool> parse(const QByteArray &file, QString *problem);

    // Checks arguments against the tool's file. Empty when they fit, with
    // positional set to the method's arguments in order, defaults filled in
    // and each of the right type; otherwise why not, in plain words.
    // Anything the file doesn't name is refused.
    static QString check(const Tool &tool, const QVariantMap &arguments, QVariantList *positional = nullptr);

private:
    QStringList m_folders;
    QList<Tool> m_tools;
    QStringList m_problems;
};

// What an app's tool gave, as the model is given it: set apart in
// <tool_result> marks the text can't close early, as material, never
// instructions.
QString toolResultText(const QString &tool, const QVariant &result);

// The names of what a tool read, as the answer names them: each title it
// gave.
QStringList readNames(const QVariant &result);

// A value from the bus as plain Qt values all the way down: dictionaries and
// lists nested in a reply arrive wrapped, as QDBusArgument.
QVariant fromBus(const QVariant &value);

} // namespace splitrock::tools
