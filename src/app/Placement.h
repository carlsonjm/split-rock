// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QMetaObject>
#include <QQuickWindow>

namespace splitrock {

// Opens Genie's window over the work: across the room the panels leave, with
// its card centred on it. It is an ordinary window, not a surface of the
// desktop's own as Gooseberry's card is, so that the browser an assistant's
// sign-in opens comes in front of it; the widget in Milestone 1 settles where
// it lives on the desktop.
inline void openOverWork(QQuickWindow *window)
{
    window->showMaximized();
    window->requestActivate();
    QMetaObject::invokeMethod(window, "open");
}

} // namespace splitrock
