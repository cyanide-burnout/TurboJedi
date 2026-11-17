# Plugins of TurboJedi

The Actions menu of an entry is made by plugins in Lua 5.4. A plugin looks at the entry under the cursor and offers
what can be done with it: the status of its unit, a ban of an address in its message, a command of your own with a
value of one of its fields. TurboJedi itself knows nothing of the services: it gives the fields of the entry, the menu,
dialogs and ways to run commands.

Alt-F10 opens the menu, as the local menu of the IDEs of Borland, also F11 as in Far, Shift-F10 and a right click on a
line; Journal → Actions... is in the menu bar. A plain A opens it as well, as M marks: Terminal of macOS keeps F10 and
F11 for itself and has no Alt unless Option is set to be Meta. Every item shows the plugin it comes from on its right, the items of two
plugins are parted by a line.

## Where plugins are

| Directory | |
|---|---|
| `/usr/share/turbojedi/plugins` | of the package |
| `/usr/local/share/turbojedi/plugins` | put on the machine by hand |
| `$XDG_CONFIG_HOME/turbojedi/plugins` | of the user, `~/.config/turbojedi/plugins` without XDG_CONFIG_HOME |

All three are read whatever way TurboJedi is built and installed. There are no plugins in `/etc`: it is for settings,
not for code.

- Every `*.lua` file of them is run once at start, in the order of the names: `010-systemd.lua` before
  `040-fail2ban.lua`. The name of a plugin is the name of its file without the number and `.lua`: `fail2ban`.
- A file of the same name in a directory lower in the table takes the place of the one above: the user's file of the
  package, the machine's one of the package.
- A plugin may be a link to a file elsewhere, an example of the package among them; under root the file it points to
  is the one checked. An empty file, or a link to `/dev/null`, of the same name leaves the plugin out, the way a unit of
  systemd is masked. A link to a file that is gone is told about.
- `lib/` of the directories holds modules for `require`, looked for in the same order: `require('net')` finds
  `lib/net.lua` of the user first, `require('net.address')` finds `lib/net/address.lua`. Paths relative to the current
  directory are taken out of `package.path` and `package.cpath`.
- A plugin that fails to load is told about once the windows are up, the rest of them work.

`/usr/share/turbojedi/examples` holds plugins that change things and are not run by themselves; copy one to a directory
of plugins to use it.

### Plugins and root

The directory of the user is the one of the running process, by its `XDG_CONFIG_HOME` or `HOME`, as for `last.jedi`.
Under `sudo` it is root's, never the one of `SUDO_USER`, for plugins and modules. A plugin may still read settings of
the user of sudo, data and not code: `sudo()` tells that user and the home the system has for them, as Arrakis reads
`~/.config/turbojedi/arrakis.conf` of the one who has run `sudo turbojedi`.

Run by root, TurboJedi uses a plugin or a module only when root alone can change it: the file and every directory above
it, up to `/`, links resolved, belong to root, and the others cannot write to them. The group may write when it is
`root`, or `staff`, which Debian and Ubuntu give `/usr/local` so that its members install there: root runs what they
put to `/usr/local/bin` anyway. A plugin that is left out is told about with the directory that has let it out.

## How a plugin works

A plugin registers handlers when it is loaded. When the menu is opened, every handler is called with the entry and adds
the items it has for it, or none. When an item is chosen, its function is called.

A handler only makes the menu and changes nothing: it is also called without the menu being shown, when a caller of
the D-Bus interface asks which actions an entry has, see [DBUS.md](DBUS.md). Whatever changes something belongs to the
function of an item, which only the user runs.

```lua
turbojedi.registerHandler(function(entry, menu, context)
  local unit = entry._SYSTEMD_UNIT

  if type(unit) == 'string' then
    menu:add('Status of ' .. unit, function()
      turbojedi.output { 'systemctl', 'status', '--no-pager', unit }
    end)
  end
end)
```

A file may register several handlers, a handler may add several items, and two items of one plugin may share a
function with arguments of their own:

```lua
local function ban(address, jail) ... end

turbojedi.registerHandler(function(entry, menu)
  for found in turbojedi.matches(entry.MESSAGE, ADDRESS) do
    menu:add('Ban ' .. found.address, ban, found.address, 'sshd')
  end
end)
```

Rules:

- **A handler only makes the menu.** Dialogs and commands in it are an error: the menu is opened by a key, and nothing
  is asked or run before an item is chosen.
- **A handler that fails takes its items along.** The items it has added before the error are not shown, the error is,
  the other plugins work.
- **Lua is not entered twice.** While an action runs, even in a dialog or waiting for a command, the windows go on, but
  no menu is opened and no handler is called.
- **Plugins are read at start.** A changed plugin works after TurboJedi is started again.

### `entry`

The fields of the entry under the cursor as the journal has them, by their names: `entry.MESSAGE`,
`entry._SYSTEMD_UNIT`, `entry._PID`, `entry._HOSTNAME`, any field the entry has. A field the entry does not have is
`nil`.

- Values are strings, binary data included: a string of Lua holds any bytes. A number such as `_PID` is a string too,
  `tonumber()` makes it a number.
- A field the entry has several times, which the journal allows, is a list of its values in their order:
  `type(value) == 'table'`.
- `__REALTIME_TIMESTAMP` and `__MONOTONIC_TIMESTAMP` are integers of microseconds. `__CURSOR` is the cursor of the
  entry, `journalctl --cursor` takes it.

### `context`

What the window tells about where its journal comes from, not a field of the entry:

| Field | |
|---|---|
| `source` | `'local'` for this machine, `'all'` for every machine the journal has, `'directory'` for a directory |
| `directory` | the directory, when `source` is `'directory'` |
| `marked` | the entries of the selection and the marks of the window, tables like `entry`, the oldest first, up to 1000 of them; `nil` when only the cursor is there |
| `selected` | how many entries the selection and the marks hold, more than `#marked` when the list is cut |

Commands always run on this machine. A unit or a process of an entry of another machine, of a merged journal or of a
directory mounted over SSH or NFS, is not here: `_HOSTNAME` tells where it is, and an action may reach it by `ssh`.

An action on the marked entries takes `context.marked` along, the way the other values of the handler go to it:

```lua
turbojedi.registerHandler(function(entry, menu, context)
  if context.marked then
    menu:add(('Copy %d marked messages'):format(#context.marked), function(marked)
      local lines = { }
      for _, item in ipairs(marked) do lines[#lines + 1] = item.MESSAGE end
      turbojedi.copy(table.concat(lines, '\n') .. '\n')
    end, context.marked)
  end
end)
```

Later versions may add fields to `context` without changing the handlers there are.

### `menu:add(title, func, ...)`

Adds an item: the title, the function to call when it is chosen and the arguments to call it with, `nil` among them.

- `~` marks a hot key in the title as everywhere in Turbo Vision: `'~S~tatus'`. A title made of a value of the entry
  that has `~` in it gets a highlight instead of it.
- A title is cut to fit the screen. A menu holds up to 100 items.
- Called anywhere but in a handler, it is an error.

## Functions

All of them are in the table `turbojedi`. Arguments of a command are a list, `{ 'systemctl', 'status', unit }`: the
first one is the program, looked for in `PATH`, every other one goes to it as it is. A value of an entry stays one
argument whatever it holds, no shell sees it.

| Function | When | |
|---|---|---|
| `registerHandler(func)` | loading | a handler, `func(entry, menu, context)` |
| `registerRestore(func)` | loading | gets back the watches of the plugin a layout has kept, `func(context, position)` for each |
| `registerMethod(name, func)` | loading | a method for `Invoke()` of D-Bus: `func(values)` with the values of the caller as a table, the table it returns is the answer; see below |
| `settings(name, text)` | any time | a file of the plugin of its own among the settings TurboJedi writes, `~/.config/turbojedi/name`, under `sudo` the one of the user of sudo, read and written as that user. Without the text, its text or `nil`; with it, the file replaced whole: `true`, or `nil` and why. A name of letters, digits, dots, dashes and underscores; `update.conf`, `screen.conf`, `clipboard.conf`, `allow.conf` and `last.jedi` are the ones of TurboJedi itself, so a plugin names its file after itself, and a file several plugins share after what they share. For what a plugin remembers of the user, the addresses Arrakis may send to for one |
| `append(path, text)` | any time | the text at the end of a file of the process alone: made with 0600, never through a link, refused when it is another's or open to others; `true`, or `nil` and why. For what a plugin keeps of the journal |
| `log(text, options)` | any time | a line of the console, Window → Console, with the name of the plugin; `{ problem = true }` leaves it unread, the status line tells it. `print()` writes there too, as a quiet line |
| `message(text, title, options)` | action, callback | a text with OK, a long one in a window to scroll; the title is "Message" without one. `{ input = prompt }` puts a line to type into under the text, with Ask, Copy and Close: the line typed, or `nil` by Close, Esc or an empty line. Up, Down, PgUp and PgDn scroll the text while the line has the cursor, Copy puts the whole text to the clipboard as it has been given |
| `confirm(text)` | action, callback | Yes or No: `true` or `false`. No is the default one, Esc is No |
| `input(prompt, value)` | action, callback | a line to edit, starting with the value: the line, or `nil` when cancelled |
| `choose(title, items, initial)` | action, callback | one of a list of strings: the item and its index, or `nil` when cancelled. `initial` is an item or an index to start on |
| `copy(text)` | action, callback | puts the text to the clipboard the way Ctrl-Ins does |
| `open(options)` | action | a new window with a filter, like the one the menu has been opened in |
| `find(text, options)` | action | the previous match in the window of the menu, as Find: `true` when found |
| `seek(time)` | action | the window of the menu goes to a time, as Go to time |
| `query(options)` | handler, action | entries of a filter for the plugin to look into: a list, and why the reading has stopped |
| `mark(entries, options)` | action, callback of a watch | marks the lines of entries in the window of the menu or of the watch, as M does: the count marked |
| `watch(options)` | loading, action, callback | a watch: a status and lines in the list of watches, fed by the plugin and, after `watch:follow()`, by the journal |
| `poll(descriptor, events, func, ...)` | loading, action, callback | calls `func(flags, ...)` when the descriptor is ready, between the events: a poller |
| `timeout(milliseconds, func, periodic, ...)` | loading, action, callback | calls `func(...)` after the time, again and again when periodic: a timer |
| `fetch(request, func, ...)` | loading, action, callback | a request over HTTP or HTTPS: with `func` its answer goes to `func(code, body, headers, ...)` later, without it the action waits for `code, body, headers` |
| `dbus(request, func, ...)` | loading, handler, action, callback | a method of a service on D-Bus: with `func` its answer goes to `func(reply, name, text, ...)` later, without it a handler, an action or a callback waits for `reply, name, text` |
| `services(options)` | loading, handler, action, callback | the names on a bus that have an owner, with their process and unit |
| `execute(argv)` | action, callback | runs a command in the background: `true`, or `nil` and the reason when it has not started |
| `output(argv)` | action, callback | runs a command in a window of its own: `true`, or `nil` and the reason |
| `terminal(argv)` | action, callback | runs a command on the terminal: `how, code` |
| `capture(argv, options)` | action, callback | runs a command and waits for it: `output, how, code, errors` |
| `quote(argv)` | any time | the arguments as one line of `sh`, every one quoted when it has to be |
| `regex(text, pattern, flags)` | any time | the first match, or `nil` |
| `matches(text, pattern, flags)` | any time | an iterator over every match |
| `which(name)` | any time | the path of a program in `PATH`, or `nil` |
| `sudo()` | any time | the user who has run TurboJedi as root by `sudo`: `{ name = ..., uid = ..., home = ... }`, `nil` otherwise; for a file of settings of that user, see Plugins and root |
| `utc()` | any time | `true` when the user wants the times of the watches in UTC, `false` for the local zone |
| `date(format, time)` | any time | `os.date()` in the zone the user has chosen for the watches: put it in its place for the times a watch shows. `!` in front of the format forces UTC as in `os.date()`, `*t` is left to `os.date()` |

Called at the wrong time, a function is an error.

### Errors

The functions tell two kinds of trouble apart, the way `io.open` of Lua does:

- **A mistake of the plugin is raised** as an error of Lua: a wrong argument (`copy() takes a text`), a call at the
  wrong time, a regular expression or a filter that cannot be taken. It is the same as an error in the code of the
  plugin itself, and goes the same way.
- **A refusal of the world is given back** as a value, the plugin decides what to do with it: a command that has not
  started, a network that does not answer, a text not found, a reading stopped by its limit. Nothing is raised, and
  nothing is hidden either:

| Function | When it has not worked |
|---|---|
| `execute`, `output` | `nil` and the reason the command has not started |
| `capture`, `terminal` | `how` is `'error'`, `'timeout'` or `'cancel'` instead of `'exit'` or `'signal'` |
| `fetch` | `nil, how, text`; an answer of 404 or 500 is an answer, its code tells it |
| `dbus`, `services` | `nil, name, text`: the name of the error of D-Bus, `'cancel'` for Cancel |
| `find` | `false` when nothing more is found |
| `query` | the entries read, and why the reading has stopped: `'limit'`, `'scan'`, `'time'` or `'cancel'` |
| `mark` | the count marked, fewer than given when some are not in the window; in a callback of a watch without a window, `nil` and the reason |
| `watch:follow` | `nil` and the reason when its journal cannot be opened |

An error raised goes to the window of the plugins with the name of the plugin and the place in it:

- in a handler, the items it has added go and the other plugins make the menu;
- in an action, the action ends there;
- in a callback of `poll` or `timeout`, the poller or the timer goes and is not called again; a callback of `fetch` or
  `dbus` is called once anyway;
- in a callback of `watch:follow`, the watch stops reading and stays in the list of watches as failed, with its lines.

`pcall` catches an error raised as anywhere in Lua, when a plugin would rather go on. `remove()` of a poller or a watch and
`cancel()` of a timer or a request that are gone already do nothing: that is not a failure.

### Version

`turbojedi.version` is the version of TurboJedi as git tells it: `v0.15` of a release, `v0.15-3-gabcdef` of a build
after it, `unknown` of a copy without git. A plugin that needs a function of a later release asks for the function
itself, which works for a build between the releases too:

```lua
if not turbojedi.fetch then
  error('fetch() is in TurboJedi ' .. turbojedi.version .. ' not yet')
end

local major, minor = turbojedi.version:match('^v(%d+)%.(%d+)')  -- nil for unknown
```

### Dialogs

Dialogs are modal, the windows go on behind them; a long text scrolls. They are the way for the user to see with their
own eyes what is about to be done, with the values the plugin has found: an address, an identifier, a unit.

```lua
if turbojedi.confirm(('Ban %s in jail %s?'):format(address, jail)) then
  turbojedi.execute { 'fail2ban-client', 'set', jail, 'banip', address }
end
```

### Clipboard

`copy` takes the way Ctrl-Ins and Copy of a window take: the command of `--clipboard-copy` or
`TURBOJEDI_CLIPBOARD_COPY` on its standard input, such as `remote-copy.sh`, or the clipboard of Turbo Vision, OSC 52 over
SSH among its ways, when there is no command or it fails. A result goes there without a window:

```lua
local text, how, code = turbojedi.capture { 'systemctl', 'show', unit }

if (how == 'exit') and (code == 0) then
  turbojedi.copy(text)
end
```

### Windows

An action may take the user to where the journal tells more: a new window with a filter, or a find or a time in the
window the menu has been opened in. Lua does not read the journal itself, the windows do, with their filters, history
and Undo navigation.

`open` makes a window the way Enter on a field of Details does: its journals, columns and highlighting are of the window
of the menu, its filter is of the options.

| Option | |
|---|---|
| `matches` | `{ FIELD = value }`, `{ FIELD = { value, ... } }` or `{ 'FIELD=VALUE', ... }`: the journal finds them, the values of one field are ORed, different fields ANDed |
| `text` | the text filter, plain by default; with `regex = true` an expression of PCRE2, `exclude = true` leaves the matching entries out, `case = true` minds the case. It is matched against `identifier[pid]: message`, as the text filter of a window is: `^Failed` never matches, `: Failed` does |
| `since`, `until` | times as `-S` takes them: `2026-10-23 19:00`, `-10m`, `today` |
| `priority` | up to the level, as `-p`: `err`, `warning` or `0` to `7` |

```lua
turbojedi.open { matches = { _SYSTEMD_UNIT = entry._SYSTEMD_UNIT }, text = identifier, since = '-1h' }
```

`find` looks for the text the way Find does, from the entry of the menu on into the older entries, `newer = true` the
other way; `regex` and `case` as for `open`. The match is highlighted and becomes a step of Undo navigation, Ctrl-N and
Ctrl-P go on with it. It gives `false` when nothing more is found, saying nothing: the action tells it as it sees fit.

`seek` takes microseconds, `__REALTIME_TIMESTAMP` of an entry for one, or a time as Go to time takes it:

```lua
turbojedi.seek(entry.__REALTIME_TIMESTAMP - 60 * 1000000)  -- a minute before the entry
```

### Reading the journal

`query` gives the plugin the entries of a filter to look into on its own, so that it decides by what the journal has
told before: not to offer Drop for a connection that has closed already, to show how many times it has come back. It
takes the options of `open`, read the way a window reads: the journal applies the matches, the text is matched against
`identifier[pid]: message`, the times bound the range.

| Option | |
|---|---|
| `before` | an entry or its `__CURSOR`: the entries older than it, the newest first; the entry itself is left out |
| `after` | the same the other way: the entries newer than it, the oldest first |
| `limit` | entries to give, 100 by default |
| `scan` | entries of the matches to look at, 100000 by default: a text is matched here, not by the index of journald |
| `timeout` | seconds an action reads, 30 by default, 0 for as long as it takes |

Without `before` and `after` it starts from the newest entry of the range. The entries are tables like `entry`.

```lua
local closed, stopped = turbojedi.query { matches = { _SYSTEMD_UNIT = entry._SYSTEMD_UNIT }, text = id .. ' closed', before = entry, limit = 1 }
```

The second value tells why the reading has stopped before the journal or the range has ended: `limit`, `scan`, `time`
or `cancel`; `nil` when it has looked at everything. Nothing found with `nil` means there is nothing; nothing found with
a reason means it is not known, and an action that changes things has to take it so.

A handler may query while it makes the menu, so that the items depend on the history, but the menu is not kept waiting:
the queries of all handlers of one menu share 200 ms and 10000 entries, a query stops with `time` or `scan` when they
are spent. An action reads on, after a moment with a dialog and Cancel while the windows go on.

### Marking

`mark` marks in the window of the menu the lines of the entries it is given, every line of a message, the way M and
Ctrl+click mark: Copy and Export take them, Esc takes them off. It takes an entry, a list of entries such as `query`
gives, or cursors; `clear = true` takes the other marks off first. The older entries come into the window down to the
oldest one given, as far as the window holds; a long way back gets a dialog with Cancel after a moment, while the
windows go on, and Cancel marks what has come so far. The count of the entries marked tells when some are not there.

```lua
local entries, stopped = turbojedi.query { matches = { _SYSTEMD_UNIT = entry._SYSTEMD_UNIT }, text = id, limit = 1000 }
local count = turbojedi.mark(entries, { clear = true })
```

### Commands

Every command runs in a process group of its own with `/dev/null` for its input, so that whatever it starts stops
along with it.

- **`execute`** runs in the background, with no window. Its end is told only when it is not `exit 0`: a dialog with
  the command, how it has ended and its standard error.
- **`output`** opens a window of the output, standard output and error together in the order they come, with the
  colors of their escape sequences, line by line as the command writes them: it runs through `stdbuf -oL -eL` when
  coreutils has it, so that `tracepath` or `whois` do not hold their output until they end. Left of the buttons a
  turning sign and the time tell that it runs, then how it has ended and after how long; the outcome also comes at the
  end of the output, `[exit 0]`. The window is not modal; closing it stops the command. Copy puts its text to the
  clipboard.
- **`terminal`** gives the screen back to the terminal while the command runs on it, as Ctrl-O of Norton Commander:
  `ssh`, `less`, `top`, `vim` work as they do in a shell. Ctrl-C and Ctrl-\ go to the command. After it, Enter brings
  TurboJedi back.
- **`capture`** waits for the command and gives its standard output and error apart. A short command ends before
  anything is shown, a longer one gets a dialog with Cancel; the windows go on meanwhile. `options.timeout` is the
  seconds it may take, 30 by default, 0 for as long as it takes. Up to 16 MiB of each stream is kept.

How a command has ended is told the way `os.execute` of Lua tells it:

| `how` | `code` | `output` of `capture` |
|---|---|---|
| `'exit'` | the exit code, success is `0` only | all of it |
| `'signal'` | the number of the signal | what has come |
| `'timeout'` | the seconds it has had | what has come |
| `'cancel'` | `nil` | what has come |
| `'error'` | the reason it has not started, such as `no-such-program: No such file or directory` | `nil` |

`errors` of `capture` is the standard error, `nil` when there is none.

```lua
local text, how, code, errors = turbojedi.capture { 'fail2ban-client', 'status' }

if (how ~= 'exit') or (code ~= 0) then
  turbojedi.message('fail2ban does not answer: ' .. (errors or (how .. ' ' .. tostring(code))))
  return
end
```

### A command to edit before it runs

A shell is fine for a line the user has seen and accepted. `quote` makes the line with the values of the entry quoted,
`input` lets it be changed, `sh -c` runs it:

```lua
local line = turbojedi.input('Command', turbojedi.quote { 'ssh', '-v', user .. '@' .. address })

if line then
  turbojedi.terminal { 'sh', '-c', line }
end
```

Never put a value of an entry into a line of a shell by yourself, `quote` is there for it.

### Regular expressions

`regex` and `matches` take patterns of PCRE2, the syntax of Perl and Python, the one of the text filter: named groups,
`\d`, `\b`, lookarounds. Patterns of Lua have none of them. `flags` is a string, `'i'` ignores the case.

A match is a table: `[0]` the whole match, `[1]` and on the groups, named groups by their names as well. A group that
has not taken part is `nil`. Without a text, a field the entry does not have, `regex` gives `nil` and `matches` goes
round no time.

```lua
local found = turbojedi.regex(entry.MESSAGE, [[Failed password for (?<user>\S+) from (?<address>\S+)]])

if found then
  print(found.user, found.address, found[0])
end

for number in turbojedi.matches(entry.MESSAGE, [[\d+]]) do
  print(number[0])
end
```

## Lua

The plugins run in Lua 5.4 of the distribution, with its whole standard library: `string`, `table`, `math`, `utf8`,
`io`, `os`. A plugin is trusted as a shell script of the user is: there is no sandbox. What is not to be trusted is the
entry, the journal of another machine above all; that is why commands are lists of arguments.

All plugins share one state of Lua, a global of one is seen by the others: keep your names `local`.

### Modules of Lua

A plugin may `require` any module installed for Lua 5.4 in the system: luasocket, a library of JSON, a client of MQTT.
`package.path` and `package.cpath` keep the directories of the system, only the paths relative to the current directory
are taken out, and `lib/` of the plugin directories is looked into first. Under root, the modules of `lib/` are checked
the way the plugins are; the ones of the system directories belong to its packages.

An action runs on the thread of the screen. While a call of a module waits, nothing is drawn and no key is taken; only
the dialogs and `capture` of TurboJedi keep the windows going. Give a call to the network a short timeout:

```lua
local socket = require('socket')
local client = socket.tcp()

client:settimeout(2)

local done, failure = client:connect(host, 80)
```

A request to a web service goes better through `fetch`, below, which waits in a dialog with Cancel or not at all; any
other one that may take long, through a program and `capture`, which does the same for it.

A handler making the menu should not wait on the network at all: the budget of the menu bounds `query`, not a module.
A socket that is to be read when something comes is better watched by `poll`, below.

### Descriptors and timers

A plugin may keep a descriptor watched and a timer going between the events, the way the windows read their journals,
for a subscription of MQTT, a socket of a service, a look at something now and then. They are set while the plugin is
loaded, by an action or by a callback.

```lua
local socket = require('socket')
local udp    = socket.udp()

udp:setsockname('127.0.0.1', 47474)
udp:settimeout(0)

local poller = turbojedi.poll(udp:getfd(), 'r', function(flags)
  local data = udp:receive()
  if data then turbojedi.copy(data) end
end)

local timer = turbojedi.timeout(5000, function() ... end, true)
```

- `poll(descriptor, events, func, ...)` watches a descriptor, `'r'`, `'w'` or `'rw'`; `func` gets what is ready, `'r'`,
  `'w'` or `'rw'`, with `'e'` for an error or a hang-up. `poller:update(events)` changes the events, `poller:remove()`
  stops it. A descriptor closed without `remove()` is told about and its poller goes.
- `timeout(milliseconds, func, periodic, ...)` calls `func()` once after the time, or every that time when `periodic` is
  true; `timer:cancel()` stops it.
- The values after the function go to it after the ones of the event, as the ones of `menu:add` go to an action:
  `poll(fd, 'r', read, client)` calls `read(flags, client)`, `timeout(1000, retry, false, url)` calls `retry(url)`.
- The descriptors are looked at every 50 ms while there are pollers, so a callback comes within that much.
- A callback runs only when no dialog is open and Lua runs nothing else: it waits while an action asks something. It
  may use the dialogs and the commands as an action does, but not `open`, `find`, `seek`, `mark` and `query`, which are
  of the window of a menu.
- A callback that fails is told about with its plugin and is not called again.
- They stay until removed or cancelled, or until TurboJedi exits.

### Requests over HTTP

`fetch` asks a web service through libcurl of TurboJedi itself: a webhook of a chat, a ticket, a look at an address,
a silence of Alertmanager. No program runs, and a token in a header is not on any command line.
[`100-chat.lua`](Plugins/Examples/100-chat.lua) of the examples is a whole plugin made with it, see Chat below.

**Ask and wait.** In an action, `fetch` with the address alone waits for the answer; when it is late, a dialog with
Cancel comes up, the windows go on meanwhile:

```lua
menu:add('Weather here', function()
  local code, body = turbojedi.fetch('https://wttr.in/?format=3')
  if code == 200 then turbojedi.message(body) end
end)
```

**Tell how it went.** The answer is `code, body, headers`: the status of HTTP, the body and a table of the headers by
their names in lower case. 404 or 500 is an answer as well, the code tells it. A request that has got no answer is
`nil, how, text`: `how` is `'error'` (no network, no such name, a certificate), `'timeout'` or `'cancel'`, `text` says
more:

```lua
local code, body, more = turbojedi.fetch(url)

if not code then
  turbojedi.message(('No answer: %s, %s'):format(body, more))  -- body is how, more the text here
elseif code ~= 200 then
  turbojedi.message(('The service has answered %d'):format(code))
else
  turbojedi.copy(body)
end
```

**Send something.** A table instead of the address takes the method, the headers, the body and the timeout. A body
makes it a POST unless `method` says otherwise:

```lua
local code = turbojedi.fetch {
  url     = 'https://tickets.example.com/api/issues',
  method  = 'POST',                                   -- GET by default, POST with a body, any other word
  headers = { ['Content-Type'] = 'application/json',
              ['Authorization'] = 'Bearer ' .. token },
  body    = '{"title": "Disk full on db1"}',
  timeout = 10                                        -- seconds of the whole request, 30 by default, 0 for no limit
}
```

A header with an empty text takes out the one libcurl would send, such as `Accept`. A value of the entry put into JSON
by hand has to have its quotes, backslashes and control characters escaped: `json()` of `100-chat.lua` does it.

**Read JSON.** Lua has no JSON of its own; a module does it, such as `lua-cjson` where the distribution has it for
Lua 5.4. `pcall(require, ...)` keeps the plugin working without it:

```lua
local found, cjson = pcall(require, 'cjson')

menu:add('Who is ' .. address, function()
  local code, body = turbojedi.fetch('https://ipinfo.io/' .. address .. '/json')
  if code ~= 200 then return end

  if found then
    local info = cjson.decode(body)
    turbojedi.message(('%s: %s, %s'):format(address, info.org or '?', info.country or '?'))
  else
    turbojedi.message(body)
  end
end)
```

**Do not wait.** With a function after the request, `fetch` returns at once and the answer comes to the function
later, with the same values. Nothing is shown meanwhile, the user goes on; the function may use the dialogs as an action
does, and is called when no dialog is open, once. This one also may start while the plugin is loaded:

```lua
local request = turbojedi.fetch({ url = hook, body = text }, function(code, body, more)
  if code ~= 200 then turbojedi.message('Not sent: ' .. tostring(code or more)) end
end)

request:cancel()  -- drops it while the answer has not come, the function is not called then
```

**Take your values along.** The values after the function go to it after the answer, as the ones of `menu:add` go to
an action: one function serves every request, no closure is needed for each:

```lua
local function told(code, body, more, unit, host)
  if code ~= 200 then turbojedi.message(('%s of %s is not reported: %s'):format(unit, host, tostring(code or more))) end
end

turbojedi.fetch({ url = hook, body = text }, told, entry._SYSTEMD_UNIT, entry._HOSTNAME)
```

**Limits.**

- Only `http` and `https`, redirects are followed up to five.
- The proxy of `http_proxy`, `https_proxy` and `no_proxy` is used, the certificates are of the system.
- An answer larger than 16 MiB is an error.
- Waiting is allowed to an action and a callback, not to a handler making the menu, which never waits on the network.
- Requests that are still going when TurboJedi exits are dropped.

### Calls over D-Bus

`dbus` calls a method of a service on the system bus or the bus of the user through sd-bus of TurboJedi itself:
systemd, a daemon of your own, anything `busctl call` reaches. No program runs, and the values come typed, not as
text to take apart:

```lua
local reply, name, text = turbojedi.dbus {
  bus       = 'system',                           -- 'system' by default, or 'user'
  service   = 'org.freedesktop.systemd1',
  path      = '/org/freedesktop/systemd1',
  interface = 'org.freedesktop.systemd1.Manager', -- may be left out, as D-Bus allows
  method    = 'GetUnit',
  signature = 's',                                -- of the arguments, as busctl takes it; none without them
  arguments = { 'nginx.service' },
  timeout   = 5                                   -- seconds, 25 by default as sd-bus has it, up to a day
}
```

**The answer.** `reply` is a table of the values of the answer in their order, `reply[1]`, `reply[2]`... A method
without values gives an empty table: that is a success too, `if not reply` tells a failure. A failure is
`nil, name, text`: `name` is the name of the error as D-Bus gives it, `text` its message:

| `name` | |
|---|---|
| `org.freedesktop.DBus.Error.ServiceUnknown` | no such service on the bus |
| `org.freedesktop.DBus.Error.UnknownMethod` | the service has no such method, or not with these arguments |
| `org.freedesktop.DBus.Error.AccessDenied` | the policy of the bus does not let the call through |
| `org.freedesktop.DBus.Error.NoReply` | no answer in time, the time of the menu included |
| `org.freedesktop.systemd1.NoSuchUnit` and the like | an error of the service itself, by its own name |
| `System.Error.ENOENT` and the like | an errno a service has answered with, or one of this side: root has no bus of a user, as a rule |
| `'cancel'` | Cancel of the dialog, or `cancel()` of the call |

Checking the name instead of the text keeps a plugin working whatever language the service speaks:

```lua
local reply, name = turbojedi.dbus { ... }

if name == 'org.freedesktop.systemd1.NoSuchUnit' then
  turbojedi.message('The unit is not loaded')
elseif not reply then
  turbojedi.message('No answer: ' .. name)
end
```

**Types.** Values of Lua go to the arguments by the signature, values of the answer come back as Lua has them:

| D-Bus | Lua |
|---|---|
| `y`, `n`, `q`, `i`, `u`, `x` | an integer, checked against the range of the type on the way there |
| `t` | an integer; one above `math.maxinteger` comes negative, as `string.unpack('J')` has it, and goes back as it is, `math.ult` compares them: `UINT64_MAX`, which systemd uses for infinity, is `-1` |
| `d` | a number |
| `b` | `true` or `false` |
| `s`, `o`, `g` | a string |
| `ay` | a string of bytes |
| an array `a...` | a sequence |
| a dictionary `a{...}` | a table by its keys |
| a struct `(...)` | a sequence |
| a variant `v` | the value in it; going there, a string is `s`, a boolean `b`, a float `d`, an integer `i` while it fits, `t` above and `x` below, and `{ type = 'u', value = 5 }` names the type itself |
| `h` | a descriptor of a file is not passed: a mistake in the arguments, `System.Error.ENOTSUP` in an answer |

A value that does not fit the signature is a mistake of the plugin and is raised, as a wrong argument of any function.

**Waiting.** As `fetch`: in an action and a callback, `dbus` without a function waits for the answer, a dialog with
Cancel comes up when it is late and the windows go on meanwhile. A handler making the menu may call it too, without a
dialog: the calls of all handlers share the 200 ms of the menu, and the time over is `NoReply`. The state of a unit in
the title of an item is quick enough for it:

```lua
local function status(unit) ... end  -- the action, as in 010-systemd.lua

turbojedi.registerHandler(function(entry, menu, context)
  local unit = entry._SYSTEMD_UNIT
  if (type(unit) ~= 'string') or (context.source ~= 'local') then return end

  local found = turbojedi.dbus { service = 'org.freedesktop.systemd1', path = '/org/freedesktop/systemd1',
    interface = 'org.freedesktop.systemd1.Manager', method = 'GetUnit', signature = 's', arguments = { unit } }
  local state = found and turbojedi.dbus { service = 'org.freedesktop.systemd1', path = found[1],
    interface = 'org.freedesktop.DBus.Properties', method = 'Get', signature = 'ss',
    arguments = { 'org.freedesktop.systemd1.Unit', 'ActiveState' } }

  menu:add(('Status of %s%s'):format(unit, state and (' (' .. state[1] .. ')') or ''), status, unit)
end)
```

A plugin being loaded does not wait: `dbus` without a function then is a mistake.

**Later.** With a function after the request, `dbus` gives back a handle at once and the answer comes to the function
later, `reply, name, text` and the values after the function, when no dialog is open; `call:cancel()` drops it, and the
function is not called then. This one also may start while the plugin is loaded:

```lua
local function told(reply, name, text, what)
  if not reply then turbojedi.message(('%s has not answered: %s'):format(what, name)) end
end

local call = turbojedi.dbus({ service = 'me.example.Daemon', path = '/me/example/Daemon', method = 'Reload' },
  told, 'The daemon')
```

Up to 64 calls of all plugins go on at once; one more is refused with `System.Error.EBUSY`.

**Who is there.** `services` lists the names on a bus that have an owner, of a prefix or all, sorted, with the
process that owns each:

```lua
for _, service in ipairs(turbojedi.services { bus = 'system', prefix = 'me.example.Daemon.' }) do
  -- service.name, service.pid, service.uid, service.unit: the unit of systemd of the owner, nil without one
  if (tostring(service.pid) == entry._PID) or (service.unit == entry._SYSTEMD_UNIT) then ... end
end
```

It asks the bus itself and waits for it in any phase, up to 5 seconds, or within the time of the menu in a handler;
a failure is `nil, name, text` as above.

**Limits.**

- The bus decides who may call what, by its policy and by polkit of the service, as it does for `busctl` of the same
  user: TurboJedi run by a user calls as that user, run by root as root.
- Signals are not listened to: a plugin asks, a service does not tell it by itself.
- Calls that are still going when TurboJedi exits are dropped.

## Methods for the bus

A program of the user may call a plugin over D-Bus, `Invoke(plugin, method, values)` of DBUS.md: a service that tells
what it has found, a script of cron, an agent. The plugin offers what it wants to by name while it is loaded:

```lua
-- ~/.config/turbojedi/plugins/200-alerts.lua: the plugin is called alerts
turbojedi.registerMethod('notify', function(values)
  -- A timer answers the caller at once, the user reads it when the event comes
  turbojedi.timeout(0, function(text) turbojedi.message(text, 'Alert') end, false, values.text or '')
  return { shown = true }
end)
```

```
busctl --system call me.burnaway.TurboJedi.U1000.P4242 /me/burnaway/TurboJedi me.burnaway.TurboJedi \
  Invoke 'ssa{sv}' alerts notify 1 text s 'disk is almost full'
```

- `values` is the `a{sv}` of the caller as a table, the values of their types; nested ones as `dbus()` gives them.
- The function runs as a callback: dialogs, commands, watches, timers, `dbus()`; not `mark()` and not `query()`.
- The table it returns is the answer, `a{sv}`: strings, numbers, `true`/`false` as they are, anything else as
  `{ type = 'as', value = { ... } }`. Nothing returned is an empty answer.
- An error of the function goes to the caller as `Failed` with its message; the user is not told.
- Only the methods a plugin has registered are there; a plugin that has failed to load has none.

## The console

`print()` of a plugin cannot write to the screen, Turbo Vision has it: its values, a tab between them, go to the
console of TurboJedi as a line with the name of the plugin. `log(text)` does the same, `log(text, { problem = true })`
leaves the line unread so that the status line calls for it. The console has the errors of the plugins as well, the
problems of the start and what callers of D-Bus have done.

At start it tells every plugin: the file it is loaded from and what it has set, handlers of the menu, watches, methods
for `Invoke()`, or that it has set nothing and so has nothing to offer here; a plugin left out by an empty file of its
name, and why one has failed. The plugins of the package tell their own reason as they decide, `fail2ban is not
installed`, `no arrakis.conf`, the file and the service Arrakis talks to; a plugin of your own does well to tell its
own too, by `log()`.

It is a watch of TurboJedi itself, `turbojedi` with the key `console`: Window → Console or F12 shows it, `ListWatches()`
and `GetWatchLines()` of D-Bus read it, a layout keeps the window of its lines. Nobody removes it, F8 in its lines
clears them. `turbojedi` is no name for a plugin: a file of that name is left out.

## Watches

A watch is a line in the list of watches, F12 or Window → Watches, with a status the plugin tells and lines it adds:
the console above is one. The status line says `Watch 2/5 *` while there are any: two of five watches have lines not
read yet, the star stays until the list has been looked at. A click on it opens the list.

A plugin makes one by `watch(options)` and feeds it itself, from a request, a timer, a command: the jobs of Arrakis
are such watches, a line each with the answer in it. `watch:follow(options, func, ...)` lets the journal feed it too,
see below.

```lua
local job = turbojedi.watch({ title = 'backup of /srv' })

job:status('running')
turbojedi.timeout(60000, function() job:status('done') job:add('backup of /srv is over') end)
```

| Option | |
|---|---|
| `title` | the name of the watch in the list, needed |
| `key` | what the watch is to the plugin, unique among its watches: a layout finds the window of its lines by it. Without one, by its title and its place among the ones of the same title, which holds while the plugin sets them in the same order. A key taken by another watch of the plugin is an error |
| `window` | `'current'`, by default: the window of the menu is the window of the watch while it is open; `'new'`: a window of its own, opened when needed |
| `context` | data a layout keeps to get the watch back, see Layouts below |

`watch()` reads no journal: an option of a filter, `skipped` or `from` is for `watch:follow()` and an error there. A
line added with an entry goes to that entry in the window of the watch, the one of the menu it has been made in.

### Following the journal

```lua
local count = 0

local restarts = turbojedi.watch({ title = 'nginx restarts' })

restarts:follow({ matches = { _SYSTEMD_UNIT = 'nginx.service' }, text = 'Started' },
  function(entry, watch)
    count = count + 1
    watch:status(('%d restarts, last at %s'):format(count, os.date('%H:%M', entry.__REALTIME_TIMESTAMP // 1000000)))
    watch:add('restarted: ' .. entry.MESSAGE, entry)
  end)
```

This is for the time TurboJedi is open: alerts while you work in it, counts, a mark on what matters. It is not a
monitoring of the server: closed TurboJedi or a broken SSH connection watch nothing.

`watch:follow(options, func, ...)` takes a filter of the same options as `open` and `query`, `matches`, `text` with
`regex`, `exclude` and `case`, `priority`, and two of its own:

| Option | |
|---|---|
| `skipped` | `skipped(gap, watch, ...)`, when the watch has not seen a part of the journal |
| `from` | the place a layout has kept, given by `registerRestore()` |

- The watch starts at the end of the journal: what has been there before it is not its business, so it takes neither
  `since` nor `until`. Called while loading, it follows the journal of this machine; in an action, the journal of the
  window of the menu; in a callback of a watch, the journal of that watch.
- A watch follows one journal: a second `follow()` of it is an error. Its window is the one the watch has been made
  with, `mark` and the entries of its lines go there.
- `func(entry, watch, ...)` gets the entry as a handler does, the watch itself, the same table every time, and the
  values after the function.
- A callback runs only when no dialog is open, as the ones of `poll` do. It may use the dialogs and the commands, and
  `mark`, which marks in the window of the watch: when the watch has no window, `mark(entry, { open = true })` opens one,
  without it `mark` gives `nil` and the reason.
- `follow()` gives the watch, or `nil` and the reason when the journal cannot be opened; the watch stays as it is then,
  without the journal, and the plugin removes it or tells the reason in it.
- `watch(options, func, ...)` with a function and the filter among the options, the way 0.15 has had it, still makes a
  watch and follows the journal at once; `nil` and the reason, and no watch, when the journal cannot be opened. A
  plugin for both 0.15 and 0.16 keeps that form.

### Methods

| Method | |
|---|---|
| `watch:follow(options, func, ...)` | the journal feeds the watch, see Following the journal above |
| `watch:status(text)` | the status of the watch in the list |
| `watch:add(text, entry, options)` | a line of the watch with its time, of one or several lines of text; with an entry, Enter on it goes to the entry in the window of the watch. `{ action = func }` makes Enter or a double click on it call `func(watch, line)` instead, as a callback of the watch: a dialog of the plugin about that line, the answer of Arrakis with a line for a question for one. `{ raise = true }` brings the list up with a bell, the watch red and blinking until its lines are read; `{ quiet = true }` adds it read, for a list of a state that changes all the time and is not news. The number of the line |
| `watch:delete(line)` | the line of a number `add` has given |
| `watch:clear()` | all its lines |
| `watch:context(table)` | the data a layout keeps from now on |
| `watch:remove()` | the watch goes, with its lines |

- A watch keeps its last 1000 lines. What it writes and how often is up to the plugin: a log of every entry, a line
  with a count, several lines of text.
- In the list, Enter shows the lines of a watch, Delete or Backspace removes it after a question. In the lines, Enter
  or a double click calls the action of a line or goes to its entry, Delete or Backspace takes a line away, F8 clears them; a line is read once it has been shown. The
  status line of both windows tells their keys, its items are clicked as well; Backspace is the key a Mac calls delete.
- U in either window turns the times of all the watches over between the local zone and UTC, the status line tells
  which one it is and a layout keeps it. The times the windows show follow at once; a plugin that writes times into its
  status and lines takes them from `turbojedi.date()`, the ones written already stay as they are.
- A callback of `watch:follow()` that fails leaves its watch failed: it reads nothing more, its lines and the error stay in
  the list until it is removed. A watch whose journal has gone, a directory unmounted, is stopped the same way.
- Methods of a watch removed meanwhile, by the user in the list for one, do nothing.

### Gaps

Of `watch:follow()`. When the plugin takes the entries slower than they come and does not catch up for a minute, the watch jumps to the
end of the journal; when the entry it has read last is gone, a vacuum or a file deleted, it goes on after its time.
Either way `skipped(gap, watch, ...)` is told, and the list shows the gaps with the watch whether the plugin asks or not:

- `gap.since` and `gap.till` are microseconds of the entries around the part not seen. The range is rough: the clocks
  of a merged journal go their own ways. How many entries of the filter were there is not known.
- `gap.uncertain` is `true` when nothing may as well be missing: older entries are still there, only the one read
  last is not.

### Layouts

A layout keeps the watches that have a `context`, by File → Save layout and on exit in `last.jedi`, and gives them back
to their plugins when it is opened:

```lua
local function keep(context, position)
  local watch = turbojedi.watch({ title = 'context ' .. context.id, context = context })
  return watch:follow({ matches = { CONTEXT_UUID = context.id }, from = position }, callback)
end

-- A layout opened: the plugin decides whether the watch comes back and from where
turbojedi.registerRestore(function(context, position)
  keep(context, position)
end)
```

- `context` is data only: strings, numbers, booleans and tables of them, keys of strings and integers, up to 8 tables
  deep and 64 KiB. Anything else is an error at once, so a watch never drops out of a layout for its context.
- `position` is where a watch has stopped following, `nil` for one that has not followed a journal: the entry given to
  its callback last, kept by TurboJedi at the moment the layout is saved. With `from = position` the watch goes on after it and gets what has come while TurboJedi was closed;
  without it, from the end. The place means the entry has come to the callback, not that what the callback has started
  is done: a request of `fetch` not answered before the exit is not made again. A plugin that needs that keeps what is
  not confirmed in its context.
- The lines of the watches are not kept. A plugin that wants its own history keeps it in a file and its name in the
  context.
- Watches set while a plugin is loaded belong to it and stay when another layout is opened; the ones of actions and
  callbacks belong to the layout and go with it. A line of a plugin that is not there stays in the list as stopped and
  goes on into the next layout, until the plugin is back or the line is removed.

## Examples

### The unit and the process of an entry

`010-systemd.lua` of the package only looks, nothing is changed. For an entry of this machine:

- Status of the unit, `systemctl status`, in a window
- Copy status of the unit, the same text put to the clipboard
- Unit file of the unit, `systemctl cat`, in a window
- Process in `htop`, or in `top` without it, on the terminal, while the process of the entry still runs as the same
  program

```lua
local viewer = turbojedi.which('htop') and 'htop' or 'top'

local function top(pid)
  turbojedi.terminal { viewer, '-p', pid }
end

turbojedi.registerHandler(function(entry, menu, context)
  if context.source ~= 'local' then
    return
  end

  local pid  = entry._PID
  local name = entry._COMM

  if (type(pid) == 'string') and (type(name) == 'string') then
    local comm = io.open('/proc/' .. pid .. '/comm')

    if comm then
      local current = comm:read('l')
      comm:close()

      if current == name then
        menu:add('Process ' .. pid .. ' (' .. name .. ') in ' .. viewer, top, pid)
      end
    end
  end
end)
```

### Addresses

`020-network.lua` of the package gives one item to every address of the message, IPv4 or IPv6, and the actions are
chosen in a list there: Route (`ip route get`), Lookup (`host`, or `getent hosts` without it), Whois, Ping (four of them), Trace path
(`tracepath -n`) and Copy. Only the actions whose programs are installed are in the list, so the menu stays short when
an entry has several addresses.

### Core dumps

`030-coredump.lua` of the package acts on an entry of systemd-coredump. The PID and the time of the dump pick that dump
alone, a PID alone may stand for several: Core dump shows `coredumpctl info` in a window, Copy puts it to the clipboard,
Debug runs `coredumpctl debug` on the terminal when gdb is installed and the journal is of this machine, where the core
file is.

### fail2ban

`040-fail2ban.lua` of the package, for root where `fail2ban-client` is installed, offers Ban and Unban for every address
in the message. Ban asks for the jail, starting on `sshd` for an entry of sshd, `sshd-session` or `sshd-auth`, and for a
confirmation, which warns when the address is the one of your own SSH session: a ban of the wrong address locks its
user out. An address of this machine itself, such as the one sshd tells as `to`, gets no items. `fail2ban-client`
answers nobody but root, so a TurboJedi of another user has no items of it.

### Arrakis

`050-arrakis.lua` of the package explains the marked entries, or the entry and what has come before it, by a language
model behind the HTTP API of OpenAI or Anthropic that `arrakis.conf` tells; see README. Without the file it does
nothing. A plugin of `fetch()`, `query()`, a watch whose lines are the job and `message(text, title)`, the way a plugin
of your own may ask a service.

### SSH attacks

`/usr/share/turbojedi/examples/100-ssh.lua` watches the SSH server of this machine, where there is one, while TurboJedi
is open: what no single entry tells, an attack put together from many. Every address that fails to log in has a line in
the watch SSH attacks, the busiest on top: its failures in the last minute and in all, the user names it has tried, the
time of its last try; Enter goes to that entry. An address silent for ten minutes leaves the list. The status tells the
addresses, the failures of the last minute and the busiest one.

Two things bring the list of watches up with a bell: a login after failures of the same address, and an address that
tries more than 30 times a minute, once for it. A login from an address without failures is nobody's business. It only
looks: banning is the business of fail2ban.

One try may come in several lines, `Invalid user` and `Failed password for invalid user` of the same connection count
once; sshd of OpenSSH 9.8 and later logs as `sshd-session` and `sshd-auth`, all of them are watched. It is an example
since it watches and rings on every machine with sshd while TurboJedi is open.

A link to it turns it on and follows the updates of the package, for the whole machine:

```
sudo mkdir -p /usr/local/share/turbojedi/plugins
sudo ln -s /usr/share/turbojedi/examples/100-ssh.lua /usr/local/share/turbojedi/plugins/
```

or for one user alone:

```
mkdir -p ~/.config/turbojedi/plugins
ln -s /usr/share/turbojedi/examples/100-ssh.lua ~/.config/turbojedi/plugins/
```

TurboJedi reads plugins at start, it takes the plugin when started again. Removing the link takes it away, a link left
behind by a removed package is told about at start. A build installed by `make install` has the examples in
`/usr/local/share/turbojedi/examples`.

### Block devices

`/usr/share/turbojedi/examples/100-storage.lua` gives an item to every block device an entry of the kernel or smartd
names, `[sda]`, `dev nvme0n1`, `(sda1)`, when the system has it: Device (`lsblk`), Mounts (`findmnt`), udev
(`udevadm info`), SMART (`smartctl -a` of the disk, root) and Copy. It is an example since a name of a device comes up
in messages that tell nothing about it.

### Asterisk

`/usr/share/turbojedi/examples/100-asterisk.lua` gives an item to every object an entry of Asterisk names, with its
actions chosen in a list there:

| Object | As the entry has it | Actions |
|---|---|---|
| Call | `VERBOSE[1234][C-0000001a]` | History, Mark history, Previous mention, Copy |
| Channel | `PJSIP/2505-0000002b`, `Local/1@ctx-0000000a;1` | Show (`core show channel`), Hang up, and the ones of a call |
| Endpoint | the one of a PJSIP channel, `Endpoint 2505 is now ...` | Show (`pjsip show endpoint`), Qualify |
| AOR | `Contact 2505/sip:... is now ...` | Show (`pjsip show aor`) |
| Module | `res_stir_shaken` in a message of the loader or a module | Show (`module show like`), Load, Reload |
| Dialplan | `Executing [2505@from-internal:1]` | Show (`dialplan show`) |

The commands go to the Asterisk of this machine through `asterisk -rx`, root or the group asterisk; the CLI is the one
in the `PATH`, or of a build of its own in `/opt/asterisk/sbin`, `/usr/local/sbin` or `/opt/asterisk/bin`, out of the
`PATH` of `sudo`; the console tells which one, or that there is none. A journal of another
machine has only the actions of calls and channels in the journal. A value of the entry takes part in a command only
when the CLI would take it as one argument. Hang up, Load and Reload ask first; it is an example since they change the
PBX.

### Alerts

`/usr/share/turbojedi/examples/100-alert.lua` watches the journal by the rules of `~/.config/turbojedi/alert.conf`:
a watch for every rule, its count and last time in the status, the entries in its lines, the gaps said there too.
Without the file it does nothing.

```
[nginx restarts]
unit = nginx.service
text = Started

[disk errors]
identifier = kernel
text       = I/O error|EXT4-fs error
regex      = yes
raise      = yes
```

A rule takes `unit`, `identifier`, `priority` (up to a level: `err`, `warning`...), `match = FIELD=VALUE` (several
times), `text` with `regex = yes` and `case = yes`, and `raise = yes` to bring the list up with a bell for every entry.
It is linked as the one of SSH attacks above.

### Chat

`/usr/share/turbojedi/examples/100-chat.lua` sends the entry under the cursor, or the marked ones, to a chat through its
webhook: Slack, Mattermost, Rocket.Chat, Discord, Telegram or ntfy. Send to chat shows the line of the entry to add a
word to before it goes; Send N marked to chat asks first. Nothing waits for the chat, only a failure is told about.
It is an example since the entries leave the machine.

The chat is told by `~/.config/turbojedi/chat.conf` of the user who runs TurboJedi; without the file the plugin adds
nothing:

```
url    = https://chat.example.com/hooks/xxxxxxxxxxxxxxxx
format = slack
```

| `format` | `url` | more |
|---|---|---|
| `slack` | the incoming webhook of Slack, Mattermost or Rocket.Chat | |
| `discord` | the webhook of the channel | |
| `telegram` | `https://api.telegram.org/bot<token>/sendMessage` | `chat = <id of the chat>` |
| `ntfy` | `https://ntfy.sh/<topic>` or a server of your own | `token = <access token>` when the topic needs one |

`token` gives the header `Authorization: Bearer` to any of them. The file holds a secret: `chmod 600`. It is linked as
the one of SSH attacks above.

### A module of your own

`~/.config/turbojedi/plugins/lib/hosts.lua`:

```lua
local hosts = { }

function hosts.name(address)
  local text, how, code = turbojedi.capture({ 'getent', 'hosts', address }, { timeout = 3 })
  return (how == 'exit') and (code == 0) and text:match('^%S+%s+(%S+)') or nil
end

return hosts
```

`~/.config/turbojedi/plugins/200-hosts.lua`:

```lua
local hosts = require('hosts')

turbojedi.registerHandler(function(entry, menu)
  for found in turbojedi.matches(entry.MESSAGE, [[\b(?:\d{1,3}\.){3}\d{1,3}\b]]) do
    menu:add('Name of ' .. found[0], function(address)
      turbojedi.message(address .. ' is ' .. (hosts.name(address) or 'unknown'))
    end, found[0])
  end
end)
```
