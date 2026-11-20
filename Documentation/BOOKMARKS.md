# Bookmarks

What an investigation has found stays: an entry, or the entries you have selected, kept with a comment and a topic,
"the restart is the OOM killer of the backup at 03:00". A copy of every entry goes along, so a bookmark is there after
the journal has rotated. Months later you, or an agent, find it again: has this happened before, and what did I find
then? `010-bookmarks.lua` of the package keeps them in a base of SQLite of yours; it only keeps what you tell it,
nothing on the system changes.

## In Actions

The Actions menu of an entry, Alt-F10, A, F11 or a right click, tells its bookmark as soon as it opens:

| Item | |
|---|---|
| Comment | the entry has none: a line to type one into, which makes its bookmark |
| Comment: the OOM killer of ... | the comment of its bookmark: a line with it to edit; emptied, the comment goes and the topic stays |
| Set topic | the entry has no topic: the topics there are, the last one first, a new one typed, or none |
| Topic: backups | the topic of its bookmark, to change or to take off |
| Remove bookmark | the bookmark goes, its comment, its topic and the copies of its entries, after a question |
| Bookmark 7 selected entries | with entries selected or marked: the topic, then the comment, one new bookmark of all of them |
| Comment of the 7 selected: ... | the selection is just the entries of a bookmark: its Comment, Topic and Remove bookmark, no new one |
| Remove 3 selected entries from bookmarks | some of the selected entries are in bookmarks: they are taken out of them, after a question; a bookmark left with no entry goes |
| 2 more bookmarks of this entry | an entry in several bookmarks: a submenu of the others by their comments, each with Show, Comment, Topic and Remove bookmark; past 10 of them, All N of them... lists the rest |

A bookmark is there while it has a comment or a topic. Select with the mouse or Shift and the keys, or mark with M,
Insert or Ctrl+click, then right click, Bookmark: what the selection has found is kept. Selected again, the same entries
give that bookmark, so a comment changed is changed in it, not kept as a second one.

## In the Plugins menu

| Item | |
|---|---|
| Bookmarks ▸ All... | all of them |
| Bookmarks ▸ Without a topic... | the ones without a topic |
| Bookmarks ▸ Of a topic... | the ones of a topic, chosen in the list of the topics with how many bookmarks each has and the time of the last |
| Topics ▸ New topic... | a topic to choose for bookmarks later |
| Topics ▸ Rename... | a topic chosen in the list, its new name typed |
| Topics ▸ Remove... | a topic chosen in the list, its bookmarks kept without a topic or removed with it |
| Find in bookmarks... | the bookmarks whose topic, comment or entries have the words |

A list of bookmarks, the newest first, up to 500, has two items on top, and after them the ones other plugins add:

- **Show them in a watch**: a watch of F12, a line a bookmark, and Enter on a line shows the bookmark from the base, its
  comment and the copies of its entries, whether the journal has them still or not. The watch follows the changes of the
  bookmarks, a layout keeps it; remove it when you are done.
- **Mark them in the window**: their entries marked in the window you are in, its history brought in down to the oldest,
  the window at it. Ctrl-D then shows them alone, the marked lines without the ones between them; Ctrl-D again shows all.
  An entry the journal of the window does not have, rotated or of another machine, is told: Show has its copy.
- **Ask Arrakis about them**, when Arrakis is set up, see [ARRAKIS.md](ARRAKIS.md): the bookmarks with their times,
  topics and comments, and the copies of their entries, rotated ones included, go to the model, which tells what they
  have in common, whether what they have found comes back and when, and what to look at next.

A bookmark chosen gives a popup menu of Mark in the window, Show, Comment, Topic, Copy and Remove bookmark.

### Items of other plugins

The plugins share the globals of Lua, and `Bookmarks.items` is a list a plugin adds an item of a list of bookmarks to,
`{ title = ..., action = func }`. Chosen, it calls `func(bookmarks, what)` as an action: the bookmarks of the list,
the oldest first, each `{ id, created, topic, comment, left_out, entries }`, `created` in microseconds, `entries` the
copies as `query()` gives entries, every field as text; `what` names the list, `3 bookmarks all`. Whichever plugin
loads first makes the table, so the order of the plugins does not matter:

```lua
Bookmarks = Bookmarks or { }
Bookmarks.items = Bookmarks.items or { }
table.insert(Bookmarks.items, { title = 'Copy their messages', action = function(bookmarks)
  local lines = { }
  for _, bookmark in ipairs(bookmarks) do
    for _, entry in ipairs(bookmark.entries) do lines[#lines + 1] = entry.MESSAGE end
  end
  turbojedi.copy(table.concat(lines, '\n'))
end })
```

## For an agent

Over MCP, see [MCP.md](MCP.md), an agent reads the bookmarks, it never writes them:

| Method | |
|---|---|
| `topics` | the topics, how many bookmarks each has, the time of the last; how many have none |
| `bookmarks` | by `topic`, words in `text`, `since` and `until` in microseconds, `limit` and `offset`: id, time, topic, comment, host, how many entries, the first message; `total` |
| `bookmark` | one whole by `id`: its comment, topic, and its entries with their cursors and every field as JSON; `limit`, `offset` and `total` of the entries |

## The base

`~/.config/turbojedi/bookmarks.sqlite`, under `sudo` the one of the user of sudo. It is yours and outlives the plugin, so its
schema is told here, for `sqlite3` and anything else that reads it. `PRAGMA user_version` is its version; a later
TurboJedi brings an older base up to its own and never changes what a column means.

```sql
-- version 1
CREATE TABLE topics (
  id      INTEGER PRIMARY KEY,
  name    TEXT NOT NULL UNIQUE,
  created INTEGER NOT NULL);           -- microseconds since the epoch
CREATE TABLE bookmarks (
  id        INTEGER PRIMARY KEY,
  created   INTEGER NOT NULL,          -- microseconds since the epoch, to the millisecond
  changed   INTEGER NOT NULL,
  topic     INTEGER REFERENCES topics(id) ON DELETE SET NULL,
  comment   TEXT,                      -- NULL without one: a bookmark has a comment, a topic or both
  source    TEXT NOT NULL,             -- local, all or directory: the journal of the window it has been made in
  directory TEXT,                      -- the directory of that journal
  host      TEXT,                      -- _HOSTNAME of its first entry
  left_out  INTEGER NOT NULL DEFAULT 0);  -- entries of the set not kept
CREATE TABLE entries (
  id       INTEGER PRIMARY KEY,
  bookmark INTEGER NOT NULL REFERENCES bookmarks(id) ON DELETE CASCADE,
  cursor   TEXT NOT NULL,              -- __CURSOR, to find the entry while the journal has it
  time     INTEGER NOT NULL,           -- __REALTIME_TIMESTAMP
  message  TEXT,                       -- MESSAGE as text
  fields   TEXT NOT NULL);             -- every field of the entry, JSON
CREATE VIRTUAL TABLE texts USING fts5(topic, comment, entries);  -- rowid is the id of the bookmark
```

`fields` is an object of the fields of the entry. A field is its text; one that is not text of UTF-8 is
`{ "base64": ... }`; a field past 64 KiB is cut and says so, `{ "text": ..., "cut": N }` or `{ "base64": ..., "cut": N }`,
N the bytes it has had; a field the entry has more than once is a list of them. A bookmark keeps up to 1 MiB of copies:
of a larger selection the newest entries are kept, `left_out` tells how many are not. One entry larger than that by
itself has its fields cut shorter, by halves down to 256 bytes each, told by `cut` as above; an entry of so many fields
that even this is too much keeps its main ones, `MESSAGE`, `PRIORITY`, its source and its place, each cut to 256 bytes and
by its first value when the entry has it many times, and `__LEFT_OUT` tells how many values of fields are not kept. What is shown to you of a cut field
ends with "… (cut, N bytes)".

```
sqlite3 ~/.config/turbojedi/bookmarks.sqlite "SELECT datetime(b.created / 1000000, 'unixepoch'), t.name, b.comment
  FROM bookmarks b LEFT JOIN topics t ON t.id = b.topic ORDER BY b.created DESC LIMIT 20"
```

A change is one transaction: two TurboJedi of yours bookmarking at once both keep theirs, the second waits up to three
seconds for the first. The journal of SQLite is the rollback one, not WAL: after a change there is no file beside the
base.

### Under sudo

TurboJedi is root under `sudo`, the base is of the user. It is made by that user, 0600, never through a link, and opened
without following one: a link put in its place is refused, root never writes into the file it points to. Root then only
writes into a file of the user, which keeps its owner.

TurboJedi under `sudo` stopped in the middle of a change, killed or the machine gone, leaves `bookmarks.sqlite-journal` of
root beside the base, which SQLite needs to finish or undo that change. The next run under `sudo` does it by itself.
A run without `sudo` cannot read that file and leaves the base alone: the items of Bookmarks and the console tell to start
TurboJedi under `sudo` once, or to remove `bookmarks.sqlite-journal` and lose that one change.

### SQLite of TurboJedi

The bookmarks need no Lua module of the distribution: lsqlite3 0.9.6 is built into TurboJedi with the SQLite of the
system, see [BUILD.md](BUILD.md), and `require('lsqlite3')` gives it to any plugin, see [LUA.md](LUA.md).
