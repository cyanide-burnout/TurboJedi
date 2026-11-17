# Changes of TurboJedi

Every release has a theme of its own, the patch releases after it finish the same one. Packages of every release are on
the [releases page](https://github.com/cyanide-burnout/TurboJedi/releases).

## 0.16 — 2026-11-17

Arrakis: a language model explains the entries and answers questions about them; the timeline gets ranges, their
history and UTC.

- Arrakis, `050-arrakis.lua`: Actions → Arrakis: explain this entry, which takes along up to 30 entries of its unit
  before it and names the one chosen, or explain N marked entries. A language model behind the API of OpenAI or of
  Anthropic that `arrakis.conf` tells answers what has happened, the likely cause and what to check: llama-server,
  Ollama, vLLM, colibri, `fm serve` of macOS over SSH, OpenAI, Anthropic. Without the file nothing is offered and
  nothing leaves the machine. lua-cjson for Lua 5.4 is needed, the packages of Debian, Ubuntu and openSUSE recommend it
- The answer comes in a window with a line for a question under it: Ask sends the question with the entries, the answer
  and the questions before it, as a conversation of that job alone, and the next answer comes the same way. Copy puts
  the whole answer to the clipboard, PgUp and PgDn scroll a long one. A request waits in a window with a spinner, the
  seconds so far and Cancel, the journal windows go on behind it
- The answers, the questions and the entries an answer names are lines of a watch "Arrakis N" in F12: Enter on an answer
  or a question brings it back with a line for the next question, Enter on an entry goes to it
- An address of another machine is asked about before the first request; a yes is kept in
  `~/.config/turbojedi/arrakis-trusted.txt` and holds while `url` stays the same
- An answer cut at the limit of tokens says so, one of thinking only or an empty one fails and tells why; Anthropic gets
  2048 tokens by default. `tokens` bounds an answer, `prompt` gives instructions of one's own and `record` keeps every
  answer with its request, 0600, for teaching a model of one's own
- Under `sudo` Arrakis takes `arrakis.conf` of the user of sudo, its `prompt` and `record` left out, and keeps the yes
  in the home of that user
- Window → Console: `print()` and `log()` of the plugins, their errors, the problems of the start, what each plugin has
  been loaded from and why one offers nothing, and what callers of D-Bus have done; a watch of TurboJedi itself that
  stays, F8 in its lines clears them
- Timeline: B draws the bars in Braille, two intervals a cell. Shift with the arrows, dragging or Shift+click select
  intervals, Z or a double click zooms into them, Ctrl-T sets the range; Ctrl-Z, a right click and Ctrl-Y go back and
  forth along the ranges and steps, + and - included. Its own status line tells the keys, clicked as well
- Timeline: U turns it to hours and days of UTC; intervals of hours and days go by the wall clock, a day the clocks
  change has 23 or 25 hours. A layout keeps the bars and the zone
- Ctrl-T has a UTC check box that turns the times over and keeps the moments; a time takes UTC at its end and fractions
  of a second as journalctl reads them, Ctrl-G starts in the zone of the window
- Window → Shadows: off, light, medium as Turbo Vision has them, or dark, for every window and dialog, shown as soon as
  chosen and kept in `screen.conf`
- The process of an entry opens in htop where it is installed, in top otherwise
- D-Bus: `Describe` of a timeline in front tells its filter, step, range and the interval under the cursor or the
  selection with their counts. `Invoke(plugin, method, values)` calls a method a plugin offers by `registerMethod()`.
  Files of allow, `/etc/turbojedi/allow.d/*.conf` and `~/.config/turbojedi/allow.conf`, name the users and groups
  answered besides the own one and root, and the methods they may call, `Invoke:plugin` or `Invoke:plugin.method` for
  one plugin or one method. See DBUS.md
- `copy =` of `~/.config/turbojedi/clipboard.conf` names the command of the clipboard when no option or environment
  does; under `sudo` the file of the user of sudo is taken and its command runs as that user
- `update.ini` and `screen.ini` are `update.conf` and `screen.conf` now, so the check for updates on start is asked
  about once more. Under `sudo` they are the ones of the user of sudo, written as that user; a file of settings is
  written whole by a file beside it
- Plugins: `watch(options)` makes a watch the plugin feeds itself and `watch:follow(options, func, ...)` lets the
  journal feed it, `watch(options, func)` of 0.15 still follows at once. A line of a watch takes an action for Enter and
  a double click, `{ action = func }`. `message(text, title, { input = prompt })` asks a question under a text. `sudo()`
  tells the user of sudo, `log()` writes to the console, `append()` adds to a file of the process alone, `settings()`
  keeps a file among the settings of TurboJedi, `registerMethod()` offers a method to D-Bus. A wait of `fetch()`,
  `dbus()` or `query()` shows a spinner and the seconds so far. See PLUGINS.md
- The example of Asterisk finds the CLI of a build of its own in `/opt/asterisk` or `/usr/local/sbin` as well

Thanks to icelord.

## 0.15.1 — 2026-11-16

- Plugins call a method of a service on the system or the user bus by `dbus()`, with typed values, waiting in an action
  or calling back later as `fetch()` does; an error comes back by its D-Bus name, a broken connection ends its calls
  and is opened anew. `services()` lists the names of a prefix with the process, the user and the unit that own them.
  See PLUGINS.md
- The status of a unit in the Actions menu tells its state and since when, as systemd tells it over D-Bus
- fail2ban comes with the package as `040-fail2ban.lua`, for root where `fail2ban-client` is installed. A link to the
  example `100-fail2ban.lua` left behind is told about at start and is to be removed; a copy of it is to be removed as
  well, or its items come twice
- The watch of SSH attacks is an example now, `100-ssh.lua`, to link as PLUGINS.md tells: it watches and rings on every
  machine with sshd while TurboJedi is open
- The soft limit of open files goes up to 65536, as every window and watch keeps every file of its journal open; an
  entry not read for want of descriptors is told so, not as gone from the journal
- An entry without actions, or no entry, is told by a line in the Actions menu itself instead of a dialog with OK
- `Describe` over D-Bus tells the descriptors TurboJedi has open and its limit

## 0.15 — 2026-11-16

Watches of the journal, and the workspace given to scripts and AI agents over D-Bus.

- Plugins watch the journal by `watch()`: the entries of a filter come to them as they are written, while TurboJedi is
  open, with a status and lines of their own; a watch keeps its place over rotations of the journal and tells the gaps
  it has not seen
- F12, a click on Watch 1/3 * in the status line or Window → Watches lists the watches with their status, Enter shows
  the lines of one and Enter on a line its entry in a window; an important line brings the list up with a bell.
  Backspace removes as Delete does, ← and → scroll the rows wider than the window, U turns the times to UTC and back
- Layouts keep the watches of the plugins in `[watches]`, and the list and the lines of a watch where they have been;
  `registerRestore()` gives the lines back to their plugin, `key` of `watch()` names a watch to it
- The plugin `040-ssh.lua` watches the attacks on the SSH server of the machine by address, with their failures and the
  user names tried; a login after failures or a flood of tries rings. The example `100-alert.lua` watches the journal
  by the rules of `~/.config/turbojedi/alert.conf`
- Every TurboJedi is on the system bus as `me.burnaway.TurboJedi.U<uid>.P<pid>` and answers its own user and root, and
  the user of sudo of an instance of root: its windows, what is in front, the entry under the cursor, the marked
  entries, queries of the journal, the actions of an entry and the watches. It changes only the screen, marks, the
  cursor, the moment of a window, a new window, and says so in the status line as `API: ...`; such a call waits while
  a dialog is open. See [DBUS.md](DBUS.md)
- `turbojedi-mcp.py` gives that to an AI agent such as Claude Code or Codex over SSH, as a server of the Model Context
  Protocol: the agent points, the user acts, no action of a plugin or command is run from there. See [MCP.md](MCP.md);
  the packages recommend Python 3 and dbus-python for it and bring the policy of the bus
- Relative times are read as journalctl reads them: `-1min`, `-1h 30min`, `-2days`, `5min ago`, `2 hours left`
- Keys are not held back while a journal that has changed is read
- PLUGINS.md tells when a plugin raises an error and when it gives one back, and that the text of a filter is matched
  against `identifier[pid]: message`

## 0.14.1 — 2026-11-15

- A window of output shows the lines of a command as it writes them, `tracepath` and `whois` among them, which kept
  everything until they ended; left of the buttons it tells that the command runs and for how long, then how it has
  ended and after how long
- Plugins ask web services over HTTP and HTTPS by `fetch()`: an action waits for the answer with Cancel, or the answer
  comes to a function later; the token of a header is on no command line
- Plugins keep a descriptor watched and a timer going between the events by `poll()` and `timeout()`, for a socket of
  luasocket, a subscription or a look now and then
- The values after the function of `poll()`, `timeout()` and `fetch()` go to it, as the ones of `menu:add()` go to an
  action; `turbojedi.version` tells a plugin the version of TurboJedi
- The example `100-chat.lua` sends the entry or the marked ones to Slack, Mattermost, Rocket.Chat, Discord, Telegram or
  ntfy
- The example of fail2ban offers no Ban for an address of the machine itself, such as the one sshd tells as `to`
- PLUGINS.md tells about the modules of Lua such as luasocket and their timeouts, and `fetch()` step by step
- libcurl is needed to build: `libcurl4-openssl-dev`, `libcurl-devel` or `curl`

## 0.14 — 2026-11-15

Actions on entries: plugins in Lua 5.4 make the menu of what can be done with the entry under the cursor.

- Alt-F10, A, F11, Shift-F10, a right click or Journal → Actions... opens the Actions menu of the entry under the
  cursor; every item names its plugin, the items of two plugins are parted by a line
- Plugins in Lua 5.4 come from `/usr/share/turbojedi/plugins`, `/usr/local/share/turbojedi/plugins` and
  `~/.config/turbojedi/plugins`: a file of the same name takes the place of one above, an empty one or a link to
  `/dev/null` leaves it out; root runs only the ones nobody but root, or the groups root and staff, can change. How to
  write them is in [PLUGINS.md](PLUGINS.md)
- Plugins ask with dialogs, copy to the clipboard and run commands in the background, in a window of their output or on
  the terminal, always as lists of arguments, never through a shell
- Plugins open windows with a filter, find and go to a time in the window, read the journal for the entries before or
  after one and mark them; the selected and marked entries of the window come to them as well
- The package brings plugins that only look: the status, unit file and process of the unit of an entry; the route,
  lookup, whois, ping and path of an address; a core dump in coredumpctl and gdb. fail2ban, block devices and Asterisk
  come as examples to link
- The cursor is shown from the start on the newest line of the screen, in the window in front; the arrows move it at
  once, Esc keeps it and drops the selection and the marks, Ctrl-Ins copies its line; thanks to select26 on Habr
- Lua 5.4 is needed to build: `liblua5.4-dev`, `lua-devel`, `lua54-devel` or `lua54`

## 0.13.1 — 2026-11-10

- The first start asks once whether to check for updates on start: Yes or No, Esc counts as No, the answer is kept in
  `update.ini`; a user of an older release without that file is asked as well

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
