# Changes of TurboJedi

Every release has a theme of its own, the patch releases after it finish the same one. Packages of every release are on
the [releases page](https://github.com/cyanide-burnout/TurboJedi/releases).

## 0.13 — 2026-11-08

Navigation and the order of a window: the newest on top, the signs of the title as buttons, undo and redo of the moves.

- Newest on top by Ctrl-R, the Journal menu, Columns, `-r` or `reverse` in layouts, for every window of its own: the
  lines of a message stay under it, the history comes under the last line, the cursor and the marks stay on their lines
  when the order turns; the title ends with ▲ or ▼
- The signs of the title are buttons: a click on ► or ‖ pauses or follows, on ▲ or ▼ turns the order
- Undo navigation by Ctrl-Z and Redo navigation by Ctrl-Y: every move of the cursor by the keys or the mouse, Ctrl-G,
  Alt-G and a find, the last 64 of them; a new move after Ctrl-Z drops the way ahead, marks and lines are not undone;
  Alt-Left, Alt-Backspace and Alt-Right go as well
- Exit of the status line with a menu open quits instead of hanging
- tvision at the head of upstream; `make` builds what has changed in it after the submodule has moved, and
  `git pull --recurse-submodules` updates a clone with it

Thanks to icelord.

## 0.12 — 2026-11-07

Selections: marks of lines apart, the state of a window in its title, a paused window that holds.

- Mark by M or Insert: a stroke from the cursor marks the lines it goes over, lines apart from each other are marked by
  strokes of their own, a stroke begun on a marked line takes the marks off; Esc drops all of them. The keys need no
  Shift, which Terminal of macOS keeps for itself
- In a stroke a click or a drag goes on with it as the keys do, Ctrl+click ends it; out of a stroke a click puts
  the cursor and the marks stay
- The title of every journal window tells its state: ► follows, ‖ is paused, ‖ +N has N entries come meanwhile
- A paused window that is full keeps its lines and marks: new entries are counted, not kept, and read after the last
  line when the window follows again; it holds when the lines of an entry do not go in whole
- A window whose filter has no entries yet shows them when they come, not after a restart only

## 0.11.1 — 2026-11-07

- A line cursor on the arrows: Up, Down, PgUp, PgDn, Home and End move it, it comes on the last row first; on the
  newest line the window follows

Thanks to select26 on Habr.

## 0.11 — 2026-11-05

Distribution and remote journals.

- Journals of other machines over sshfs and NFS: a journal of a file system without inotify is read on every poll,
  windows follow it through rotation and reconnects; a fault of a mapped journal file gives zeros instead of SIGBUS
- Links to the repository come from the origin of the clone: About, `--help` and Check for updates of a fork point
  to the fork

## 0.10.3 — 2026-11-04

- Packages know the distribution they are built for: after an upgrade of the distribution `install.sh` puts the
  package of the same release built for the new one, Check for updates tells about it

## 0.10.2 — 2026-11-04

- Check for updates on start, chosen in the answer of a check: once a day whatever the answer and only about a newer
  release, kept in `$XDG_CONFIG_HOME/turbojedi/update.ini`

Thanks to icelord.

## 0.10.1 — 2026-11-04

- Check for updates in a dialog of its own: Copy URL and Copy command take only the link of the release or the command
  of the install script

## 0.10 — 2026-11-04

Updates.

- Help → Check for updates asks GitHub for the latest release only when chosen and shows it with the command of the
  install script; built with libcurl
- Link time optimization: the parts of TurboJedi are inlined across its files

## 0.9 — 2026-10-30

Display.

- Escape sequences of a terminal in messages: SGR colors are shown under the highlighting rules, the rest is taken out
  instead of drawn as glyphs
- Columns: the heading of the table can be hidden, the lines take its row; `headless` in layouts
- A paused window keeps its lines on the screen when a full one drops the oldest

## 0.8 — 2026-10-28

Convenience and thrift.

- List of the windows by Alt-0, narrowed down by a filter
- Next lines of a multi-line message go under it without the columns of the entry, as `journalctl` puts them; no empty
  rows from line breaks at the end of a message
- F2 opens a window with the filter of the command line
- Terminal title of its own, the previous one comes back at the end
- Journals wake TurboJedi up by their inotify instead of polling it several times a second
- A held mouse button repeats
- WRAP in the status line for a window with wrapped lines
- Layout dialogs start at the layout opened or saved last, Save offers its file again

## 0.7.1 — 2026-10-27

- rpm: debug information of openSUSE in a package of its own, hardening flags of the distribution on EL9 and openSUSE
- `install.sh` downloads nothing when that release is installed already, a pinned older release downgrades

## 0.7 — 2026-10-27

Clones and the install script.

- Shift-F2 clones the focused window: lines, filter, find, selection and place, or the counts, step, scale and cursor
  of a timeline
- `install.sh` installs the package of the latest release for the distribution and architecture
- Packages for Ubuntu 22.04, Amazon Linux 2023, openSUSE Tumbleweed and Leap 16; Debian 12 packages serve Raspberry Pi
  OS bookworm

Thanks to Tuxozaur and konkere on Habr.

## 0.6 — 2026-10-26

Clipboard.

- Copy through a command of the user: `--clipboard-copy` or `TURBOJEDI_CLIPBOARD_COPY`, the clipboard of Turbo Vision
  when it fails

## 0.5.2 — 2026-10-26

- Mouse of the Linux console by GPM when libgpm is there, in the packages but EL10

## 0.5.1 — 2026-10-25

- Timeline: full cells by their background, no gaps with fonts of taller rows

## 0.5 — 2026-10-25

Timeline.

- Timeline by Alt-T: entries of the filter of a window by intervals of time
- Timelines in layouts: `[timeline]` with the filter, the step and the scale; `[journal]` for windows of the journal,
  `[window]` is still read
- UTF-8 for characters when the locale has none, as under sudo
- rpm: the license and the documentation by their names

Thanks to TIEugene on Habr.

## 0.4.1 — 2026-10-25

- Whole fields of the journal, a byte bound on the buffer of a window

Thanks to opium on Habr.

## 0.4 — 2026-10-25

Sources of journals.

- Journals of every window: of this machine, of every machine or of a directory, Browse for the directory
- Machine in Source, chosen by host name; `--merge` for the journals of every machine

Thanks to antonchev on Habr.

## 0.3 — 2026-10-24

- Arch Linux package by `make arch`

## 0.2 — 2026-10-24

- Packages for Debian, Ubuntu, Fedora and RHEL on amd64 and arm64 by GitHub Actions, `make deb` and `make rpm`
- The list of a pick dialog is given back on shut down instead of leaking

Thanks to vlad4kr7 and Apoheliy on Habr.

## 0.1 — 2026-10-24

A Turbo Vision viewer of the systemd journal.

- Journal windows with filters, levels and columns: go to time, synchronized windows, find beyond the buffer, back to
  where a window was before a jump
- Highlighting by built-in rules or rule files of grc, PCRE2 for regular expressions
- Debian package by `make package`, cross builds for arm64
