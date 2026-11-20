# D-Bus interface of TurboJedi

Every running TurboJedi offers its workspace on the system bus: the windows and their filters, the entry under the
cursor, the marked entries, queries of the journal, the watches of the plugins and their lines. A script, a monitor or
an AI agent through [turbojedi-mcp.py](MCP.md) reads what the user has in front of them and can point at things on their
screen: mark entries, put the cursor on one, take a window to a moment, open a window of its own.

The rule is: **the caller points, the user acts.** The calls of TurboJedi itself change nothing on the system. They do
not run the actions of the plugins or commands, do not touch the clipboard, do not change the filters of the user's
windows, and do not close windows or open layouts. `ListActions()` names what the Actions menu has for an entry, the
user chooses and runs it.

`Invoke()` is the one exception: it runs a method a plugin offers, and that is the code of the plugin, which may do
anything the plugin may. The bus does not hold back a method that changes the system; the files of allow decide who may
call `Invoke()` and which methods, and the effect a method tells, see Plugins below, lets a caller choose. The server of
MCP is such a caller: it gives an agent only the methods that read or change the screen.

## Names on the bus

Each instance takes the name `me.burnaway.TurboJedi.U<uid>.P<pid>` on the system bus, for example
`me.burnaway.TurboJedi.U1000.P4242`. The system bus is the one where the instances of root and of the users are seen
alike. The name is for finding an instance and nothing else. Trust does not rest on it, nobody checks the UID in it.

```
busctl --system list | grep me.burnaway.TurboJedi
```

A program lists the names with `ListNames()` of `org.freedesktop.DBus` and keeps the ones of the prefix. The policy lets
anyone take a name of the prefix, so a careful program keeps a name only when `GetConnectionUnixUser(name)` and
`GetConnectionUnixProcessID(name)` there give the UID and the PID the name says, as `turbojedi-mcp.py` does. This keeps
a process from passing for an instance of another user or another process; it does not prove that the owner is
TurboJedi.

### Policy

The bus lets a name be taken and called only when a policy allows it. The package installs
`/usr/share/dbus-1/system.d/me.burnaway.TurboJedi.conf`, `make install` puts the same file into
`/etc/dbus-1/system.d/`, as dbus does not read `/usr/local/share`. The policy lets anyone own a name of the prefix and
call the interface. What a caller may do is decided by TurboJedi itself.

dbus-daemon before 1.13.12, of Ubuntu 22.04 for one, knows no `send_destination_prefix` and refuses a file that has it.
Where the dbus-daemon of the build is older, `make install` puts `me.burnaway.TurboJedi.compat.conf` in its place under
the same name: the calls go by the interface of TurboJedi alone, and introspection and ping of its names are left out,
as they could not be told from the ones of other services there. `POLICY_FILE=` names one of the two by hand.

Without the policy TurboJedi works as before, without the interface, and says nothing about it. After the file is put
in place, `systemctl reload dbus` makes the bus read it. Instances started before that have no name and need a restart.

### Who is answered

On every call TurboJedi asks the bus for the UID of the caller, the real one, not the one in a name. It answers:

- its own user and root;
- for an instance of root started by `sudo`, also the user of `SUDO_UID`, for as long as this instance lives. The user
  could run `sudo` anyway, so this gives no new rights. If the rules of sudo change later, a running instance does not
  know it;
- an instance of root started without sudo answers root only.

Other users are answered when a file says so, a service of its own user for one:

- `/etc/turbojedi/allow.d/*.conf`, of root and of the packages, read in the order of their names;
- `$XDG_CONFIG_HOME/turbojedi/allow.conf` (`~/.config/turbojedi/allow.conf`) of the user of the instance, after them.

```
# A service that analyzes entries: reads and marks, does not move the windows
user=arrakis
methods=Describe,ListWindows,GetMarked,GetEntry,Query,Mark,Invoke:arrakis
```

`user` is a name or a UID, `group` a name or a GID for every member of the group, `methods` the ones it may call,
comma separated, for the `user` or `group` before them; without `methods` all of them. Members of a group are the ones
the system database tells (`getgrouplist()`, the primary group included), not the groups a process has taken since
its start. `Invoke`
lets it call every method of every plugin; `Invoke:arrakis` the methods of the plugin `arrakis` only,
`Invoke:arrakis.notify` that one method of it. A user named in several files gets what all of them give. The files
only add: the own user and root are answered whatever they say, and an instance of root reads `allow.conf` of root.
They are read at start; a mistake in them, a user, a group or an id the system does not know or a method there is not,
is told in the window of problems and that line is left out.

Anyone else gets `me.burnaway.TurboJedi.Error.Denied`.

## Object and interface

Object `/me/burnaway/TurboJedi`, interface `me.burnaway.TurboJedi`. `org.freedesktop.DBus.Introspectable` tells the
signatures.

Answers are plain D-Bus types, not JSON in a string: `busctl --json=pretty call` turns them into JSON as they are.

### Windows

A window of the journal is named by its `id`. The id stays the same for as long as the window is open and is never given
to another window. The number on the screen is not used for that: there are more windows than numbers, and the numbers
move when windows close.

### Entries

An entry is `a{sv}`, every field of it, as a plugin gets `entry`:

| Value | Type |
|---|---|
| a field once, UTF-8 | `s` |
| a field several times | `as`, or `aay` when one of the values is not UTF-8 |
| a field that is not UTF-8 or holds NUL, binary | `ay` |
| `__REALTIME_TIMESTAMP`, `__MONOTONIC_TIMESTAMP` | `t`, microseconds |
| `__CURSOR` | `s` |

An entry that is not there is an empty dictionary.

## Methods

### Reading

These calls are answered at once, also while the user has a dialog open.

| Method | Signature | |
|---|---|---|
| `Describe()` | `→ a{sv}` | `pid` u, `uid` u, `user` s, `host` s, `version` s, `started` t (microseconds), `api` u; `sudo` u, the user of `SUDO_UID` an instance of root answers as well; what the user has in front, see below |
| `ListWindows()` | `→ aa{sv}` | the windows of the journal, the front one first, see below |
| `GetCursor(id)` | `u → a{sv}` | the entry under the cursor of the window, the one Enter and the actions take |
| `GetMarked(id, limit)` | `uu → (aa{sv}u)` | the marked and selected entries of the window, the oldest first, up to `limit` (0 gives 1000), and how many there are in all, as `context.marked` of a plugin |
| `GetEntry(id, cursor)` | `us → a{sv}` | the entry of a cursor in the journal of the window: of a line of a watch, of an answer of a query |
| `Query(options)` | `a{sv} → (aa{sv}s)` | entries of the journal, as `turbojedi.query()`; see below |
| `ListActions(id, cursor)` | `us → aa{sv}` | the items of the Actions menu for the entry: `title` s, `plugin` s, in the order of the menu; an item of a submenu has `path` as as well, the titles of the submenus it is in, the outer one first: `Show` of `["Context 2222"]` |
| `ListWatches()` | `→ aa{sv}` | the watches of the plugins, see below |
| `GetWatchLines(id)` | `u → aa{sv}` | the lines of a watch, the newest first, see below |
| `ListMethods()` | `→ aa{sv}` | the methods of the plugins for `Invoke()` the caller may call, see Plugins below |

A window of `ListWindows()`:

| Key | Type | |
|---|---|---|
| `id` | u | of the window, for the other calls |
| `window` | i | its number on the screen, 0 without one |
| `title` | s | as the frame shows it, without the signs of following and the order |
| `directory` | s | the journal files of a directory, empty for the journal of the machine |
| `merge` | b | the journals of every machine |
| `text`, `regex`, `exclude`, `case` | s, b, b, b | the text of the filter and how it is matched |
| `levels` | s | the priorities shown, `EACRWNID` with `-` for the hidden ones |
| `matches` | as | FIELD=VALUE of the command line and of the plugins |
| `unit`, `identifier`, `command`, `transport`, `boot`, `user`, `machine` | s | the fields of Source, only the ones set |
| `pid` | u | Source, when set |
| `since`, `until` | s | the range as given, when set |
| `following` | b | the window follows the end of its journal |
| `selected` | u | entries marked and selected |
| `active` | b | the window the user has in front; none is while a timeline or the watches are in front |

What the user has in front, in `Describe()`:

| Key | Type | |
|---|---|---|
| `front` | s | `journal`, a window of the journal; `timeline`; `watches`, the list of watches; `watch`, the lines of a watch; empty without a window |
| `window` | u | `journal`: its `id` |
| `title` | s | `journal` and `timeline`: the title of the window |
| `watch` | u | `watches`: the watch under the cursor, 0 without one; `watch`: the watch whose lines these are |
| `line` | t | `watch`: the `number` of the line under the cursor, 0 without one |
| `dialog` | b | a dialog or a menu is open over it: the calls that change the screen wait |

A timeline in front also tells what it counts and what the user looks at, for `Query()` to read the entries of it:

| Key | Type | |
|---|---|---|
| `directory` … `until` | | its filter, the keys of a window of `ListWindows()` from `directory` to `until` |
| `step` | t | microseconds of an interval |
| `counted_since`, `counted_until` | s | the intervals counted, from the start of the first one to the last microsecond of the last one |
| `interval_since`, `interval_until` | s | the interval under the cursor, or the selection |
| `intervals` | u | how many intervals these are |
| `selection` | b | they are the selection |
| `errors`, `warnings`, `other` | t | entries in them: errors and worse, warnings, the rest |

The times are in the zone of the timeline as `Query()` takes them, `2026-11-16 03:12:00` and
`2026-11-16 03:12:59.999999`, or with `UTC` at the end when the timeline counts hours of UTC: given as
`since` and `until` with the filter, a query reads the entries of the bars. Without anything counted yet only the filter
is told.

`Describe()` also tells `descriptors` u, the files the process has open, and `descriptor_limit` u, its soft limit: every
window and every watch keeps every file of its journal open, the archives included.

`ListActions()` calls the handlers of the plugins as opening the menu does, and never the functions of the items. The
functions of TurboJedi give the handlers no dialogs and no commands in that phase, and `query()` has the 200 ms budget of
a menu. A handler is still Lua with its standard libraries: it is code of a plugin, trusted as the plugins are, and is to
make the menu and nothing else, as [LUA.md](LUA.md#how-a-plugin-works) asks. The call waits while a dialog is open or Lua is busy,
as the calls of the next section do. An entry that is not in the journal of the window gives
`me.burnaway.TurboJedi.Error.NoEntry`.

The console of TurboJedi is one of them: `plugin` `turbojedi`, `key` `console`, its lines `print()` and `log()` of the
plugins, their errors, the problems of the start and the calls of the screen of this interface. An agent reads there what
has gone wrong.

A watch of `ListWatches()`:

| Key | Type | |
|---|---|---|
| `id` | u | of the watch, for `GetWatchLines()`, the same for as long as the instance lives |
| `plugin`, `key`, `title` | s | who has set it and what it is; `key` is empty when the plugin has given none |
| `state` | s | `active`, `stopped` or `failed` |
| `status`, `reason` | s | the status the plugin tells; why it is stopped or failed |
| `gaps` | u | gaps told so far |
| `unread` | u | lines not looked at |
| `latest` | t | microseconds of the last line, 0 before |

A line of `GetWatchLines()`: `number` t, `moment` t (microseconds it has been added at), `text` s, `unread` b, and
`cursor` s with `realtime` t of its entry when it has one. `GetEntry()` gives the fields of the entry, `GoTo()` shows it.

### Query

`Query(options)` reads the way `turbojedi.query()` of a plugin does. The journal applies the matches, the text is
matched against `identifier[pid]: message` as in a window.

| Option | Type | |
|---|---|---|
| `id` | u | the journal of this window, its directory and merge; the journal of the machine without it |
| `matches` | as | FIELD=VALUE, the same field twice is OR, different fields are AND |
| `text` | s | text of the message |
| `regex`, `exclude`, `case` | b | the text is a regular expression of PCRE2; the entries that match are left out; the case matters |
| `priority` | s | up to the level: `emerg`, `alert`, `crit`, `err`, `warning`, `notice`, `info`, `debug` or `0`..`7` |
| `since`, `until` | s | times as `--since` takes them |
| `before`, `after` | s | a cursor: entries older than its entry going back, or newer going on |
| `limit` | u | entries kept, 100 by default |
| `scan` | u | entries looked at, 100000 by default |

Without `before` or `after` the query goes from the newest entry back. Numbers may also come as `i`, `t` or `x`.
Options of other names are left alone.

The answer is the entries and why the query has stopped: `limit`, `scan`, `time`, or empty when the journal or the range
has ended. A query is read by steps between the events of the screen, the windows go on meanwhile. Up to 4 queries go on
at once, a fifth one gets `Busy`.

### The screen

These calls change only what the user sees. They wait while the user has a dialog open or a plugin runs: `GoTo()`
should not pull a window away from under Find or a question the user is answering. Once no dialog is open, the call is
done. A window is looked for again then, the user may have closed it meanwhile.

| Method | Signature | |
|---|---|---|
| `Mark(id, cursors, clear)` | `uasb → (us)` | marks the lines of the entries, as M and `mark()` of a plugin do; `clear` takes the other marks off first. The history comes in down to the oldest of them. The count of the entries marked, and `time` when the history has not come in to the end |
| `GoTo(id, cursor)` | `us → (bs)` | the cursor of the window on the entry, the window in front, as Enter in the lines of a watch; whether the entry is found, and `time` when the history has not come in to it. Not found with no reason: the entry is not in the journal of the window, the cursor is near its time |
| `Seek(id, time)` | `ut → (bs)` | the window to a moment, microseconds of CLOCK_REALTIME, as Ctrl-G and `seek()` of a plugin; the window pauses, following the end again is up to the user. Whether it has reached the moment, and `time` when it has not |
| `OpenWindow(options)` | `a{sv} → u` | a new window of the journal with a filter: the options of `Query()` but `before`, `after`, `limit` and `scan`, the journal of the window `id` or of the machine. The `id` of the new window |

To set several windows to the same moment, call `Seek()` for each.

Whatever the bus has done on the screen shows in the status line: `API: marked 12 entries in window 1`,
`API: cursor on an entry in window 2`, `API: window 2 at 2026-11-16 03:12:00`, `API: opened window 4`. The line stays
until the user presses a key or a mouse button, so someone who has been away sees what has happened. Reading leaves no
trace. It says API, not agent: any allowed script may call.

### Plugins

A plugin offers methods of its own by `on('invoke')` ([LUA.md](LUA.md#methods-for-the-bus)), for programs that tell it something or ask it
what only it knows: a service that has analyzed entries, a script of cron, an agent.

| Method | Signature | |
|---|---|---|
| `Invoke(plugin, method, values)` | `ssa{sv} → a{sv}` | calls the method of the plugin with the values as a table, the table it returns is the answer, empty without one. `plugin` is the name of its file without the number and `.lua`, `fail2ban` of `060-fail2ban.lua`, as `ListActions()` and `ListWatches()` tell it |

The method runs as an action of a window does: it waits while a dialog is open or Lua runs something else, as the
calls of the screen do, and may open a dialog itself, a question to the user. Then the answer comes once the user has
answered, and the caller waits that long; a call that has to come back at once is better answered by the plugin
first and asked later, by a timer of its own. The status line tells `API: called notify of plugin arrakis`.

The keys of `values` beginning with `_` are of TurboJedi, not of the method, which never gets them:

| Key | Type | |
|---|---|---|
| `_window` | u | the `id` of a window of `ListWindows()`: the method runs as an action of that window, its queries, marks and watches take its journal. Without it, the journal of this machine and no window. No window of that `id` gives `NoWindow` |

Another key beginning with `_` gives `InvalidArgs`, so a caller learns that an instance does not know it yet. A method
that has told its arguments, see `ListMethods()`, gets its values checked before it runs: a key it has not told, a
value of another type or a required one missing gives `InvalidArgs` with the name. An integer of another type is taken
when its value fits, any number for `d`.

Values of the answer are of the types of Lua: a string `s`, `true`/`false` `b`, an integer `i` while it fits, `t`
above, `x` below, a float `d`; any other type is given as `{ type = 'as', value = { ... } }`, as `dbus()` takes a
variant, `aa{sv}` for a list of records. No plugin or no method of that name gives `NoMethod`, an error of the method
`Failed` with its message.

A method of `ListMethods()`, of the plugins in the order of their names:

| Key | Type | |
|---|---|---|
| `plugin`, `method` | s | what `Invoke()` takes |
| `description` | s | what it does and gives back, as the plugin tells it; empty when it tells nothing |
| `effect` | s | `none`, it only reads; `screen`, it changes what the user sees, windows, marks, watches; `system`, it changes the machine or a service, and so is every method that has not told its effect |
| `declared` | b | it has told its arguments and its values are checked by them; any values go to it otherwise |
| `arguments` | aa{sv} | `name` s, `type` s (`s`, `u`, `i`, `t`, `x`, `b`, `d`, `as`), `description` s, `required` b |

A caller limited by the files of allow sees only the methods its `Invoke:` permissions give, and needs `ListMethods`
there to ask. The effect is what the plugin promises: a caller decides by it what to call, the server of MCP offers an
agent only the methods of `none` and `screen` with a description, see MCP.md.

### Time of a call

A call has 30 seconds from the moment it comes, the wait for a dialog to close and the work together:

- a dialog open for the whole time gives `Busy`, "a dialog is open in TurboJedi";
- a query or the history of a window not read through in time gives what has come so far and `time`;
- the queries of a method of `Invoke()` read until the 30 seconds are over and stop with `time` then, `left()` of LUA.md
  tells the method what is left. A method that runs past them still answers; the console tells it.

The default timeout of a D-Bus call is 25 seconds, so a caller sets a longer one, 40 seconds for example:
`busctl --timeout=40`, `timeout=40` of dbus-python. The answer then always comes before the caller stops waiting.

## Errors

| Error | |
|---|---|
| `me.burnaway.TurboJedi.Error.Denied` | the caller is not the user of the instance, nor root, nor a user the files of allow let call this method |
| `me.burnaway.TurboJedi.Error.NoWindow` | no window of that `id`, or it has been closed while the call waited |
| `me.burnaway.TurboJedi.Error.NoWatch` | no watch of that `id` |
| `me.burnaway.TurboJedi.Error.NoEntry` | `ListActions()`: the entry is not in the journal of the window |
| `me.burnaway.TurboJedi.Error.Busy` | 4 queries go on already; a dialog has been open for the whole time of the call; TurboJedi exits |
| `me.burnaway.TurboJedi.Error.NoMethod` | `Invoke()`: no plugin of that name, or it has no such method |
| `me.burnaway.TurboJedi.Error.Failed` | `Invoke()`: the method has failed, the message tells its error |
| `me.burnaway.TurboJedi.Error.Invalid` | an option is wrong: a match, a regular expression, a time, a priority, a cursor without its time |
| `me.burnaway.TurboJedi.Error.InvalidArgs` | `Invoke()`: a value the method has not told or of another type, a required one missing, a key beginning with `_` TurboJedi does not know |

The message of an error tells the reason in words.

## Version

`api` of `Describe()` is 1. Within a version, fields of the dictionaries, options and methods are only added: a caller
leaves alone what it does not know. A change that breaks a caller, such as a field removed, its type changed or a
signature changed, comes with a new version.

## Examples

### busctl

```
busctl --system call me.burnaway.TurboJedi.U1000.P4242 /me/burnaway/TurboJedi me.burnaway.TurboJedi ListWindows
busctl --system --json=pretty call me.burnaway.TurboJedi.U1000.P4242 /me/burnaway/TurboJedi me.burnaway.TurboJedi GetCursor u 1
busctl --system --timeout=40 call me.burnaway.TurboJedi.U1000.P4242 /me/burnaway/TurboJedi me.burnaway.TurboJedi \
  Query 'a{sv}' 3 matches as 1 _SYSTEMD_UNIT=nginx.service priority s err limit u 20
busctl --system --timeout=40 call me.burnaway.TurboJedi.U1000.P4242 /me/burnaway/TurboJedi me.burnaway.TurboJedi \
  Seek ut 1 1794812400000000
busctl --system --json=pretty call me.burnaway.TurboJedi.U1000.P4242 /me/burnaway/TurboJedi me.burnaway.TurboJedi ListMethods
busctl --system --timeout=40 call me.burnaway.TurboJedi.U1000.P4242 /me/burnaway/TurboJedi me.burnaway.TurboJedi \
  Invoke 'ssa{sv}' tetrapack sessions 2 id u 2621234 _window u 1
```

### Python

With dbus-python (`python3-dbus`, `python3-dbus-python` of openSUSE, `python-dbus` of Arch):

```python
import dbus

bus = dbus.SystemBus()
names = [name for name in bus.list_names() if name.startswith('me.burnaway.TurboJedi.')]
jedi = dbus.Interface(bus.get_object(names[0], '/me/burnaway/TurboJedi'), 'me.burnaway.TurboJedi')

for window in jedi.ListWindows():
    print(window['id'], window['title'])

entries, stopped = jedi.Query({'matches': dbus.Array(['_SYSTEMD_UNIT=nginx.service'], signature='s'),
                               'priority': 'err', 'limit': dbus.UInt32(20)}, timeout=40)

for entry in entries:
    print(entry['__REALTIME_TIMESTAMP'], entry.get('MESSAGE'))

marked, reason = jedi.Mark(1, [entry['__CURSOR'] for entry in entries], True, timeout=40)
```
