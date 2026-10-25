<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="assets/readme/banner-dark.png">
    <img src="assets/readme/banner-light.png" alt="Split Rock: Helpers that look after a KDE Plasma computer" width="100%">
  </picture>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/KDE_Plasma-6-1d99f3?style=flat" alt="KDE Plasma 6">
  <img src="https://img.shields.io/badge/Arch_Linux-based-1793d1?style=flat" alt="Arch Linux based">
  <img src="https://img.shields.io/badge/input-touch_%2B_mouse-555555?style=flat" alt="Touch and mouse">
  <img src="https://img.shields.io/badge/license-GPL--2.0--or--later-blue?style=flat" alt="GPL-2.0-or-later">
</p>

<p align="center">
  <a href="#install">Install</a> · <a href="#controls">Controls</a> · <a href="docs/README.md">Docs</a> · <a href="#used-in-shuffle">Used in Shuffle</a>
</p>

<p align="center">
Helpers that look after a KDE Plasma computer, built for people leaving decades of Windows. Each Helper does one job, in plain words, and changes nothing until you tap its button. Core watches the computer quietly in the background and points you to the Helper that fits when something needs a look.
</p>

**Status:** early. On its own there is no package yet, only a developer install
(below).

## What you get

<table>
<tr>
<td width="33%" valign="top"><b>Update</b><br>Lists every update before it starts, then runs it with a restore point first.</td>
<td width="33%" valign="top"><b>Free up space</b><br>Names everything it would remove before it removes any of it. Tidy up Downloads moves old downloads to the Trash.</td>
<td width="33%" valign="top"><b>Install or remove an app</b><br>Finds the app for what you need, or removes the one you name. Your apps lists what you added, biggest first.</td>
</tr>
<tr>
<td width="33%" valign="top"><b>Health check</b><br>Looks the computer over and says what is fine and what needs a look.</td>
<td width="33%" valign="top"><b>What changed</b><br>The last two weeks, with a way to undo a change or go back to before. Restore points and community apps have Helpers too.</td>
<td width="33%" valign="top"><b>Core</b><br>A background program with no window. It notices when space runs low, updates have waited too long, a problem turns up, a Windows program lands in Downloads or a new device is plugged in, and says so without nagging.</td>
</tr>
</table>

Each Helper opens in a window of its own, and the window closes with it.
Before it changes anything, it writes what it is about to change in your
notebook, an ordinary folder of notes, with how to undo it. How Core decides
when to speak is in [docs/CORE.md](docs/CORE.md).

<details>
<summary>What it never does</summary>

- Show you a terminal.
- Change anything without asking first.
- Hold your password. Anything that needs one uses the system's own prompt.
- Ask for money or card details.

</details>

## Controls

| Task | Touch | Keyboard |
| --- | --- | --- |
| Open a Helper | Tap it in Search | None yet |
| Act on a Helper | Tap its button | None yet |
| Undo a past change | Tap Undo beside a change on What changed | None yet |
| Scroll | Drag the Helper | None yet |
| Close | Tap Close or Not now, or close the window | The window's own |

Every input, with what you see happen, is in [docs/INPUT.md](docs/INPUT.md).

## Install

```sh
./install.sh
```

This builds Split Rock, runs every check, and installs it for every account on
this computer: the Helpers' program, Core, the guide and the tour. It is a
developer install, not a package to hand to anyone else.

<details>
<summary>What it needs</summary>

CMake, a C++20 compiler, Qt 6.4 or later and Python 3 to build. The Helpers
manage an Arch-based system: updates and apps go through pacman and Flatpak,
and restore points through snapper where the disk supports them.

</details>

<details>
<summary>Updating and uninstalling</summary>

Running `./install.sh` again first removes what the earlier install placed,
and replaces Core if it is running. It asks for the administrator
password once, to write under `/usr`. On a computer where Shuffle has set up
its own install step, it needs no password.

`./uninstall.sh` removes what the install placed and leaves the notebook in
place.

</details>

<details>
<summary>With Shuffle</summary>

Split Rock works on any KDE Plasma desktop. Its Helpers open from Core's
notices, and from Search where Tettegouche is installed, as it is in Shuffle.

</details>

## For developers

<details>
<summary>Working on Split Rock</summary>

Read [AGENTS.md](AGENTS.md) before working in this repository. The documentation
index is [docs/README.md](docs/README.md). Run `./verify.sh` before proposing a
change: it checks the public text and the guide, builds Split Rock and runs
every test.

`./run.sh space` builds Split Rock and opens Free up space from this checkout;
any other Helper's name works the same way: `update`, `tidy`, `install`,
`apps`, `health`, `changes`, `restore` or `community`.

</details>

## Used in Shuffle

Shuffle, a touch-first desktop built on KDE Plasma, uses Split Rock for its
Helpers. The other open-source projects it uses are
[Kadunce](https://github.com/carlsonjm/kadunce),
[Tettegouche](https://github.com/carlsonjm/tettegouche),
[Temperance](https://github.com/carlsonjm/temperance),
[Gooseberry](https://github.com/carlsonjm/gooseberry) and
[Shuffle Keyboard](https://github.com/carlsonjm/shuffle-keyboard). Shuffle's own
site is [shuffleplasma.com](https://shuffleplasma.com).

## License

Split Rock is licensed under GPL-2.0-or-later. See [LICENSE](LICENSE).

The project names and marks are not covered by that licence. See
[TRADEMARKS.md](TRADEMARKS.md).

<p align="center"><picture><source media="(prefers-color-scheme: dark)" srcset="assets/readme/good-input-white.png"><img src="assets/readme/good-input-ink.png" alt="Good Input" width="140"></picture></p>
