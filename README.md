# TurboJedi

Turbo Journald Interface (J[e]DI) — a Turbo Vision viewer of the systemd journal. Every window is a journal client of its own:
its filter goes to the journal as a query, so a window of one unit or of errors only reads just those
entries, while other windows follow the whole system next to it.

It is scriptable in Lua 5.4: plugins make the Actions menu of an entry and watch the journal as it comes, see
[plugins](Documentation/PLUGINS.md). It is open to AI agents as well: `turbojedi-mcp.py`, a server of the Model Context Protocol,
gives Claude Code, Codex or another agent the windows you have open, over SSH; the agent finds and points at entries
on your screen, you act, see [MCP.md](Documentation/MCP.md). And it asks a language model itself: Arrakis explains the
entries you mark or the errors of a window, answers questions about them, by a model on your machine or a service of
your choice, see [ARRAKIS.md](Documentation/ARRAKIS.md).

Built on [magiblot/tvision](https://github.com/magiblot/tvision), libsystemd, PCRE2, Lua 5.4, libcurl and
[lua-cjson](https://github.com/openresty/lua-cjson).

![Three windows: the whole journal following, its warnings and errors paused with marked lines and 7 entries come meanwhile, and their timeline in Braille dots; the Plugins menu open with the items of Arrakis, Watches and Console](screenshot.png)

## Features

- Windows with journals and filters of their own: journals of this machine, of every machine or of a directory,
  one mounted from another machine over SSH or NFS as well,
  levels, source (unit, identifier, command, transport, boot, user, machine, PID,
  any `FIELD=VALUE`), time range, and text, plain or a regular expression of PCRE2, to show or to leave out
- Follows the tail, Space pauses; scrolling past the oldest line brings the older entries in. The title of every
  window tells it: ► follows, ‖ is paused, ‖ +12 has 12 entries come meanwhile; a click on the sign pauses or follows.
  A paused window that is full keeps
  its lines and marks, the new entries are counted and come in when it follows again
- The newest entries at the bottom or on top, like `journalctl --reverse`, for every window of its own; the lines of
  a message stay under it, the title ends with ▲ or ▼ and a click on it turns the order
- Columns to choose: date, time, level, host, unit, identifier, PID; UTC, milliseconds, wrapped messages, hidden heading,
  the newest on top
- Highlighting of messages by rules: built-in ones for times, addresses, identifiers, paths, quoted text, key=value
  and the words that matter, or any rule file of [grc](https://github.com/garabik/grc); colors of the escape sequences
  a message comes with are shown under the rules, the rest of the sequences are taken out
- Find with highlighting, next and previous, on into the older entries past the oldest line
- Clone of a window with its lines, filter, selection and place: one stays where it is, the other goes elsewhere;
  a timeline is cloned with its counts, step and scale
- List of the windows to go to, narrowed down by a filter
- Go to a time, and every other window to the time of the selected line: what happened elsewhere at that moment
- Details of an entry with all its fields, a field there opens a window narrowed to it
- Actions of an entry by plugins in Lua 5.4: Alt-F10, A, F11, Shift-F10 or a right click opens the menu of what the
  plugins offer for it, Alt-P the Plugins menu of what they do without one. The plugins of the package:
  [bookmarks](Documentation/BOOKMARKS.md), an entry or a selection kept with a comment and a topic after the journal
  has rotated; [Arrakis](Documentation/ARRAKIS.md), a language model of your choice that explains the entries; the unit
  and the process of an entry, addresses, core dumps, fail2ban; more of them are examples. See [plugins](Documentation/PLUGINS.md) and how
  to write one, [LUA.md](Documentation/LUA.md)
- Watches of plugins: entries of a filter as they come, a status and lines of each in a list, F12; Watch 1/3 * in the
  status line tells about new ones, an important one brings the list up with a bell
- AI agents: every instance offers its workspace on the system bus, and `turbojedi-mcp.py` gives it to an agent such
  as Claude Code or Codex over SSH. The agent points, the user acts; plugins may offer it methods of their own, a
  search or a watch the user follows. See [MCP.md](Documentation/MCP.md), the interface itself in
  [DBUS.md](Documentation/DBUS.md)
- Selection with the keyboard or the mouse, copy to the clipboard (OSC 52 over SSH), export to a file; Ctrl-D shows
  the marked lines alone
- Timeline: entries of the filter of a window by intervals of time, errors, warnings and the rest stacked
  in a column per interval; a column takes every journal window to its time
- Layouts: all windows with their filters, columns and places, saved to and opened from INI files;
  the last one comes back on the next start
- Check for updates in the Help menu, only when chosen or once a day when you say so; nothing goes to the network
  otherwise

## Installation

Ready packages of every release are on the [releases page](https://github.com/cyanide-burnout/TurboJedi/releases/latest):
Debian 12 and 13, Ubuntu 22.04, 24.04 and 26.04, Fedora 44, RHEL 9 and 10 (and their rebuilds), Amazon Linux 2023,
openSUSE Tumbleweed and Leap 16 on amd64 and arm64, Arch Linux on amd64. GitHub Actions builds each of them in a container
of its distribution. The script takes the one that fits:

```
curl -fsSL https://raw.githubusercontent.com/cyanide-burnout/TurboJedi/master/install.sh | sh
```

It asks for sudo when not run as root, `TURBOJEDI_VERSION=0.6` before `sh` takes that release instead of the latest one.
Run again, it updates TurboJedi and leaves it alone when that release is installed already. After an upgrade of the
distribution it puts the package of the same release built for the new one; Check for updates tells about it as well.
Raspberry Pi OS of 64 bits is Debian on arm64 and has packages; on 32 bits (armhf) TurboJedi builds from the sources.
By hand:

```
sudo apt install ./turbojedi_<version>_<distribution>_<arch>.deb
sudo dnf install ./turbojedi-<version>-1.<dist>.<arch>.rpm
sudo zypper install --allow-unsigned-rpm ./turbojedi-<version>-1.<tumbleweed|leap16>.<arch>.rpm
sudo pacman -U turbojedi-<version>-1-x86_64.pkg.tar.zst
```

Alpine has no systemd, so there is no journal to show. Elsewhere TurboJedi builds from the sources.

## Building

```
git clone --recursive https://github.com/cyanide-burnout/TurboJedi.git
cd TurboJedi
make
sudo make install
```

The dependencies and the packages of the distributions: [BUILD.md](Documentation/BUILD.md).

## Start

```
turbojedi [options] [FIELD=VALUE ...]
```

`-u nginx` takes the entries of a unit, `-p err` the errors, `-S -1h` the last hour, `-D DIR` a directory of journals;
reading the whole journal needs root or the `systemd-journal` group. F1 tells the keys, the main ones:

| Key | |
|---|---|
| F2 | new window |
| F3, F4, F7 | levels, text filter, source of the window |
| Ctrl-T | time range |
| Ctrl-F | find |
| Space | pause or follow |
| Enter | details of the entry |
| M | mark lines |
| Ctrl-D | the marked lines only, or all of them |
| Alt-F10, A | actions of the plugins for the entry |
| Alt-P | the Plugins menu, the watches and the console |
| Alt-T | timeline |
| Alt-G | every other window to the time of the line |
| Alt-X | exit, the layout is kept for the next start |

## Documentation

| | |
|---|---|
| [USAGE.md](Documentation/USAGE.md) | options, keys, journals of other machines, layouts, highlighting |
| [BUILD.md](Documentation/BUILD.md) | building from the sources and the packages of the distributions |
| [PLUGINS.md](Documentation/PLUGINS.md) | the plugins of the package and the examples, where plugins are |
| [LUA.md](Documentation/LUA.md) | the interface of Lua for plugins of your own |
| [BOOKMARKS.md](Documentation/BOOKMARKS.md) | bookmarks of entries, their topics and the base they are kept in |
| [ARRAKIS.md](Documentation/ARRAKIS.md) | entries explained by a language model |
| [MCP.md](Documentation/MCP.md) | AI agents over the Model Context Protocol |
| [DBUS.md](Documentation/DBUS.md) | the interface on the system bus |
| [CHANGELOG.md](CHANGELOG.md) | what every release has brought |
