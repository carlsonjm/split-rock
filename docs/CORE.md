# Core

Core is Split Rock's front desk: a small background program with no window,
`split-rock-core`, one for each person. It looks at the computer on a schedule,
writes what it saw in a logbook the person can read, and tells the desktop when
a fact deserves attention. The Helpers do the work, after the person's yes.

Core speaks in two ways:

- **The ticker:** an ordinary desktop notification. Tapping it opens the
  Helper that fits.
- **The island:** a notice beside Search, the launcher from Tettegouche, which
  reads Core's facts over the session bus. It is for what should not wait.
  Without that launcher, only the ticker is seen.

Core has nine topics, each switched on and off on its own: Storage, Updates,
Health, Windows programs (an installer arriving in Downloads), Tidy up
Downloads (how much has piled up there), Devices, Focus (what came in during
Do Not Disturb), Workspace Memory (bringing back the apps from the last
sign-in) and Automations (opening the apps the person always starts with). Each section below gives what Core watches, the
Helper it can open and the words it uses.

## What holds

- It runs as the person, never as the administrator.
- It calls no language model and works with none installed. What it says is
  fixed wording ("Storage · 91% full"). Ask, which does answer with a model,
  is a program of its own beside it (`ASK.md`).
- It changes nothing on the computer. The most it does is open a Helper, where
  the person decides, or open apps the person said yes to, as Workspace Memory
  does.
- Everything it keeps is a plain file in `split-rock/core` in the person's data
  folder, cut back to the last 90 days:
  - `logbook.txt`: what it saw and said, and what the person did, one line each,
    by time
  - `storage.tsv`: its looks at the drive: time, size and free space in
    kilobytes
  - `attention.ini`: what it has said, so it doesn't say it twice, and its
    last looks for updates and at the computer's health, and the devices
    it knows
  - `settings/`: its copies of the desktop's settings files and what changed
    in them, kept 30 days (Settings copies, below)
- `split-rock-corerc` in the person's settings folder switches off Core as a
  whole (`[General] Enabled=false`) or one topic (`[Storage] Enabled=false`,
  `[Updates] Enabled=false`, `[Health] Enabled=false`,
  `[Windows] Enabled=false`, `[Tidy] Enabled=false`,
  `[Devices] Enabled=false`, `[Focus] Enabled=false`,
  `[WorkspaceMemory] Enabled=false`, `[Automations] Enabled=false`). Core
  neither watches nor offers a topic that is switched off. Core writes to
  this file only what the person chose on one of its islands:
  `[WorkspaceMemory] Always=true`, and each automation they said yes to.
- Core starts at sign-in, from `io.github.carlsonjm.splitrock.Core.desktop` in
  the system's autostart folder, so it keeps its schedule whether or not
  anything has asked for it yet. A second start ends at once.
- Where systemd runs the person's services, Core runs as
  `split-rock-core.service` inside the signed-in desktop, whether sign-in or
  the session bus started it. Signing out stops it, so the next sign-in starts
  whichever version is installed then. `install.sh` also replaces a running Core
  with the one it just installed.
- Nothing that shows Core's notices depends on it: a program looks for Core on
  the session bus and carries on if it isn't there.

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
lists into a folder of its own, `split-rock/checkup-db` in the person's cache
folder, adds Flatpak apps with updates, and reads the time of the last full
update from pacman's log. The Check for updates Helper fetches into the same
folder, so each fetch leaves the other only what changed since, and the
Helper opens on the list the last fetch left at once, fetching nothing, then
puts it right in place when its own fresh look comes back. Update always
waits for that fresh look before it installs anything. A computer without
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
changes nothing, not even the last look the Health check keeps. Of what it
finds, Core keeps only the problems; space, updates and old copies of updates are left to
Storage and Updates. A look that fails leaves the last fact standing.

| Level | When | Where it shows |
| --- | --- | --- |
| Quiet | No problems | Search only, when asked |
| Ticker | A problem: a service that didn't start, an install that was cut off, a broken list of what's installed | "Health · 1 problem", "Services: One didn't start: …". Tapping it opens Health check |
| Island | The drive reports it is failing, the one problem that can lose files | Search's island. Tapping it opens Health check |

The step is one problem: Health speaks again only when there is a problem
more, and Not now quiets it until then. No problems counts as settled. The
Notice setting does not move Health: a problem is a problem.

The Health check has a Battery row wherever the computer has a battery that
says what it holds: "Holds 87% of its original charge". It
reads the kernel's own power supply folder, and UPower where that folder says
nothing, and leaves out the batteries of a mouse, a pen or a controller. Under
70% of its original charge, or with a fault the battery itself reports, such
as overheating, the row is a problem with Copy for help, and Core's weekly
look speaks of it once on the ticker like any other problem; never the
island. A computer with no battery has no row.

A problem no Helper fixes says "Copy for help" on the Health check. A tap
copies the problem, written as a question, with the computer's maker and
model, its system, its Linux kernel and Plasma's version, to paste in a forum
or send to someone. Nothing is sent anywhere.

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

The Helper groups the files directly in those two folders: installers of apps
already installed and Windows programs, copies the same as their original,
files not opened in 30 days, screenshots, and the folders there, each whole
with its size. Only the first two are ticked; an AppImage opened in the last
month never is. What is ticked goes to the
Trash, where it can be put back, with no password. Free up space ends with a
row that opens it, never ticked.

## Devices

The first time the computer sees a printer, a drive that can be unplugged,
a Bluetooth device once it is paired, a game controller or a screen, Core
says so once, at the ticker: "New printer · Set it up", with the device's own
name under it, such as "LaserJet M110we". Tapping it opens KDE's own settings
page for that kind of device: Printers, Removable Storage, Bluetooth, Game
Controller or Display Configuration. It never takes the island, has no
fact on the bus, and speaks during Do Not Disturb too, as only the ticker does.

Core reads the system bus and changes nothing there: UDisks2's drives that
say they can be removed, BlueZ's paired devices, and the device units that
want systemd's `printer.target`, which udev's own rules give every printer
it finds. It lists each kind half a minute after it starts, and again a few
seconds after the bus says something about that kind changed. The first time
Core lists a kind, what is already there is known without a word, so only
what arrives afterwards is new. A drive is known by its id, a Bluetooth device
by its address and a printer by its model; Core keeps the last 200 of each.

Game controllers and screens are read from the kernel's own folders instead:
each joystick in `/sys/class/input`, known by its maker's and model's numbers
and its serial, and each connector in `/sys/class/drm` that says it is
connected, known by the maker, model and serial in the screen's EDID and named
by the name the screen gives itself there. Core lists both when a device
comes or goes in `/dev/input`, and every half minute, which costs a few small
reads. The built-in screen is there at the first look, so it is known without
a word.
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

When Do Not Disturb ends inside the notifications themselves, as with its
switch in Status, the person is already looking at what came in: Status
calls `FocusEndedInView`, Core writes it in the logbook, and there is no
island. Status marks what came in there instead. Whatever Core held went to
the ticker, so it is among them. The island shows only when Do Not Disturb
ends somewhere else, by a shortcut or its timer, and opening the
notifications any way at all counts as seeing them: Status calls
`NotificationsSeen` and the island goes.

Tapping it, See them, asks to show the notifications: Core sends
`OpenNotifications` on the bus, and Status opens its notification history.
With no notifications counted, the tap opens the card of the first topic
held instead. Each time Do Not Disturb ends is news once; the island's daily
allowance leaves a second time the same day to the ticker. Once answered, it
is no longer a fact.

## Workspace Memory

Cards, from Kadunce, remembers at sign-out which apps were open and where each
one stood: its screen, its card, its pair. After sign-in Core asks once, on
Search's island: "Bring back 4 apps from yesterday?", "Each one goes back to
its place", with Bring back, Always and Not now. Bring back asks Cards to
expect them, then launches each app as the app menu would; Cards puts each one
back in its place as its window arrives. Not now leaves the desktop as it is
and tells Cards to stop offering that session. Always brings them back now
and is kept as `[WorkspaceMemory] Always=true`: from the next sign-in Core
asks Cards straight away with no island, and the ticker says "Brought back 4
apps", "As you left them" once they are back, or how many didn't open in
time. Turning Always off in Settings brings the question back.

Core asks Cards twenty seconds after it starts, and twice more forty seconds
apart until Cards answers, on Kadunce's Workspace Memory calls, version 1
(`savedSession`, `bringBack`, `dismissSavedSession`, and the
`bringBackFinished` signal), at `org.kde.KWin`, `/Kadunce`,
`co.goodinput.Kadunce`. Without Cards, or with a version Core doesn't know,
nothing is said. Core launches only the apps Cards names, each once, and stops
waiting 90 seconds after the last one arrived if Cards never says the run is
over.

Each saved session is news once, by when Cards saved it, so a Core started
again in the same session doesn't ask twice. A second sign-in the same day, or
one during Do Not Disturb, goes to the ticker, whose tap brings the apps
back. A session saved more than 14 days ago is not offered. The Notice setting
does not move it.

## Automations

An automation is only ever offered, never made on its own. The first kind
opens a set of apps at sign-in. Cards records which apps the person opens by
hand in the five minutes after the first window of each sign-in, and counts
each set (apps it brought back don't count). Core asks Cards for the sets
seven minutes after it starts and every two hours. Once the same set has
opened together three times, Core offers once, on Search's island.

> **The wording names the apps** (the product owner, 30 October). Every word
> of the offer is in one place, `automationWords` in `src/core/Automations.h`:
> "Open Firefox, Notes and Mail when you sign in?", "You've started with these
> three times", with "Yes, each time" and "Not now". Past three apps the names
> end "and 2 more".

- Yes adds the automation to `split-rock-corerc`, in a group of its own that
  Settings lists with a switch: `[Automation 1]` with `Kind=open-together`,
  `Apps` (desktop file ids), `Names`, `Enabled` and `Added`. Nothing opens
  until the next sign-in.
- Not now, or dismissing it, is for good: that set is never offered again, and
  neither is one already an automation.
- One offer at a time. During Do Not Disturb, or once the topic's island has
  shown today, an offer waits for a later look; it never goes to the ticker,
  where a tap would be a yes to words nobody read.
- At each sign-in, once Workspace Memory has had its answer from Cards, Core
  opens the apps of every automation switched on, each once, but those Always
  is bringing back. It runs once a sign-in, however often Core starts in it.

## Settings copies

What changed says a settings change as it was made, "Built-in screen: scale
175% to 150%", and puts it back, so Core keeps its own copy of each settings
file What changed reads: the screens, keyboard shortcuts, windows and effects,
window rules, the look, panels and widgets, pointer and touchpad, keyboard
layouts, power, screen lock, Bluetooth, notifications, default apps, fonts,
file search, accessibility, the clock and the language. It looks at them when
one is written, five seconds after the last write, and on its usual schedule.

- A key whose value differs from the copy is one change, kept with the time
  and its value before and after in `settings/changes.jsonl`; then the copy is
  brought up to date. The first look only takes the copies, so a change made
  before Core began keeping them can only be named by its file, as before.
- The desktop's own keeping is left out: a file written again with the same
  values, window sizes and places, recent lists, a shortcut's name written
  again, and a panel's every size and place. For panels, adding or removing a
  widget or a panel, and the wallpaper, are changes.
- It reads the settings and writes only its own folder. With Core off as a
  whole, it keeps no copies.

What changed makes one row of the changes in one place within two minutes of
each other, and each row has the way back that is known to work on it:

| Way back | For | What it does |
| --- | --- | --- |
| Put back | A screen's scale, resolution and turn; keyboard shortcuts; windows and effects; window rules; keyboard layouts; power; screen lock; notifications; default apps; the wallpaper | A change from a card of Split Rock's own, after the person's yes, with Undo: KDE's own tools set the values from before and the desktop is told |
| Open in Settings | The look, pointer and touchpad, Bluetooth and the rest | KDE's page for it opens; the row says what it was |
| Copy for help | Panels and widgets | Everything that changed, exactly, with what the computer is |

A setting already back as it was says so on its row and offers nothing.

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
  update, or no problem is left, the fact has settled: what was said is
  forgotten, so the next time it gets worse, Core speaks again.

The logbook records Core's first look after each start, every time it spoke,
and whether the person opened the Helper, said not now, or left it, so real use
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
| `Act(s topic, s action) → b` | The person answered a fact: `open` opens its Helper (for Automations, says yes to the offer), `not-now` and `dismiss` quiet it, `never`, for Windows programs, stops it for good, and `always`, for Workspace Memory, brings the apps back and stops asking. False for any other topic or action |
| `Actions() → aa{sv}` | The Helpers Search can open by their words: `id`, `title`, `sub`, `icon` and `keywords` (`as`) each |
| `OpenAction(s id, s query) → b` | Opens that Helper on its own. `query` goes only to Install an app, which searches for it, or removes an app when it starts with "uninstall" or "remove". False for an id not listed |
| `RemoveApp(s desktopId) → b` | Opens Install an app straight to removing the app with that desktop file id, such as `org.videolan.vlc.desktop`: it finds the package or Flatpak that owns it and waits for the person's yes. False for anything not shaped like a desktop file id |
| `FocusEnded(u notifications)` | Do Not Disturb ended, and this many notifications came in meanwhile; whatever shows the notifications calls it |
| `FocusEndedInView(u notifications)` | The same, ended inside the notifications themselves, where the person already sees what came in: logged, and nothing goes on the island |
| `NotificationsSeen()` | The person opened the notifications, so the island about what came in goes |
| `FactChanged(a{sv})` | A fact changed: its words, its level or whether it wants the island |
| `OpenNotifications()` | The person asked to see what came in during Do Not Disturb; whatever keeps the notifications opens them |

`Act("devices", "open")` opens the settings page for the kind of device Core
spoke of last, as a tap on its ticker line does.

Each fact is a dictionary of strings to variants:

| Key | Type | What it is |
| --- | --- | --- |
| `topic` | `s` | `storage`, `updates`, `health`, `windows`, `focus`, `memory` or `automations` (`tidy` speaks only on the ticker and has no fact) |
| `title` | `s` | `Storage`, `Updates`, `Health`, `Windows programs` |
| `line` | `s` | `91% full`, `12 waiting`, `Up to date`, `1 problem`, `All good` |
| `detail` | `s` | `45 GB free · filling about 2 GB a day` (the rate only when it is at least 100 MB a day), `Last updated 9 days ago` |
| `percent` | `i` | Storage only: how full, in whole points |
| `waiting`, `daysBehind` | `i` | Updates only: how many, and days since the last full update |
| `problems` | `i` | Health only: how many problems |
| `notifications` | `i` | Focus only: how many notifications came in during Do Not Disturb |
| `program` | `s` | Windows programs only: the program the installer is for, such as `Zoom`; empty when its name gives none |
| `apps`, `count` | `as`, `i` | Workspace Memory and Automations only: the apps' names, as the app menu gives them, and how many |
| `dismissText` | `s` | Automations only: what Not now says on the island, "Not now" |
| `choices` | `aa{sv}` | Choices beside the tap and Not now, each an `action` to pass to `Act` and the `text` its button says: Workspace Memory's `always`, "Always". A surface that doesn't show them still offers the tap and Not now |
| `level` | `s` | `quiet`, `ticker` or `island` |
| `attention` | `s` | `island` while the island should show it, `none` otherwise |
| `icon` | `s` | An icon name from the icon theme |
| `actionText` | `s` | What the Helper is called: `Free up space`, `Check for updates`, `Health check` |
| `keywords` | `as` | The words Search finds it by |

## Actions from Search

Every Helper opens from Search on its own: Check for updates, Free up space,
Tidy up Downloads, Install an app, Your apps, Health check, What changed,
Restore points and Community apps. Core lists them with the words each is
found by, and Search matches what is typed against them without asking Core
anything.
Windows's own words find them too: Add or remove programs opens Install an
app, Disk Cleanup Free up space, Windows Update Check for updates and System
Restore Restore points.
Enter on one asks Core to open it. Install an app searches for what was
typed; a Windows installer's name typed in full, such as ZoomInstaller.exe,
searches for its program, Zoom. Each Helper looks first and changes nothing
until the person says yes. The logbook records each Helper opened.

Your apps lists the apps the person added themselves, biggest first with the
room each takes: apps in the app menu whose package was installed on purpose,
and Flathub's apps; never what the system runs on. Removing one is Install an
app's removal, with its yes and its restore point, one app and one password
at a time. It never opens on its own: Search finds it, and Free up space ends
with a row that opens it, never ticked, so freeing up space never removes an
app.

## Opening a Helper

Core opens a Helper by starting `split-rock --card space`, or
`split-rock --card install --query "pdf editor"` for the one that takes words.
Split Rock then opens that one Helper in an ordinary window titled after it.
The window closes when the Helper does, and can't be closed while the Helper is
busy. A Helper that hands over to another, as Health check does to Free up
space, keeps the window: the next Helper opens in it.

The window's title is the Helper's name, then its one fact after a middle dot,
once it has one: `Update · 12 waiting`, `Free up space · 46 GB free`,
`Health check · 2 to look at`. The other Helpers carry their name alone.
Shuffle's top bar names the Helper and shows the fact beside it, lighter, as
it shows any window's own name (the product owner, 30 October; Shuffle's
`docs/CONTEXT-BAR-CONTRACT.md`).
