# The desktop

How Split Rock meets the desktop it runs on. Split Rock is the Helpers: each
opens on its own, in an ordinary window, when Core or Search asks for it. What
Shuffle adds switches on only where Shuffle is installed. On any other Plasma
desktop, a Helper's window is placed like any other window.

## The entry

The desktop file id is `io.github.carlsonjm.SplitRock`, and it is every window's
application id, so a window tool such as Kadunce knows a Helper's window as
Split Rock's.
The desktop file is `data/io.github.carlsonjm.SplitRock.desktop.in`. It names
the program and its icon for the windows and Core's notices. It is hidden from
the launcher, because without a Helper there is nothing to open. `install.sh`
installs it, with the program, its icon, Core, the guide and the tour, for
every account; `uninstall.sh` removes them. That is a developer install, not a
package.

| Start | What opens |
| --- | --- |
| `--card NAME` | One Helper in a window of its own: `update`, `space`, `tidy`, `install`, `uninstall`, `apps` (Your apps), `health`, `changes`, `restore` or `community`. `--query` gives Install an app what to look for, or Remove an app the desktop file id of the app to remove |
| `--notebook DIR` | With `--card`: the notebook folder, `~/Documents/Notebook` by default (`MEMORY.md`) |
| `--health-report` | No window: it looks the computer over as Health check does, prints what it finds, and ends. It needs no desktop and changes nothing, not even the last look Health check keeps. It ends with 1 only when the look could not run to its end. Shuffle's install checks run it on a freshly installed system |
| `--health-facts` | The same look for Core, one line per item: its state (`good`, `look` or `problem`), id, title and detail, separated by tabs |
| No `--card` | Nothing: it says how to start a Helper, and ends |

Only one Split Rock runs at a time. A second start hands its arguments to the
one running and ends, so a second Helper asked for opens in the same window,
closing the first; a Helper that is busy is never closed for another. Split Rock
ends a moment after the Helper's window closes, so one Helper handing over to
another, as Health check does to Free up space, keeps the window. To receive a
second start's arguments, it holds `io.github.carlsonjm.splitrock` on the
session bus while it runs, with `/Application` and
`io.github.carlsonjm.SplitRock.Application`'s `CommandLine(as)`. That name is
internal to Split Rock and starts nothing.
