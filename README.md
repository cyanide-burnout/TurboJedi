# TurboJedi

Turbo Journald Interface (J[e]DI) — a Turbo Vision viewer of the systemd journal. Every window is a journal client of its own:
its filter goes to the journal as a query, so a window of one unit or of errors only reads just those
entries, while other windows follow the whole system next to it.

Built on [magiblot/tvision](https://github.com/magiblot/tvision) and libsystemd.

![Two windows: the whole journal and its warnings and errors with found text highlighted](screenshot.png)

## Features

- Windows with filters of their own: levels, source (unit, identifier, command, transport, boot, user, PID,
  any `FIELD=VALUE`), time range, and text, plain or a regular expression, to show or to leave out
- Follows the tail, Space pauses; scrolling above the first line brings the older entries in
- Columns to choose: date, time, level, host, unit, identifier, PID; UTC, milliseconds, wrapped messages
- Find with highlighting, next and previous, on into the older entries above the first line
- Go to a time, and every other window to the time of the selected line: what happened elsewhere at that moment
- Details of an entry with all its fields, a field there opens a window narrowed to it
- Selection with the keyboard or the mouse, copy to the clipboard (OSC 52 over SSH), export to a file
- Layouts: all windows with their filters, columns and places, saved to and opened from INI files;
  the last one comes back on the next start

## Building

Debian packages: `build-essential cmake pkg-config libsystemd-dev libncurses-dev`

```
git submodule update --init
make
sudo make install
```

`make` builds tvision in `Import/tvision/build` first. `make install` puts the executable to `/usr/local/bin`
and the manual page to `/usr/local/share/man/man1`, `PREFIX` and `DESTDIR` move them.

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
| `-D`, `--directory DIR` | journal files of `DIR` |
| `-n`, `--lines N` | history to start with, 1000 by default |
| `-c`, `--layout FILE` | windows of a saved layout |

`TIME` is `2026-10-23 19:00[:00]`, `2026-10-23`, `19:00`, `now`, `today`, `yesterday`, `tomorrow` or a relative
`-1h`, `+30m` (`s`, `m`, `h`, `d`).

Reading the whole journal needs root or the `systemd-journal` group, others see their own entries only.

## Keys

| Key | |
|---|---|
| F1 | keys |
| F2 | new window with the filter of the focused one |
| F3 | levels |
| F4 | text filter |
| F7 | source |
| Ctrl-T | time range |
| Ctrl-K | columns |
| Ctrl-W | wrap long messages |
| Ctrl-F, Ctrl-N, Ctrl-P | find, next, previous |
| Ctrl-G | go to a time |
| Alt-G | every other window goes to the time of the selected line |
| Enter, double click | details of the entry, Enter on a field there opens a window with it added to the filter |
| Space | pause or follow |
| Shift+arrows, mouse | select lines, Esc drops the selection |
| Ctrl-Ins | copy the selected lines or the screen |
| Ctrl-E | export the selected lines or all of them |
| F8 | clear the window |
| Ctrl-O, Ctrl-S | open, save a layout |
| F5, Ctrl-F5, F6, F9, Alt-F3 | zoom, size, next, tile, close |
| Alt-X | exit |

## Layouts

A layout is an INI file with a `[window]` section per window:

```
[window]
bounds = 0 0 120 28
unit = nginx.service
text = timeout
regex = yes
levels = EACRW---
columns = date,time,level,identifier,pid,millisecond
```

Keys: `bounds`, `unit`, `identifier`, `command`, `transport`, `boot`, `user`, `pid`, `match` (repeatable),
`since`, `until`, `text`, `regex`, `exclude`, `case`, `levels` (letters `EACRWNID` of the levels shown, dashes
for the hidden ones) and `columns` (`date`, `time`, `level`, `host`, `unit`, `identifier`, `pid`, `utc`,
`millisecond`, `wrap`).

On exit the layout is kept in `$XDG_CONFIG_HOME/turbojedi/last.jedi` (`~/.config/turbojedi/last.jedi`)
and opened on the next start, unless a layout or filter options are given.
