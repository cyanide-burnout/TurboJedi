# Plugins of TurboJedi

The Actions menu of an entry is made by plugins in Lua 5.4. A plugin looks at the entry under the cursor and offers
what can be done with it: the status of its unit, a ban of an address in its message, a command of your own with a
value of one of its fields. TurboJedi itself knows nothing of the services: it gives the fields of the entry, the menu,
dialogs and ways to run commands.

Alt-F10 opens the menu, as the local menu of the IDEs of Borland, also F11 as in Far, Shift-F10 and a right click on a
line; Journal → Actions... is in the menu bar. A plain A opens it as well, as M marks: Terminal of macOS keeps F10 and
F11 for itself and has no Alt unless Option is set to be Meta. Every item shows the plugin it comes from on its right, the items of two
plugins are parted by a line.

What a plugin does without an entry, a search by an identifier the user types for one, goes to the Plugins menu of the
menu bar, Alt-P, see [The Plugins menu](LUA.md#the-plugins-menu).

## Plugins of the package

The package brings these, in `/usr/share/turbojedi/plugins`:

- `010-bookmarks.lua`: bookmarks of entries with a comment and a topic, kept with copies of the entries after the
  journal has rotated, looked up and shown from the Plugins menu, read by an agent; see [BOOKMARKS.md](BOOKMARKS.md)
- `020-arrakis.lua`, where `arrakis.conf` tells a language model: the marked entries, or the entry and what has come
  before it, explained by the model, and in the Plugins menu the errors of the window; see [ARRAKIS.md](ARRAKIS.md)
- `030-systemd.lua`: the status of the unit of the entry in a window or to the clipboard, its state and since when
  told in the title of the item by systemd over D-Bus, its unit file, its process in htop, or in top without it
- `040-network.lua`: a submenu for every address of the message, its route, lookup, whois, ping, path or a copy, the
  ones whose programs are installed
- `050-coredump.lua`: for an entry of systemd-coredump, what coredumpctl knows of that dump, a copy of it, its stack in
  gdb
- `060-fail2ban.lua`, for root where fail2ban is installed: a submenu for an address of the entry, the jails it is
  banned in now, Ban and Unban of it, after asking for the jail and a confirmation, never for an address of this machine

Plugins that change things, or that would come up by chance, are examples in `/usr/share/turbojedi/examples` and are not
run by themselves: `100-ssh.lua`, a watch of the attacks on the SSH server of this machine while TurboJedi is open, an
address a line with its failures and the user names it has tried, a login after failures or a flood of tries bringing
the list of watches up with a bell; `100-storage.lua`, the device, mounts, udev and SMART of a block device an
entry of the kernel or smartd names; `100-asterisk.lua`, the calls, channels, endpoints, AORs, modules and dialplan of an entry
of Asterisk, shown, traced in the journal, hung up or reloaded through `asterisk -rx`, the calls of a number and a watch
of a call, in the Plugins menu and for an agent; `100-alert.lua`, watches of the
journal by rules of `~/.config/turbojedi/alert.conf`, while TurboJedi is open; `100-chat.lua`, the entry or the
marked ones sent to Slack, Mattermost, Rocket.Chat, Discord, Telegram or ntfy through a webhook told in
`~/.config/turbojedi/chat.conf`.

A link to an example turns it on and follows the updates of the package, for the whole machine:

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

## Where plugins are

| Directory | |
|---|---|
| `/usr/share/turbojedi/plugins` | of the package |
| `/usr/local/share/turbojedi/plugins` | put on the machine by hand |
| `$XDG_CONFIG_HOME/turbojedi/plugins` | of the user, `~/.config/turbojedi/plugins` without XDG_CONFIG_HOME |

All three are read whatever way TurboJedi is built and installed. There are no plugins in `/etc`: it is for settings,
not for code.

- Every `*.lua` file of them is run once at start, in the order of the names: `030-systemd.lua` before
  `060-fail2ban.lua`, and their items come in that order in the menus. The name of a plugin is the name of its file
  without the number and `.lua`: `fail2ban`.
- A file of a plugin in a directory lower in the table takes the place of the plugin of that name above, whatever the
  numbers: `050-fail2ban.lua` of the user takes the place of `060-fail2ban.lua` of the package. The numbers of the
  package may change, as they have in 0.18, and a version or a mask of the user keeps working. Two files of one plugin
  in one directory are told at the start, the later one by name is taken.
- A plugin may be a link to a file elsewhere, an example of the package among them; under root the file it points to
  is the one checked. An empty file, or a link to `/dev/null`, of a plugin leaves it out, the way a unit of systemd is
  masked. A link to a file that is gone is told about.
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

## Writing a plugin

The interface of Lua, the menus, dialogs, commands, requests over HTTP and D-Bus, watches, and the plugins of the
package as examples of it: [LUA.md](LUA.md).
