# Core

Core is Split Rock's front desk: a small background program with no window,
`split-rock-core`, one for each person. It looks at the computer on a schedule,
writes what it saw in a logbook the person can read, and tells the desktop's
surfaces when a fact deserves attention. Search, the island and the ticker show
what it says; the upkeep cards do the work, after the person's yes. The
conversation is a separate part and Core never needs it.

Its topics are Storage and Updates. Each repeats the same pattern: what Core
watches, the card it can open, and the words it uses.

## What holds

- It runs as the person, never as the administrator.
- It never calls a language model, and works with no model installed. What it
  says is fixed wording ("Storage · 91% full").
- It never changes the computer. The most it does is open an upkeep card, where
  the person decides, as every card already asks.
- Everything it keeps is a plain file in `split-rock/core` in the person's data
  folder, cut back to the last 90 days:
  - `logbook.txt`: what it saw and said, and what the person did, one line each,
    by time
  - `storage.tsv`: its looks at the drive: time, size and free space in
    kilobytes
  - `attention.ini`: what it has said, so it never says it twice, and its
    last look for updates
- Core as a whole, or any one topic, is switched off in `split-rock-corerc` in
  the person's settings folder (`[General] Enabled=false`, or
  `[Storage] Enabled=false`, `[Updates] Enabled=false`). A topic switched off
  is neither watched nor offered.
- Core starts at sign-in, from `io.github.carlsonjm.splitrock.Core.desktop` in
  the system's autostart folder, so it keeps its schedule whether or not a
  surface has asked for it yet. A second start ends at once.
- Where systemd runs the person's services, Core runs as
  `split-rock-core.service`, part of the signed-in desktop, whether sign-in or
  the session bus started it: signing out stops it, so the next sign-in runs
  the program as installed then. `install.sh` also replaces a running Core
  with the one it just installed.
- Every surface works as it always has without Core. A surface finds Core on the
  session bus or carries on without it.

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

Core looks for updates two minutes after it starts, every 6 hours after, and
two minutes after pacman's log stops changing, so a finished update is seen at
once. It counts the system's updates with `checkupdates`, which fetches the
package lists into a folder of its own, adds Flatpak apps with updates, and
reads the time of the last full update from pacman's log. A computer without
`checkupdates` has no Updates fact. A look that fails, such as with no
network, leaves the last fact standing.

| Level | When | Where it shows |
| --- | --- | --- |
| Quiet | Nothing waiting, or updates waiting under 7 days after the last full update | Search only, when asked |
| Ticker | Updates waiting, 7 days or more since the last full update | "Updates · 12 waiting", "Last updated 9 days ago". Tapping it opens Check for updates |
| Island | Updates waiting, 21 days or more since the last full update | Search's island. Tapping it opens Check for updates |

The step is a week: Updates speaks again only a week further behind, and Not
now quiets it for a week. Nothing waiting is settled. Without a record of the
last update, the days count from when Core first saw updates waiting.

## Notice

One setting moves every topic's marks together: `[General] Notice` in
`split-rock-corerc`, which Shuffle Settings shows as Reminders (Less, Balanced,
More). Core feels a change as soon as the file is saved.

| Notice | Storage: ticker, island | Updates: ticker, island |
| --- | --- | --- |
| `quiet` | 85%, 95% | 21 days, 45 days |
| `balanced` (the default) | 75%, 90% | 7 days, 21 days |
| `eager` | 70%, 85% | 3 days, 10 days |

## How often Core speaks

- A fact speaks only when it gets worse, never because time passed: once at the
  ticker, once at the island, then again only after it is a step worse (3
  points fuller; a week further behind).
- Not now, or dismissing the island, quiets the fact until it is a step worse.
- At most one island a day for each topic; each keeps its own allowance, so
  one topic never silences another. Past that, the ticker carries it.
- Do Not Disturb leaves only the ticker.
- An island nobody touches closes after a day, and the logbook says it went
  unanswered.
- When the drive falls 2 points under the ticker's mark, or nothing is left to
  update, the fact has settled: what was said is forgotten, so the next time it
  gets worse, Core speaks again.

The logbook records Core's first look after each start, every time it spoke,
and whether the person opened the card, said not now, or left it, so real use
can set these limits. Below the ticker's mark it writes nothing more: a quiet
drive gives it nothing to say.

## On the bus

- **Service:** `io.github.carlsonjm.splitrock.Core`, owned by the one running
  Core. A call before Core has started starts it, from its bus service file.
- **Object:** `/Core`. **Interface:** `io.github.carlsonjm.SplitRock.Core`.
- **Version:** `ProtocolVersion()` returns `1`, and changes on the same terms
  as the conversation's (`DESKTOP.md`).

| Method or signal | What it does |
| --- | --- |
| `ProtocolVersion() → u` | The version, `1` |
| `Facts() → aa{sv}` | Every fact Core has now; none for a topic switched off |
| `Act(s topic, s action) → b` | The person answered a fact: `open` opens its card, `not-now` and `dismiss` quiet it. False for any other topic or action |
| `Actions() → aa{sv}` | The upkeep cards Search can open by their words: `id`, `title`, `sub`, `icon` and `keywords` (`as`) each |
| `OpenAction(s id, s query) → b` | Opens that card on its own. `query` goes only to Install an app, which searches for it, or removes an app when it starts with "uninstall" or "remove". False for an id not listed |
| `FactChanged(a{sv})` | A fact changed: its words, its level or whether it wants the island |

Each fact is a dictionary of strings to variants:

| Key | Type | What it is |
| --- | --- | --- |
| `topic` | `s` | `storage` or `updates` |
| `title` | `s` | `Storage`, `Updates` |
| `line` | `s` | `91% full`, `12 waiting`, `Up to date` |
| `detail` | `s` | `45 GB free · filling about 2 GB a day` (the rate only when it is at least 100 MB a day), `Last updated 9 days ago` |
| `percent` | `i` | Storage only: how full, in whole points |
| `waiting`, `daysBehind` | `i` | Updates only: how many, and days since the last full update |
| `level` | `s` | `quiet`, `ticker` or `island` |
| `attention` | `s` | `island` while the island should show it, `none` otherwise |
| `icon` | `s` | An icon name from the icon theme |
| `actionText` | `s` | What the card is called: `Free up space`, `Check for updates` |
| `keywords` | `as` | The words Search finds it by |

## Actions from Search

Every upkeep card opens from Search on its own, with no conversation: Check
for updates, Free up space, Install an app, Health check, What changed, Restore
points and Community apps. Core lists them with the words each is found by,
and Search matches what is typed against them without asking Core anything.
Enter on one asks Core to open it. Each card looks first and changes nothing
until the person's yes, as it always has. The logbook records each one opened.

## Opening a card

Core opens a card by starting `split-rock --card space`, or
`split-rock --card install --query "pdf editor"` for the card that takes words. Split Rock then opens
that one upkeep card on its own, in an ordinary window titled after the card,
with no conversation and no assistant started. The window closes when the card
does, and can't be closed while the card is busy. A card that hands over to
another, as Health check does to Free up space, keeps the window: the next card
opens in it.
