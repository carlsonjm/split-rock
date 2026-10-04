// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QRect>
#include <QString>

// What Split Rock reads from Kadunce, Shuffle's card stage, when it is there:
// its replies, as Kadunce's docs/TETTEGOUCHE-CONTEXT.md defines them. Nothing
// here talks to it; the shell does that.
namespace splitrock::kadunce {

inline constexpr char Service[] = "org.kde.KWin";
inline constexpr char Path[] = "/Kadunce";
inline constexpr char Interface[] = "studio.warbler.Kadunce";
// The companion guest protocol this build speaks.
inline constexpr int CompanionProtocol = 1;

// A reply to beginCompanionGuest. Rectangles are in the desktop's own
// coordinates, across every screen.
struct GuestPlace {
    bool accepted = false;
    QString output;
    QRect card;
    QRect active;
};
GuestPlace parseBegin(const QString &reply);

// How the card stage presents the work: cardLine (Spread), active, bento,
// desktop or inactive. Empty for a reply that is not Kadunce's workspace
// context, version 1.
QString presentation(const QString &context);

// Whether the card Kadunce has selected, presented Active, is a window of the
// application with this desktop identity.
bool selectedIsApplication(const QString &context, const QString &appId);

} // namespace splitrock::kadunce
