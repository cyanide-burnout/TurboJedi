# Using TurboJedi

How to start TurboJedi, its keys, journals of other machines, layouts and highlighting. What it is and how to install
it: [README](../README.md).

## Usage

```
turbojedi [options] [FIELD=VALUE ...]
```

| Option | |
|---|---|
| `-t`, `--identifier ID` | entries of `SYSLOG_IDENTIFIER` |
| `-u`, `--unit UNIT` | entries of a systemd unit, `.service` is assumed without a suffix |
| `-U`, `--user USER` | entries of a user, by name or number |
| `-p`, `--priority LEVEL` | entries up to `LEVEL`: `emerg` … `debug` or `0` … `7` |
| `-k`, `--kernel` | kernel messages only |
| `-S`, `--since TIME` | entries from `TIME` on |
| `--until TIME` | entries up to `TIME` |
| `-b`, `--boot` | entries of the current boot |
| `-D`, `--directory DIR` | journal files of `DIR` in the first window |
| `-m`, `--merge` | journals of every machine there are in the first window, like `journalctl --merge`: other machine IDs in `/var/log/journal` and `/var/log/journal/remote` of `systemd-journal-remote` |
| `-n`, `--lines N` | history to start with, 1000 by default |
| `-r`, `--reverse` | the newest entries on top in the first window, like `journalctl --reverse` |
| `-H`, `--highlight NAME` | highlighting rules: `builtin` (default), `off`, a rule file of grc such as `conf.log`, or a path |
| `-c`, `--layout FILE` | windows of a saved layout |
| `--clipboard-copy COMMAND` | command of `sh` that takes copied lines on its standard input, over `$TURBOJEDI_CLIPBOARD_COPY` |

`TIME` is `2026-10-23 19:00[:00[.000000]]`, `2026-10-23`, `19:00`, `now`, `today`, `yesterday`, `tomorrow` or a relative
one as journalctl takes it: `-1h`, `+30m`, `-1h 30min`, `-2days`, `5min ago` (`us`, `ms`, `s`, `min`, `h`, `d`, `w`,
`M`, `y` and their longer names); in local time, or in UTC with `UTC` at the end as journalctl takes it:
`2026-10-23 17:00 UTC`.

Reading the whole journal needs root or the `systemd-journal` group, others see their own entries only.

Copy (Ctrl-Ins, and Copy in the details) goes to the clipboard of the terminal by OSC 52 or to a clipboard tool
of the desktop. A command of your own takes the lines instead, such as `remote-copy.sh` of
[RemoteClipboard](https://github.com/cyanide-burnout/RemoteClipboard) that brings them home over an SSH socket:

```
export TURBOJEDI_CLIPBOARD_COPY="$HOME/remote-copy.sh"
turbojedi
```

or once and for all in `~/.config/turbojedi/clipboard.conf`, a line of its own:

```
copy = /home/you/remote-copy.sh
```

The option goes over the environment, the environment over the file. The file is read only when it is a regular
file of your own, not a link.

When the command exits with an error or does not finish in 3 seconds, the clipboard of the terminal takes the
lines as usual. Input lines of the dialogs keep the clipboard of Turbo Vision, pasting is up to the terminal.

Under `sudo` the clipboard still belongs to you: `sudo turbojedi` takes `clipboard.conf` of your home and runs its
command as you, with your groups, `HOME`, `USER` and `LOGNAME`, never as root; what you have written there is no
more than you can run yourself. `sudo` leaves `TURBOJEDI_CLIPBOARD_COPY` out of the environment, so the file is the
way. A command given by the option or kept by `sudo -E` runs as root, as before, and goes back to your user by itself:

```
sudo turbojedi --clipboard-copy "sudo -u $USER $HOME/remote-copy.sh"
```

Your shell puts in `$USER` and `$HOME` before `sudo` starts.

## Keys

| Key | |
|---|---|
| F1 | keys |
| F2 | new window with the filter of the command line |
| Shift-F2 | clone of the focused window: its lines, filter, find, selection and place, or the counts, step, scale, bars and cursor of a timeline; both go on with the journal on their own |
| F3 | levels |
| F4 | text filter |
| F7 | source |
| Shift-F7 | journals: of this machine, of every machine or of a directory |
| Ctrl-T | time range, in local time or with UTC checked in UTC: checking it turns the times over to the same moments, `today` to the date and time of its midnight, and back to `today` with nothing typed meanwhile; it starts in the zone the range is written in, or as the window shows times when nothing in the range is of a zone |
| Ctrl-K | columns |
| Ctrl-W | wrap long messages |
| Ctrl-R | the newest entries on top or at the bottom; the title ends with ▲ when they are at the bottom, ▼ when on top, a click on it turns the order too |
| Ctrl-D | Marked only: the marked lines alone, the ones between them hidden, every line of a marked message; the selection becomes marks first. The keys and the mouse go over the lines shown, a mark more shows its line, new entries are counted as in a pause. ◆ in front of ▲ or ▼ in the title, ◇ when all the lines are shown, a click on it turns it too; `MARKED` in the status line. Ctrl-D again shows all of them, the cursor on the same entry |
| Ctrl-L | highlighting rules |
| Ctrl-F, Ctrl-N, Ctrl-P | find, next, previous |
| Ctrl-G | go to a time, the one of the line at hand in the zone the window shows |
| Alt-G | every other window goes to the time of the selected line |
| Ctrl-Z | undo navigation: back along the moves of the window, every move of the cursor by the keys or the mouse, Ctrl-G, Alt-G and a find, the last 64 of them; marks and lines are not undone; also Alt-Left and Alt-Backspace |
| Ctrl-Y | redo navigation: forward again where Ctrl-Z has come from, also Alt-Right; a new move after Ctrl-Z drops the way ahead |
| Alt-T | timeline: entries of the filter of the window by intervals of time; Left and Right move, Enter takes every journal window to the interval, `+` and `-` change the step, `L` the scale, `B` the bars: blocks, or Braille dots with two intervals a cell, `U` hours and days of UTC or of the local zone, as the window shows times at first. Shift with the arrows, dragging or Shift+click select intervals, `Z` or a double click in the selection zooms into it, Ctrl-T sets the range, Ctrl-Z or a right click and Ctrl-Y go back and forward along the ranges and the steps with the cursor; intervals of hours above one and of days go by the wall clock, a day the clocks change has 23 or 25 hours; the status line tells the keys, the heading the scale, the bars and the zone |
| Up, Down, PgUp, PgDn, Home, End | move the cursor, it starts on the newest line of the screen; on the newest line the window follows, anywhere else it pauses |
| Enter, double click | details of the entry under the cursor, Enter on a field there opens a window with it added to the filter |
| Alt-F10, A, F11, Shift-F10, right click | actions of the plugins for the entry under the cursor, see [PLUGINS.md](PLUGINS.md); A is for Terminal of macOS, which keeps F10 and F11 for itself. An item with ► opens a submenu by Right or Enter, Left goes back |
| Alt-P | the Plugins menu: what the plugins do without an entry, such as a search by an identifier, then the watches and the console |
| Space | pause or follow, also a click on ► or ‖ in the title |
| Shift+arrows, mouse | select lines from the cursor, Esc drops the selection and the marks, the cursor stays |
| M, Insert | begin marking from the cursor, the keys mark the lines they pass; M again ends it and they stay marked, so lines apart are marked one stroke after another. Begun on a marked line it takes the marks off instead. No Shift is needed, which Terminal of macOS and some others keep for themselves |
| Click | put the cursor on a line, the marks stay; in a stroke of M the click goes on with it as the keys do. Ctrl+click marks a line or takes its mark off, Shift+click and dragging select |
| Ctrl-Ins | copy the selected lines or the line under the cursor |
| Ctrl-E | export the selected lines or all of them |
| F8 | clear the window |
| Ctrl-O, Ctrl-S | open, save a layout; both start at the layout opened or saved last, Save offers its file again |
| F5, Ctrl-F5, F6, F9, Alt-F3 | zoom, size, next, tile, close |
| Plugins → Console | the console of TurboJedi: `print()` and `log()` of the plugins, their errors, the problems of the start and what callers of D-Bus have done, a problem left unread; a watch of its own in F12 that stays, F8 in its lines clears them |
| Window → Shadows | of every window and dialog: off, light, medium as Turbo Vision has them, or dark; kept in `$XDG_CONFIG_HOME/turbojedi/screen.conf` whatever layout comes, under `sudo` the one of your home, written as you |
| Alt-0 | list of the windows to go to, the one used before is under the cursor |
| F12 | watches of plugins with their status, Enter shows the lines of one, Delete or Backspace removes it, U turns their times to UTC and back |
| Alt-X | exit |

## Journals of other machines

A journal directory mounted from another machine opens with `-D` or Shift-F7 like any other directory, with the filters,
the history, the timeline, the details and Alt-G. `/var/log/journal` opens as it is, with the directories of machine IDs
in it; a machine that keeps its journal in memory only (`Storage=volatile`) has it in `/run/log/journal`. A network file
system or FUSE tells nothing about appended entries and new files, TurboJedi looks at the files every second instead and
opens the journal again after a rotation.

Over SSH with sshfs, as a user of the group `systemd-journal` on that machine or as root:

```
mkdir -p ~/journals/web1
sshfs -o ro,reconnect,cache=no,ServerAliveInterval=15 admin@web1:/var/log/journal ~/journals/web1
turbojedi -D ~/journals/web1
fusermount -u ~/journals/web1
```

`cache=no` lets the appended entries in at once, sshfs keeps what it knows about a file for 20 seconds otherwise and they
come in by those steps. With `reconnect` the files open before the connection broke cannot be read anymore, the windows
open their journals again when it is back and go on after their last lines.

Over NFS, exported read-only by that machine in `/etc/exports`:

```
/var/log/journal  monitor.example.com(ro,no_subtree_check)
```

and mounted where TurboJedi runs:

```
sudo mkdir -p /mnt/journals/web1
sudo mount -t nfs -o ro web1:/var/log/journal /mnt/journals/web1
turbojedi -D /mnt/journals/web1
```

The files are `root:systemd-journal` with mode 0640 and NFS compares numbers: the user who reads them needs a group with
the GID `systemd-journal` has on that machine, root reads them only with `no_root_squash` in the export. A mount of NFS is
`hard` unless told otherwise: while the server is away, and during its grace period of 90 seconds after a start, the windows
wait for it the way every program on that mount does, and go on when it answers.

A window per machine, each with its directory chosen by Shift-F7, puts several machines side by side: Alt-G takes every
window to the time of the selected line, and a layout keeps their directories for the next start.

## Layouts

A layout is an INI file with a `[journal]` section per window of the journal (`[window]` in older layouts):

```
[journal]
bounds = 0 0 120 28
unit = nginx.service
text = timeout
regex = yes
levels = EACRW---
highlight = conf.log
columns = date,time,level,identifier,pid,millisecond
```

Keys: `bounds`, `directory`, `merge`, `unit`, `identifier`, `command`, `transport`, `boot`, `user`, `machine`, `pid`, `match` (repeatable),
`since`, `until`, `text`, `regex`, `exclude`, `case`, `levels` (letters `EACRWNID` of the levels shown, dashes
for the hidden ones), `highlight` and `columns` (`date`, `time`, `level`, `host`, `unit`, `identifier`, `pid`, `utc`,
`millisecond`, `wrap`, `headless`, `reverse`).

A timeline is a `[timeline]` section with `bounds`, the keys of the filter it counts, `step` (seconds of an interval,
`0` lets the window choose one that fits), `scale` (`linear` or `log`), `bars` (`blocks` or `braille`) and `utc = yes` for hours and days of UTC; a zoomed
timeline keeps its range in `since` and `until`:

```
[timeline]
bounds = 0 28 120 42
unit = nginx.service
levels = EACRWNID
step = 3600
scale = log
bars = braille
```

The list of watches of plugins is a `[watchlist]` section with its `bounds`, the lines of a watch a `[watchlines]` section
with `bounds` and the `plugin` and `key` of the watch, which find it when the layout opens; a watch without a key is
found by its `title` and `index` among the watches of the same plugin and title in the order they have been set; the watches
themselves are kept by their plugins in a `[watches]` section, see [LUA.md](LUA.md#layouts), which has `utc = yes` when
their times are of UTC:

```
[watchlist]
bounds = 5 8 115 26

[watchlines]
bounds = 7 9 117 27
plugin = tetrapack
key = context:41d48349-56d6-4e73-b2b9-bb45e9df0c97
title = Context 41d48349-56d6-4e73-b2b9-bb45e9df0c97
```

### Last layout

On exit the layout is kept in `$XDG_CONFIG_HOME/turbojedi/last.jedi` (`~/.config/turbojedi/last.jedi`)
and opened on the next start, unless a layout or filter options are given.

## Highlighting

Rules are those of [grc](https://github.com/garabik/grc): `regexp=` with `colours=` for the match and its groups,
`count=once`, `more` or `stop`, entries apart by lines of `-` or `=`. Rule files come from `~/.grc`,
`/usr/local/share/grc` and `/usr/share/grc`, or by a path. Colors go over the color of the level, light ones for
the blue window; rules of several lines (`count=block`) and replacements are left out. The built-in rules are
written the same way in `Source/Journal/JournalHighlighter.cpp`.

## Check for updates

Check for updates in the Help menu asks GitHub for the latest release only when chosen, shows it with the command of
the install script; nothing goes to the network otherwise. Its answer has the choice of a check on start, once a day and
only about a newer release, kept in `$XDG_CONFIG_HOME/turbojedi/update.conf`; the first start without that file asks for
the choice once, Yes or No. Under `sudo` it is the file of your home, written as you, so root asks nothing you have
answered already.
