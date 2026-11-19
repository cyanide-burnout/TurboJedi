# TurboJedi for AI agents

`turbojedi-mcp.py` gives an AI agent, such as Claude Code or Codex, the workspace you have open in TurboJedi. It is a
server of the Model Context Protocol. The agent sees what you see: your windows and their filters, the entry under the
cursor, the entries you have marked, the watches of the plugins and their lines. It can also query the journal the way
a window reads it. Ask about an entry or a time, and the agent answers with your journal in front of it.

The rule is: **the agent points, you act.** The agent can show you what it has found:

- mark entries in a window;
- put the cursor on an entry;
- take a window to a moment;
- open a window of its own;
- set a watch a plugin offers for it, such as the watch of a context of BrandMeister or TetraPack.

Each of these shows in the status line, as `API: marked 12 entries in window 1`, until you press a key. The agent
changes nothing on the machine. It cannot:

- run the actions of the plugins or commands: it names the items of the Actions menu, you choose and run them;
- call a method of a plugin that changes the system, or one that does not tell what it does;
- remove a watch: only you remove them, in the list of watches;
- touch the clipboard;
- change the filters of your windows;
- close windows or open layouts.

There is no AI on the server and no port to open. The agent runs where you work and starts the server over SSH:

```
agent ── stdio ── ssh server turbojedi-mcp.py ── system bus ── TurboJedi
```

SSH gives the authentication and the encryption. The server lives as long as the session of the agent.

## On the server

- The package of TurboJedi: it has `turbojedi-mcp.py` in `/usr/bin` and the policy of the bus,
  `/usr/share/dbus-1/system.d/me.burnaway.TurboJedi.conf`.
- Python 3 and dbus-python, which the packages recommend:

| Distribution | |
|---|---|
| Debian, Ubuntu | `sudo apt install python3-dbus` |
| Fedora, RHEL, Amazon Linux | `sudo dnf install python3-dbus` |
| openSUSE | `sudo zypper install python3-dbus-python` |
| Arch Linux | `sudo pacman -S python-dbus` |

`make install` puts the script into `/usr/local/bin` and the policy into `/etc/dbus-1/system.d/`, as dbus does not
read `/usr/local/share`. After the policy is in place for the first time, `sudo systemctl reload dbus` makes the bus
read it, and TurboJedi is started again: an instance takes its name on the bus when it starts.

Nothing has to be set up in TurboJedi: every instance is on the bus by itself.

## Connecting the agent

On the machine of the agent, with `server` being how you reach the server by SSH:

Claude Code:

```
claude mcp add turbojedi -- ssh server turbojedi-mcp.py
```

Codex:

```
codex mcp add turbojedi -- ssh server turbojedi-mcp.py
```

or in `~/.codex/config.toml`:

```toml
[mcp_servers.turbojedi]
command = "ssh"
args = ["server", "turbojedi-mcp.py"]
```

Any other client that takes a command:

```json
{"command": "ssh", "args": ["server", "turbojedi-mcp.py"]}
```

On the same machine, without SSH: `claude mcp add turbojedi -- turbojedi-mcp.py`, and the same way for the others.

For several servers, add one entry for each, such as `turbojedi-web` and `turbojedi-db`.

### SSH without questions

The standard input and output of SSH belong to the protocol, so nobody can answer a question of a password or of an
unknown host key. Use a key, loaded into ssh-agent when it has a passphrase, and connect once by hand to accept the host
key. A host of `~/.ssh/config` keeps the command short:

```
Host web
    HostName web.example.com
    User admin
    BatchMode yes
```

`ssh web turbojedi-mcp.py --version` checks that it works: it prints the version and ends.

## Whose TurboJedi

The agent works as the user it comes in as over SSH, and sees:

- the instances of TurboJedi of this user;
- the instances of root this user has started with `sudo`, for as long as each of them runs. The user could run `sudo`
  anyway, so the agent gets no new rights. If the rules of sudo change, a running instance does not know it;
- an agent that comes in as root sees every instance.

The instances of other users do not answer: the agent is told how many there are, not what they show.

## What the agent can do

| Tool | |
|---|---|
| `list_instances` | the instances it sees, with their windows and the methods their plugins offer |
| `describe` | an instance: PID, user, host, version, start time, and what you have in front: a window of the journal, a timeline with its filter, its range and the interval under the cursor or the selection, the watches or the lines of one, and whether a dialog is open |
| `list_windows` | the windows of an instance, their filters, whether they follow the end, how many entries are marked, which one is in front |
| `get_cursor` | the entry under the cursor of a window, of the one in front by default |
| `get_marked` | the entries marked or selected in a window, of the one in front by default |
| `get_entry` | an entry by its cursor, with every field |
| `query_journal` | entries of the journal by fields, text, priority and time, of the journal of a window or of the machine |
| `list_actions` | the items of the Actions menu for an entry, the one under the cursor of the window in front by default, to name the one that helps |
| `list_watches` | the watches of the plugins, their status and unread lines |
| `get_watch_lines` | the lines of a watch with their entries, of the one in front by default, and the line under your cursor |
| `mark_entries` | marks entries in a window, as M does |
| `go_to_entry` | the cursor of a window on an entry, the window in front |
| `seek_window` | a window to a moment, as Ctrl-G does; the window stops following the end |
| `open_window` | a new window with a filter |
| `list_methods` | the methods plugins offer an agent: what each does, its effect and the schema of its values |
| `invoke` | calls one of them in a window, the one in front by default |

`mark_entries`, `go_to_entry`, `seek_window` and `open_window` change your screen. They wait while you have a dialog open, up to 30 seconds, so they never pull a window
from under Find or a question you are answering. A query reads for up to 30 seconds and gives what it has found by then;
the windows go on meanwhile.

### Methods of the plugins

A plugin may offer methods for an agent, see [LUA.md](LUA.md#methods-for-the-bus): it knows its service, so it finds
what a query of the journal alone does not. `list_instances` names them with each instance, so the agent sees them at
its first step, and `list_methods` gives their schemas. Offered are the ones that tell what they do and whose effect is
`none`, they only read, or `screen`, they change what you see, as a watch set. A method that changes the system is not
offered, whatever the agent asks: `invoke` asks TurboJedi again before every call and refuses it. The effect is what the
plugin promises; TurboJedi does not check its code, which runs with your rights as the plugin does anyway.

`invoke` runs the method as an action of a window: its queries read the journal of that window, a watch it sets follows
it. Without `window_id`, the window you have in front, or the journal of the machine when no window of the journal is
in front. The values are checked by the schema of the method, by the server and again by TurboJedi.

The plugins of BrandMeister and TetraPack offer:

| Method | Effect | |
|---|---|---|
| `sessions` | none | the sessions of an ID in the journal of the core, the newest first |
| `contexts` | none | the contexts of an ID: the ones the core has now, for a window of this machine, then the ones of the journal |
| `identify` | none | what a UUID is, a context or a session, and its newest mention |
| `context` | none | the card of a context as the core has it now, for a window of this machine |
| `watch` | screen | the watch of a context from its first mention on, its sessions a line each; of a session as well in TetraPack |

The example of Asterisk, `/usr/share/turbojedi/examples/100-asterisk.lua`, offers the same for calls: `calls` of a
number, made or taken, `call`, its channels and the steps of its dialplan, and `watch` of a call. Ask "find the calls
of 2505 today and put a watch on the one still going".

The calls the agent makes are the D-Bus interface of [DBUS.md](DBUS.md), which a script of your own can use as well.

## What to ask

- "What happened to nginx around 03:10? Mark it in window 1."
- "Why has the entry under my cursor come? Look at what that unit wrote before it."
- "What is this line of the watch I am on about?"
- "What does the SSH attacks watch show? Which address is the busiest?"
- "Open a window of the errors of the last hour from every unit except cron."
- "Which actions does the entry under the cursor have? Which one should I run?"
- "Set every window to the time of the first error of postgresql today."
- "Find the contexts of 2501 and put a watch on the one of the repeater, I want to follow it."

## When it does not work

| The agent says | |
|---|---|
| dbus-python is not installed | install it as above, on the server |
| no instance, the policy is not installed | the package is not installed, or `make install` without `systemctl reload dbus`; the path is in the message |
| no instance, TurboJedi is not running | start TurboJedi on the server; an instance started before the policy has no name and needs a restart |
| instances of other users do not answer | they belong to another user: come in as that user, or start TurboJedi with `sudo` as yourself |
| Busy: a dialog is open | a dialog of TurboJedi was open for the whole 30 seconds: close it, and the agent tries again |
| TurboJedi has not answered | the instance is stuck or the call took longer than 40 seconds |
| an instance is outdated | it was started before TurboJedi was updated and lacks what is new: restart TurboJedi |
| an instance is unsupported | it speaks a newer D-Bus interface than this `turbojedi-mcp.py`: update the package on the server |

When the agent does not start the server at all, run its command by hand, such as `ssh server turbojedi-mcp.py
--version`: SSH tells what is wrong.
