// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/Windows.h"

#include <QRegularExpression>
#include <QStringList>

namespace splitrock::core {

bool isWindowsInstaller(const QString &fileName)
{
    return fileName.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive)
        || fileName.endsWith(QLatin1String(".msi"), Qt::CaseInsensitive);
}

QString programName(const QString &fileName)
{
    QString base = fileName.section(QLatin1Char('/'), -1);
    base.chop(base.size() - base.lastIndexOf(QLatin1Char('.')));
    // A browser's second copy: "ZoomInstaller (1)".
    base.remove(QRegularExpression(QStringLiteral(R"(\s*\(\d+\)$)")));
    // Words run together: "ZoomInstaller", "ChromeSetup".
    base.replace(QRegularExpression(QStringLiteral("([a-z])(Installer|Setup|Install|Web|Offline|Online)")),
                 QStringLiteral("\\1 \\2"));
    static const QRegularExpression extra(QStringLiteral(
        "^(installer|setup|install|win(32|64)?|windows|x64|x86|amd64|arm64|64bit|32bit|full|offline|online|web|stub|"
        "latest|release|portable|v?\\d+([.\\d]*\\d)?[a-z]?)$"),
        QRegularExpression::CaseInsensitiveOption);
    QStringList kept;
    for (const QString &word : base.split(QRegularExpression(QStringLiteral("[\\s_.-]+")), Qt::SkipEmptyParts)) {
        if (!extra.match(word).hasMatch())
            kept.append(word);
    }
    // The first words name it; what follows is often a channel or a build.
    QString name = kept.mid(0, 3).join(QLatin1Char(' '));
    return name.left(40).trimmed();
}

} // namespace splitrock::core
