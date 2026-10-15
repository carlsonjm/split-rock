# Core

Core is Split Rock's front desk: a small background program with no window,
`split-rock-core`, one for each person. It looks at the computer on a schedule,
writes what it saw in a logbook the person can read, and tells the desktop's
surfaces when a fact deserves attention. Search, the island and the ticker show
what it says; the upkeep cards do the work, after the person's yes. The
conversation is a separate part and Core never needs it.

Its first topic is Storage. Each topic after it repeats the same pattern: what
Core watches, the card it can open, and the words it uses.

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
  - `attention.ini`: what it has said, so it never says it twice
- Core as a whole, or any one topic, is switched off in `split-rock-corerc` in
  the person's settings folder (`[General] Enabled=false`, or
  `[Storage] Enabled=false`). A topic switched off is neither watched nor
  offered.
- Every surface works as it always has without Core. A surface finds Core on the
  session bus or carries on without it.

## Storage

Core looks at the drive that holds the home folder every 30 minutes, which costs
one call to the file system. From the last week of looks it works out how fast
the drive is filling.

| Level | When | Where it shows |
| --- | --- | --- |
| Quiet | Under 85% full, and not filling fast | Search only, when asked |
| Ticker | 85% full or more | One ordinary notification: "Storage · 86% full", "70 GB free". Tapping it opens Free up space |
| Island | 90% full or more, or at least 80% full and full within 14 days at the current rate | Search's island. Tapping it opens Free up space |

## How often Core speaks

- A fact speaks only when it gets worse, never because time passed: once at the
  ticker, once at the island, then again only after it is 3 points fuller.
- Not now, or dismissing the island, quiets the fact until it is 3 points
  fuller.
- At most one island a day from all of Core. Past that, the ticker carries it.
- Do Not Disturb leaves only the ticker.
- An island nobody touches closes after a day, and the logbook says it went
  unanswered.
- When the drive falls 2 points under the ticker's mark, the fact has settled:
  what was said is forgotten, so the next time it fills, Core speaks again.

The logbook records every time Core spoke and whether the person opened the
card, said not now, or left it, so real use can set these limits.

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
| `FactChanged(a{sv})` | A fact changed: its words, its level or whether it wants the island |

Each fact is a dictionary of strings to variants:

| Key | Type | What it is |
| --- | --- | --- |
| `topic` | `s` | `storage` |
| `title` | `s` | `Storage` |
| `line` | `s` | `91% full` |
| `detail` | `s` | `45 GB free · filling about 2 GB a day`; the rate only when it is at least 100 MB a day |
| `percent` | `i` | How full, in whole points |
| `level` | `s` | `quiet`, `ticker` or `island` |
| `attention` | `s` | `island` while the island should show it, `none` otherwise |
| `icon` | `s` | An icon name from the icon theme |
| `actionText` | `s` | What the card is called: `Free up space` |
| `keywords` | `as` | The words Search finds it by |

## Opening a card

Core opens a card by starting `split-rock --card space`. Split Rock then opens
that one upkeep card on its own, in an ordinary window titled after the card,
with no conversation and no assistant started. The window closes when the card
does, and can't be closed while the card is busy.
