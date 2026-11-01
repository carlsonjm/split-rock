// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QString>
#include <QStringList>

namespace splitrock::core {

// The app store as this computer has it: Discover, the store's window;
// Flatpak, which runs the apps; and Flathub, where they come from. A computer
// missing any one of them has no app store to speak of: Discover without
// Flathub is an empty store, and Flathub without Discover has no window.
//
// Read from the files alone, so asking costs nothing and runs nothing.
struct AppStore {
    bool discover = false;
    bool flatpak = false;
    bool flathub = false;
    bool missing() const { return !discover || !flatpak || !flathub; }

    // configs: Flatpak's repository settings, for everyone and for the
    // person; a remote named flathub in either counts.
    static AppStore look(const QStringList &configs = defaultConfigs())
    {
        AppStore store;
        store.discover = !QStandardPaths::findExecutable(QStringLiteral("plasma-discover")).isEmpty();
        store.flatpak = !QStandardPaths::findExecutable(QStringLiteral("flatpak")).isEmpty();
        for (const QString &path : configs) {
            QFile file(path);
            if (file.open(QIODevice::ReadOnly | QIODevice::Text) && file.readAll().contains("[remote \"flathub\"]")) {
                store.flathub = true;
                break;
            }
        }
        return store;
    }

    static QStringList defaultConfigs()
    {
        return {QStringLiteral("/var/lib/flatpak/repo/config"),
                QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/flatpak/repo/config")};
    }
};

} // namespace splitrock::core
