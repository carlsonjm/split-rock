# Core

Core is Split Rock's front desk: a small background program with no window,
`split-rock-core`, one for each person. It looks at the computer on a schedule,
writes what it saw in a logbook the person can read, and tells the desktop's
surfaces when a fact deserves attention. Search, the island and the ticker show
what it says; the upkeep cards do the work, after the person's yes. Core works
without the conversation.

Core watches three topics, Storage, Updates and Health, the Downloads
folder for a Windows program and for how much has piled up there, new
devices, and what came in during Do Not Disturb. Each section below gives what
Core watches, the card it can open and the words it uses.

## What holds

- It runs as the person, never as the administrator.
- It calls no language model and works with none installed. What it says is
  fixed wording ("Storage · 91% full"). Ask, which does answer with a model,
  is a program of its own beside it (`ASK.md`).
- It changes nothing on the computer. The most it does is open an upkeep card,
  where the person decides.
- Everything it keeps is a plain file in `split-rock/core` in the person's data
  folder, cut back to the last 90 days:
  - `logbook.txt`: what it saw and said, and what the person did, one line each,
    by time
  - `storage.tsv`: its looks at the drive: time, size and free space in
    kilobytes
  - `attention.ini`: what it has said, so it doesn't say it twice, and its
    last looks for updates and at the computer's health, and the devices
    it knows
- `split-rock-corerc` in the person's settings folder switches off Core as a
  whole (`[General] Enabled=false`) or one topic (`[Storage] Enabled=false`,
  `[Updates] Enabled=false`, `[Health] Enabled=false`, `[Windows] Enabled=false`,
  `[Tidy] Enabled=false`, `[Devices] Enabled=false`, `[Focus] Enabled=false`). Core neither watches nor offers a topic that is
  switched off.
- Core starts at sign-in, from `io.github.carlsonjm.splitrock.Core.desktop` in
  the system's autostart folder, so it keeps its schedule whether or not a
  surface has asked for it yet. A second start ends at once.
- Where systemd runs the person's services, Core runs as
  `split-rock-core.service` inside the signed-in desktop, whether sign-in or
  the session bus started it. Signing out stops it, so the next sign-in starts
  whichever version is installed then. `install.sh` also replaces a running Core
  with the one it just installed.
- Every surface works without Core. A surface looks for Core on the session bus
  and carries on if it isn't there.

## Storage

Core looks at the drive that holds the home folder every 30 minutes, which costs
one call to the file system. From the last week of looks it works out how fast
the drive is filling.

| Level | When | Where it shows |
| --- | --- | --- |
| Quiet | Under 75% full, and not filling fast | Search only, when asked |
| Ticker | 75% full or more | One ordinary notification: "Storage · 76% full", "120 GB free". Tapping it opens Free up space |
| Island | 90% full or more, or at least 80% full and full within 14 days at the current rate | Search's island. Tapping it opens Free up space |

## Updates

Core looks for updates two minutes after it starts and every 6 hours after
that. It also looks two minutes after pacman's log stops changing, or after the
mark a Flatpak installation (the system's or the person's own) sets on each
install and update stops changing, so it sees a finished update at once. It
counts the system's updates with `checkupdates`, which fetches the package
lists into a folder of its own, adds Flatpak apps with updates, and reads the
time of the last full update from pacman's log. A computer without
`checkupdates` has no Updates fact. A look that fails, such as with no
network, leaves the last fact standing.

| Level | When | Where it shows |
| --- | --- | --- |
| Quiet | Nothing waiting, or updates waiting under 7 days after the last full update | Search only, when asked |
| Ticker | Updates waiting, 7 days or more since the last full update | "Updates · 12 waiting", "Last updated 9 days ago". Tapping it opens Check for updates |
| Island | Updates waiting, 21 days or more since the last full update | Search's island. Tapping it opens Check for updates |

The step is a week: Updates speaks again only a week further behind, and Not
now quiets it for a week. Nothing waiting counts as settled. Without a record
of the last update, the days count from when Core first saw updates waiting.

## Health

Core looks over the computer once a week, counted from its last look, so a
computer that was off or asleep looks ten minutes after Core starts. It runs
`split-rock --health-facts`, the Health check's own look, which only reads and
changes nothing, not even the last look the card keeps. Of what it finds, Core
keeps only the problems; space, updates and old copies of updates are left to
Storage and Updates. A look that fails leaves the last fact standing.

| Level | When | Where it shows |
| --- | --- | --- |
| Quiet | No problems | Search only, when asked |
| Ticker | A problem: a service that didn't start, an install that was cut off, a broken list of what's installed | "Health · 1 problem", "Services: One didn't start: …". Tapping it opens Health check |
| Island | The drive reports it is failing, the one problem that can lose files | Search's island. Tapping it opens Health check |

The step is one problem: Health speaks again only when there is a problem
more, and Not now quiets it until then. No problems counts as settled. The
Notice setting does not move Health: a problem is a problem.

A problem no Helper fixes says "Copy for help" on the card. A tap copies the
problem, written as a question, with the computer's maker and model, its
system, its Linux kernel and Plasma's version, to paste in a forum or send to
someone. Nothing is sent anywhere.

## Windows programs

Someone who has used Windows for decades downloads a program the way they
always have, and gets a `.exe` or `.msi` that can't run here. When one arrives
in the Downloads folder, Core says so once, on Search's island:
"ZoomInstaller.exe is for Windows", "Windows programs don't run here. Find the
Linux version of Zoom". Tapping it opens Install an app, searching for the name
in the file's, without the words installers add and their versions. Only a file
that arrives while Core runs counts, five seconds after the folder stops
changing, so a download has finished and been named.

Each installer is news once. A second one the same day, or one during Do Not
Disturb, goes to the ticker. Once the person taps it or sets it aside, it is
no longer a fact. `never` on the bus stops it for good. Core reads only the
names of the files there, never what is in them.

## Tidy up Downloads

With each look at the drive, and when a download has settled, Core adds up
what the Downloads folder and the Desktop hold, folders inside them too. Past
5 GB it says so once on the ticker: "Downloads · 7.4 GB", "Tidy up Downloads".
It speaks again only 5 GB later; when the folders are tidied, the next mark is
5 GB above what is left. During Do Not Disturb it waits. Tapping it opens Tidy
up Downloads. Core reads only the sizes of the files there.

The card groups the files directly in those two folders: installers of apps
already installed and Windows programs, copies the same as their original,
files not opened in 30 days, screenshots, and the folders there, each whole
with its size. Only the first two are ticked;
an AppImage opened in the last month never is. What is ticked goes to the
Trash, where it can be put back, with no password. Free up space ends with a
row that opens it, never ticked.

## Devices

The first time the computer sees a printer, a drive that can be unplugged or
a Bluetooth device once it is paired, Core says so once, at the ticker: "New
printer · Set it up", with the device's own name under it, such as "LaserJet
M110we". Tapping it opens KDE's own settings page for that kind of device:
Printers, Removable Storage or Bluetooth. It never takes the island, has no
fact on the bus, and speaks during Do Not Disturb too, as only the ticker does.

Core reads the system bus and changes nothing there: UDisks2's drives that
say they can be removed, BlueZ's paired devices, and the device units that
want systemd's `printer.target`, which udev's own rules give every printer
it finds. It lists each kind half a minute after it starts, and again a few
seconds after the bus says something about that kind changed. The first time
Core lists a kind, what is already there is known without a word, so only
what arrives afterwards is new. A drive is known by its id, a Bluetooth device
by its address and a printer by its model; Core keeps the last 200 of each.
Where a kind's settings page isn't installed, as on a desktop without Plasma's
printer settings, the logbook alone records the new device.

## Focus

When Do Not Disturb ends, Core says once on Search's island what came in
meanwhile: "While you were focused", "7 notifications · Updates waiting". The
count comes from whatever shows the desktop's notifications, which calls
`FocusEnded` with it; Status does, and on a desktop where nothing calls it
Focus never speaks. "Waiting" names each topic that wanted the island while
Do Not Disturb kept it to the ticker, held for a day at most, and said once.
When nothing came in and nothing was held, Core writes it in the logbook and
says nothing.

Tapping it, See them, asks to show the notifications: Core sends
`OpenNotifications` on the bus, and Status opens its notification history.
With no notifications counted, the tap opens the card of the first topic
held instead. Each time Do Not Disturb ends is news once; the island's daily
allowance leaves a second time the same day to the ticker. Once answered, it
is no longer a fact.

## Notice

One setting moves Storage's and Updates' marks together: `[General] Notice` in
`split-rock-corerc`, a number from 0 (Less) to 100 (More), 50 by default, which
Shuffle Settings shows as the Pings slider on its Helpers page. Core picks up a
change as soon as the file is saved. The words an earlier Settings wrote still
read: `quiet` is 0 and `eager` is 100; anything else is 50.

The marks at the ends and the middle are below; between them each moves in a
straight line, rounded to a whole point or day (25 is 80%, 93% and 14 days, 33
days).

| Notice | Storage: ticker, island | Updates: ticker, island |
| --- | --- | --- |
| 0, Less | 85%, 95% | 21 days, 45 days |
| 50, the default | 75%, 90% | 7 days, 21 days |
| 100, More | 70%, 85% | 3 days, 10 days |

## How often Core speaks

- A fact speaks only when it gets worse, never because time passed: once at the
  ticker, once at the island, then again only after it is a step worse (3
  points fuller; a week further behind; a problem more).
- Not now, or dismissing the island, quiets the fact until it is a step worse.
- At most one island a day for each topic; each keeps its own allowance, so
  one topic never silences another. Past that, the ticker carries it.
- Do Not Disturb leaves only the ticker.
- An island nobody touches closes after a day, and the logbook says it went
  unanswered.
- When the drive falls 2 points under the ticker's mark, nothing is left to
  update, or no problem is left, the fact has settled: what was said is forgotten, so the next time it
  gets worse, Core speaks again.

The logbook records Core's first look after each start, every time it spoke,
and whether the person opened the card, said not now, or left it, so real use
can set these limits. Below the ticker's mark it writes nothing more.

## On the bus

- **Service:** `io.github.carlsonjm.splitrock.Core`, owned by the one running
  Core. A call before Core has started starts it, from its bus service file.
- **Object:** `/Core`. **Interface:** `io.github.carlsonjm.SplitRock.Core`.
- **Version:** `ProtocolVersion()` returns `1`. A caller uses the interface only
  when the number is one it knows. A method or a key added later, which a
  version-1 caller can ignore, keeps the number; any other change raises it.

| Method or signal | What it does |
| --- | --- |
| `ProtocolVersion() → u` | The version, `1` |
| `Facts() → aa{sv}` | Every fact Core has now; none for a topic switched off |
| `Act(s topic, s action) → b` | The person answered a fact: `open` opens its card, `not-now` and `dismiss` quiet it, and `never`, for Windows programs, stops it for good. False for any other topic or action |
| `Actions() → aa{sv}` | The upkeep cards Search can open by their words: `id`, `title`, `sub`, `icon` and `keywords` (`as`) each |
| `OpenAction(s id, s query) → b` | Opens that card on its own. `query` goes only to Install an app, which searches for it, or removes an app when it starts with "uninstall" or "remove". False for an id not listed |
| `RemoveApp(s desktopId) → b` | Opens Install an app straight to removing the app with that desktop file id, such as `org.videolan.vlc.desktop`: it finds the package or Flatpak that owns it and waits for the person's yes. False for anything not shaped like a desktop file id |
| `FocusEnded(u notifications)` | Do Not Disturb ended, and this many notifications came in meanwhile; whatever shows the notifications calls it |
| `FactChanged(a{sv})` | A fact changed: its words, its level or whether it wants the island |
| `OpenNotifications()` | The person asked to see what came in during Do Not Disturb; whatever keeps the notifications opens them |

`Act("devices", "open")` opens the settings page for the kind of device Core
spoke of last, as a tap on its ticker line does.

Each fact is a dictionary of strings to variants:

| Key | Type | What it is |
| --- | --- | --- |
| `topic` | `s` | `storage`, `updates`, `health`, `windows` or `focus` (`tidy` speaks only on the ticker and has no fact) |
| `title` | `s` | `Storage`, `Updates`, `Health`, `Windows programs` |
| `line` | `s` | `91% full`, `12 waiting`, `Up to date`, `1 problem`, `All good` |
| `detail` | `s` | `45 GB free · filling about 2 GB a day` (the rate only when it is at least 100 MB a day), `Last updated 9 days ago` |
| `percent` | `i` | Storage only: how full, in whole points |
| `waiting`, `daysBehind` | `i` | Updates only: how many, and days since the last full update |
| `problems` | `i` | Health only: how many problems |
| `notifications` | `i` | Focus only: how many notifications came in during Do Not Disturb |
| `program` | `s` | Windows programs only: the program the installer is for, such as `Zoom`; empty when its name gives none |
| `level` | `s` | `quiet`, `ticker` or `island` |
| `attention` | `s` | `island` while the island should show it, `none` otherwise |
| `icon` | `s` | An icon name from the icon theme |
| `actionText` | `s` | What the card is called: `Free up space`, `Check for updates`, `Health check` |
| `keywords` | `as` | The words Search finds it by |

## Actions from Search

Every upkeep card opens from Search on its own, with no conversation: Check
for updates, Free up space, Tidy up Downloads, Install an app, Your apps,
Health check, What changed, Restore points and Community apps. Core lists them with the words each is found by,
and Search matches what is typed against them without asking Core anything.
Windows's own words find them too: Add or remove programs opens Install an
app, Disk Cleanup Free up space, Windows Update Check for updates and System
Restore Restore points.
Enter on one asks Core to open it. Install an app searches for what was
typed; a Windows installer's name typed in full, such as ZoomInstaller.exe,
searches for its program, Zoom. Each card looks first and changes nothing
until the person says yes. The logbook records each card opened.

Your apps lists the apps the person added themselves, biggest first with the
room each takes: apps in the app menu whose package was installed on purpose,
and Flathub's apps; never what the system runs on. Removing one is Install an
app's removal, with its yes and its restore point, one app and one password
at a time. It never opens on its own: Search finds it, and Free up space ends
with a row that opens it, never ticked, so freeing up space never removes an
app.

## Opening a card

Core opens a card by starting `split-rock --card space`, or
`split-rock --card install --query "pdf editor"` for the card that takes words. Split Rock then
opens that one upkeep card on its own, in an ordinary window titled after the
card, with no conversation and no assistant started. The window closes when the
card does, and can't be closed while the card is busy. A card that hands over to
another, as Health check does to Free up space, keeps the window: the next card
opens in it.
