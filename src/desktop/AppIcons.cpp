// SPDX-License-Identifier: GPL-2.0-or-later
#include "desktop/AppIcons.h"

#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QPainter>
#include <QUrl>

namespace splitrock {

namespace {

// The folders under root that path, with "*" for any one folder, reaches.
QStringList expand(const QString &pattern)
{
    QStringList found{QStringLiteral("/")};
    for (const QString &part : pattern.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        QStringList next;
        for (const QString &base : std::as_const(found)) {
            if (part == QLatin1String("*")) {
                for (const QString &child : QDir(base).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
                    next.append(QDir(base).filePath(child));
            } else if (QFileInfo(QDir(base).filePath(part)).isDir()) {
                next.append(QDir(base).filePath(part));
            }
        }
        found = next;
    }
    return found;
}

} // namespace

AppIconProvider::AppIconProvider(const QStringList &catalogues)
    : QQuickImageProvider(QQuickImageProvider::Pixmap)
    , m_catalogues(catalogues.isEmpty() ? systemCatalogues() : catalogues)
{
}

QStringList AppIconProvider::systemCatalogues()
{
    // Flathub's catalogue, for the system and the person, and the system's
    // own (archlinux-appstream-data); the larger pictures first.
    QStringList folders;
    const QString home = QDir::homePath();
    for (const char *size : {"128x128", "64x64"}) {
        const QString s = QString::fromLatin1(size);
        folders += expand(QStringLiteral("var/lib/flatpak/appstream/*/*/active/icons/") + s);
        folders += expand(home.mid(1) + QStringLiteral("/.local/share/flatpak/appstream/*/*/active/icons/") + s);
        folders += expand(QStringLiteral("usr/share/swcatalog/icons/*/") + s);
        folders += expand(QStringLiteral("usr/share/app-info/icons/*/") + s);
    }
    return folders;
}

QString AppIconProvider::cataloguePicture(const QString &name)
{
    if (!m_indexed) {
        m_indexed = true;
        // "org.kde.okular.png" in Flathub's; "okular_org.kde.okular.png", the
        // package first, in the system's.
        for (const QString &folder : std::as_const(m_catalogues)) {
            for (const QString &file : QDir(folder).entryList({QStringLiteral("*.png")}, QDir::Files)) {
                const QString stem = file.chopped(4);
                const QString path = QDir(folder).filePath(file);
                if (!m_index.contains(stem))
                    m_index.insert(stem, path);
                const qsizetype under = stem.indexOf(QLatin1Char('_'));
                if (under > 0 && !m_index.contains(stem.mid(under + 1)))
                    m_index.insert(stem.mid(under + 1), path);
            }
        }
    }
    QString key = name;
    if (key.endsWith(QLatin1String(".png")) || key.endsWith(QLatin1String(".svg")))
        key.chop(4);
    return m_index.value(key);
}

QPixmap AppIconProvider::requestPixmap(const QString &id, QSize *size, const QSize &requestedSize)
{
    const QSize want = requestedSize.isValid() && !requestedSize.isEmpty() ? requestedSize : QSize(64, 64);
    QPixmap picture;
    for (const QString &name : QUrl::fromPercentEncoding(id.toUtf8()).split(QLatin1Char('|'), Qt::SkipEmptyParts)) {
        if (name.startsWith(QLatin1Char('/'))) {
            if (QFileInfo(name).isFile())
                picture = QIcon(name).pixmap(want);
        } else if (!name.contains(QLatin1Char('/'))) {
            QString themed = name;
            if (themed.endsWith(QLatin1String(".png")) || themed.endsWith(QLatin1String(".svg")))
                themed.chop(4);
            if (QIcon::hasThemeIcon(themed))
                picture = QIcon::fromTheme(themed).pixmap(want);
            else if (const QString file = cataloguePicture(name); !file.isEmpty())
                picture = QIcon(file).pixmap(want);
        }
        if (!picture.isNull())
            break;
    }
    if (picture.isNull()) {
        picture = QPixmap(1, 1);
        picture.fill(Qt::transparent);
    }
    if (size)
        *size = picture.size();
    return picture;
}

} // namespace splitrock
