// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QObject>
#include <QStringList>

#include <functional>

namespace splitrock {

// How a second start of Split Rock hands its arguments to the one running and
// ends: opening Split Rock again brings up the same conversation. Internal to
// Split Rock; the conversation's own interface is ConversationService.
class Entry : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.carlsonjm.SplitRock.Application")

public:
    static constexpr char Path[] = "/Application";
    static constexpr char Interface[] = "io.github.carlsonjm.SplitRock.Application";

    using Handle = std::function<void(const QStringList &arguments)>;
    explicit Entry(Handle handle, QObject *parent = nullptr)
        : QObject(parent)
        , m_handle(std::move(handle))
    {
    }

public Q_SLOTS:
    Q_SCRIPTABLE void CommandLine(const QStringList &arguments) { m_handle(arguments); }

private:
    Handle m_handle;
};

} // namespace splitrock
