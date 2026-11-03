// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QHash>
#include <QQuickImageProvider>
#include <QStringList>

namespace splitrock {

// Each app's own icon, for the rows of Install an app and Your apps, as
// "image://appicon/<names>": the names an app's icon may go by, separated by
// "|", best first (AppInstall::App::icons). The first that the icon theme
// has, that is a picture file, or that the app catalogues on this computer
// keep a picture for, is drawn. None found gives a picture one pixel wide,
// and the row keeps its own glyph.
class AppIconProvider : public QQuickImageProvider
{
public:
    // Where the app catalogues keep their pictures; empty for the system's own.
    explicit AppIconProvider(const QStringList &catalogues = {});

    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) override;

    // The picture file the catalogues keep for one name, or empty.
    QString cataloguePicture(const QString &name);

    static QStringList systemCatalogues();

private:
    QStringList m_catalogues;
    bool m_indexed = false;
    QHash<QString, QString> m_index;
};

} // namespace splitrock
