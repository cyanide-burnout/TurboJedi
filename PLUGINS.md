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
  `100-fail2ban.lua`. The name of a plugin is the name of its file without the number and `.lua`: `fail2ban`.
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
Under `sudo` it is root's, never the one of `SUDO_USER`.

Run by root, TurboJedi uses a plugin or a module only when root alone can change it: the file and every directory above
it, up to `/`, links resolved, belong to root, and the others cannot write to them. The group may write when it is
`root`, or `staff`, which Debian and Ubuntu give `/usr/local` so that its members install there: root runs what they
put to `/usr/local/bin` anyway. A plugin that is left out is told about with the directory that has let it out.

## How a plugin works

A plugin registers handlers when it is loaded. When the menu is opened, every handler is called with the entry and adds
the items it has for it, or none. When an item is chosen, its function is called.

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

Commands always run on this machine. A unit or a process of an entry of another machine, of a merged journal or of a
directory mounted over SSH or NFS, is not here: `_HOSTNAME` tells where it is, and an action may reach it by `ssh`.

Later versions may add fields to `context`, the marked lines among them, without changing the handlers there are.

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
| `message(text)` | action | a text with OK |
| `confirm(text)` | action | Yes or No: `true` or `false`. No is the default one, Esc is No |
| `input(prompt, value)` | action | a line to edit, starting with the value: the line, or `nil` when cancelled |
| `choose(title, items, initial)` | action | one of a list of strings: the item and its index, or `nil` when cancelled. `initial` is an item or an index to start on |
| `copy(text)` | action | puts the text to the clipboard the way Ctrl-Ins does |
| `open(options)` | action | a new window with a filter, like the one the menu has been opened in |
| `find(text, options)` | action | the previous match in the window of the menu, as Find: `true` when found |
| `seek(time)` | action | the window of the menu goes to a time, as Go to time |
| `query(options)` | handler, action | entries of a filter for the plugin to look into: a list, and why the reading has stopped |
| `mark(entries, options)` | action | marks the lines of entries in the window of the menu, as M does: the count marked |
| `execute(argv)` | action | runs a command in the background: `true`, or `nil` and the reason when it has not started |
| `output(argv)` | action | runs a command in a window of its own: `true`, or `nil` and the reason |
| `terminal(argv)` | action | runs a command on the terminal: `how, code` |
| `capture(argv, options)` | action | runs a command and waits for it: `output, how, code, errors` |
| `quote(argv)` | any time | the arguments as one line of `sh`, every one quoted when it has to be |
| `regex(text, pattern, flags)` | any time | the first match, or `nil` |
| `matches(text, pattern, flags)` | any time | an iterator over every match |
| `which(name)` | any time | the path of a program in `PATH`, or `nil` |

Called at the wrong time, a function is an error.

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
| `text` | the text filter, plain by default; with `regex = true` an expression of PCRE2, `exclude = true` leaves the matching entries out, `case = true` minds the case |
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
oldest one given, as far as the window holds, and the count of the entries marked tells when some are not there.

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
  colors of their escape sequences. The outcome comes at the end, `[exit 0]`. The window is not modal; closing it
  stops the command. Copy puts its text to the clipboard.
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

## Examples

### The unit and the process of an entry

`010-systemd.lua` of the package only looks, nothing is changed. For an entry of this machine:

- Status of the unit, `systemctl status`, in a window
- Copy status of the unit, the same text put to the clipboard
- Unit file of the unit, `systemctl cat`, in a window
- Process in `top`, on the terminal, while the process of the entry still runs as the same program

```lua
local function top(pid)
  turbojedi.terminal { 'top', '-p', pid }
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
        menu:add('Process ' .. pid .. ' (' .. name .. ') in top', top, pid)
      end
    end
  end
end)
```

### Addresses

`020-network.lua` of the package gives one item to every address of the message, IPv4 or IPv6, and the actions are
chosen in a list there: Route (`ip route get`), Name (`getent hosts`), Whois, Ping (four of them), Trace path
(`tracepath -n`) and Copy. Only the actions whose programs are installed are in the list, so the menu stays short when
an entry has several addresses.

### Core dumps

`030-coredump.lua` of the package acts on an entry of systemd-coredump. The PID and the time of the dump pick that dump
alone, a PID alone may stand for several: Core dump shows `coredumpctl info` in a window, Copy puts it to the clipboard,
Debug runs `coredumpctl debug` on the terminal when gdb is installed and the journal is of this machine, where the core
file is.

### Block devices

`/usr/share/turbojedi/examples/100-storage.lua` gives an item to every block device an entry of the kernel or smartd
names, `[sda]`, `dev nvme0n1`, `(sda1)`, when the system has it: Device (`lsblk`), Mounts (`findmnt`), udev
(`udevadm info`), SMART (`smartctl -a` of the disk, root) and Copy. It is an example since a name of a device comes up
in messages that tell nothing about it.

### fail2ban

`/usr/share/turbojedi/examples/100-fail2ban.lua` offers Ban and Unban for every address in the message. Ban asks for
the jail, starting on `sshd` for an entry of sshd, `sshd-session` or `sshd-auth`, and for a confirmation, which warns when the address is the one of
your own SSH session. It is an example to copy on purpose: a ban of the wrong address locks its user out.
`fail2ban-client` needs root.

A link to it turns it on and follows the updates of the package, for the whole machine:

```
sudo mkdir -p /usr/local/share/turbojedi/plugins
sudo ln -s /usr/share/turbojedi/examples/100-fail2ban.lua /usr/local/share/turbojedi/plugins/
```

or for root alone:

```
sudo mkdir -p /root/.config/turbojedi/plugins
sudo ln -s /usr/share/turbojedi/examples/100-fail2ban.lua /root/.config/turbojedi/plugins/
```

TurboJedi reads plugins at start, it takes the plugin when started again. The items are there when `fail2ban-client`
is in `PATH` and the message of the entry has an address. Removing the link takes it away, a link left behind by
a removed package is told about at start. A build installed by
`make install` has the example in `/usr/local/share/turbojedi/examples`.

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
