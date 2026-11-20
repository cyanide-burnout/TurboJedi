-- TurboJedi, Turbo Journald Interface
-- https://github.com/cyanide-burnout/TurboJedi
-- Artem Prilutskiy, 2026

-- Bookmarks: an entry, or the selected ones, kept with a comment and a topic in a base of SQLite of the user, a copy of
-- every entry along, so that they are there after the journal has rotated. The Actions menu tells and changes the
-- bookmark of the entry under the cursor, the Plugins menu looks them up, marks them in the window, shows them in a
-- watch and keeps the topics; an agent reads them. It only keeps what the user says, nothing on the system changes.
-- The schema of the base is in BOOKMARKS.md, for anything else that reads it

local cjson = require('cjson')
local loaded, sqlite = pcall(require, 'lsqlite3')

if not loaded then
  turbojedi.log('lsqlite3 is not there, nothing to offer: ' .. tostring(sqlite), { problem = true })
  return
end

local NAME     = 'bookmarks.sqlite'
local VERSION  = 1             -- Of the schema, PRAGMA user_version
local FIELD    = 64 * 1024     -- Bytes a field is kept up to, cut beyond it with the bytes it has had
local BOOKMARK = 1024 * 1024   -- Bytes of copies a bookmark is kept up to, the newest entries of a set first
local LIST     = 500           -- Bookmarks a list of the menu shows, the newest
local TITLE    = 60            -- Characters of a comment in the title of an item of a menu
local OTHERS   = 10            -- Other bookmarks of an entry in its submenu of Actions, the rest in a list
local NOFOLLOW = 0x01000000    -- SQLITE_OPEN_NOFOLLOW: a link in the place of the base is refused
local ENOENT   = 2             -- errno of a file that is not there, whatever language the system tells it in
local SMALLEST = 256           -- Bytes a field of an entry larger than BOOKMARK is cut down to at most
local MAIN     = { '__CURSOR', '__REALTIME_TIMESTAMP', 'MESSAGE', 'PRIORITY', 'SYSLOG_IDENTIFIER', '_SYSTEMD_UNIT', '_HOSTNAME',
                   '_PID', '_COMM', '_BOOT_ID' }  -- The fields kept of an entry too large even then

local date = turbojedi.date or os.date
local path = turbojedi.where(NAME)

if not path then
  turbojedi.log('there is no directory of settings, HOME is not set: nothing to offer')
  return
end

-- The base

local base       -- Open once it is needed, nil when it cannot be
local failure    -- Why it cannot be
local recent     -- The topic chosen last, offered first

-- A transaction of a run under sudo stopped in the middle leaves a -journal of root SQLite has to roll back: root does it
-- by itself, a run of the user cannot read it, so the base is left alone and the user told what to do
local function stuck()
  local file, _, code = io.open(path .. '-journal', 'rb')

  if file then
    file:close()
    return nil
  end

  if code == ENOENT then
    return nil
  end

  return 'a run of TurboJedi under sudo has been stopped in the middle of a change of the bookmarks: start it under ' ..
         'sudo once to finish that change, or remove ' .. path .. '-journal and lose it'
end

local function prepare(text, ...)
  local statement = base:prepare(text)

  if not statement then
    error(base:errmsg(), 0)
  end

  if select('#', ...) > 0 then
    statement:bind_values(...)
  end

  return statement
end

-- The rows of a query as tables by the names of the columns
local function query(text, ...)
  local rows      = { }
  local statement = prepare(text, ...)

  for row in statement:nrows() do
    rows[#rows + 1] = row
  end

  statement:finalize()
  return rows
end

local function run(text, ...)
  local statement = prepare(text, ...)
  local result    = statement:step()

  statement:finalize()

  if (result ~= sqlite.DONE) and (result ~= sqlite.ROW) then
    error(base:errmsg(), 0)
  end
end

-- A change is one transaction: two instances of one user wait for each other, neither loses its change
local function change(func, ...)
  if base:exec('BEGIN IMMEDIATE') ~= sqlite.OK then
    return nil, base:errmsg()
  end

  local done, result = pcall(func, ...)

  if done and (base:exec('COMMIT') == sqlite.OK) then
    return result or true
  end

  -- A failure of the change or of its commit: nothing of it stays, the transaction does not stay open either
  result = done and base:errmsg() or tostring(result)
  base:exec('ROLLBACK')
  return nil, result
end

local SCHEMA = [[
CREATE TABLE IF NOT EXISTS topics (
  id      INTEGER PRIMARY KEY,
  name    TEXT NOT NULL UNIQUE,
  created INTEGER NOT NULL);
CREATE TABLE IF NOT EXISTS bookmarks (
  id        INTEGER PRIMARY KEY,
  created   INTEGER NOT NULL,
  changed   INTEGER NOT NULL,
  topic     INTEGER REFERENCES topics(id) ON DELETE SET NULL,
  comment   TEXT,
  source    TEXT NOT NULL,
  directory TEXT,
  host      TEXT,
  left_out  INTEGER NOT NULL DEFAULT 0);
CREATE TABLE IF NOT EXISTS entries (
  id       INTEGER PRIMARY KEY,
  bookmark INTEGER NOT NULL REFERENCES bookmarks(id) ON DELETE CASCADE,
  cursor   TEXT NOT NULL,
  time     INTEGER NOT NULL,
  message  TEXT,
  fields   TEXT NOT NULL);
CREATE INDEX IF NOT EXISTS entries_cursor ON entries(cursor);
CREATE INDEX IF NOT EXISTS entries_bookmark ON entries(bookmark);
CREATE INDEX IF NOT EXISTS bookmarks_topic ON bookmarks(topic);
CREATE INDEX IF NOT EXISTS bookmarks_created ON bookmarks(created);
CREATE VIRTUAL TABLE IF NOT EXISTS texts USING fts5(topic, comment, entries);
]]

-- The base of the user: made by settings() as that user, never through a link; opened without following one, with the
-- journal of rollback and not WAL, so that no file of root stays beside it under sudo
local function open()
  if base or failure then
    return base, failure
  end

  failure = stuck()

  if failure then
    turbojedi.log(failure, { problem = true })
    return nil, failure
  end

  local file, _, code = io.open(path, 'rb')

  if file then
    file:close()
  elseif code == ENOENT then
    local made, why = turbojedi.settings(NAME, '', { private = true })
    if not made then
      failure = 'the base cannot be made: ' .. tostring(why)
      return nil, failure
    end
  end

  local handle, code, message = sqlite.open(path, sqlite.OPEN_READWRITE + NOFOLLOW)

  if not handle then
    failure = ('the base %s cannot be opened: %s'):format(path, tostring(message or code))
    turbojedi.log(failure, { problem = true })
    return nil, failure
  end

  base = handle
  base:busy_timeout(3000)

  local version = query('PRAGMA user_version')[1]

  if (base:exec('PRAGMA foreign_keys = ON; PRAGMA journal_mode = DELETE;') ~= sqlite.OK) or
     ((version.user_version < VERSION) and (base:exec(SCHEMA .. ('PRAGMA user_version = %d;'):format(VERSION)) ~= sqlite.OK)) then
    failure = ('the base %s cannot be made ready: %s'):format(path, base:errmsg())
    base:close()
    base = nil
    turbojedi.log(failure, { problem = true })
    return nil, failure
  end

  if version.user_version > VERSION then
    failure = ('the base %s is of a newer TurboJedi, version %d of the schema'):format(path, version.user_version)
    base:close()
    base = nil
    return nil, failure
  end

  return base
end

-- Copies of the entries

local ALPHABET = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/'

local function base64(data)
  local parts = { }

  for index = 1, #data, 3 do
    local a, b, c = data:byte(index, index + 2)
    local number  = a * 65536 + (b or 0) * 256 + (c or 0)
    local four    = { }

    for place = 1, 4 do
      local digit = (number >> (6 * (4 - place))) & 63
      four[place] = ALPHABET:sub(digit + 1, digit + 1)
    end

    if not b then four[3] = '=' end
    if not c then four[4] = '=' end
    parts[#parts + 1] = table.concat(four)
  end

  return table.concat(parts)
end

-- A field as it is kept: its text, { base64 } when it is not text of UTF-8, and cut past the limit, FIELD by default,
-- with the bytes it has had
local function keep(value, most)
  if type(value) ~= 'string' then
    return value
  end

  local size = #value

  most = most or FIELD

  if utf8.len(value) then
    if size <= most then
      return value
    end

    -- Cut by whole characters
    local text = value:sub(1, most)
    while not utf8.len(text) do text = text:sub(1, -2) end
    return { text = text, cut = size }
  end

  local limit = (most // 4) * 3

  if size <= limit then
    return { base64 = base64(value) }
  end

  return { base64 = base64(value:sub(1, limit)), cut = size }
end

local function fields(entry, most, only)
  local kept = { }

  for name, value in pairs(entry) do
    if (not only) or only[name] then
      if type(value) == 'table' then
        -- A field the entry has more than once
        local list = { }
        for index, item in ipairs(value) do list[index] = keep(item, most) end
        kept[name] = list
      else
        kept[name] = keep(value, most)
      end
    end
  end

  return kept
end

-- The copy of an entry within BOOKMARK however large it is: its fields cut shorter, by halves down to SMALLEST; an entry
-- of so many fields that this is not enough keeps its main ones, and says how many are left out
local function copy(entry)
  local most = FIELD
  local json = cjson.encode(fields(entry))

  while (#json > BOOKMARK) and (most > SMALLEST) do
    most = math.max(SMALLEST, most // 2)
    json = cjson.encode(fields(entry, most))
  end

  if #json > BOOKMARK then
    -- The main fields, a field there many times by its first value: some hundreds of bytes each, whatever the entry
    local only, count, kept = { }, 0, { }
    for _, name in ipairs(MAIN) do only[name] = true end

    for name, value in pairs(entry) do
      if not only[name] then
        count = count + ((type(value) == 'table') and #value or 1)
      elseif type(value) == 'table' then
        count = count + #value - 1
        kept[name] = keep(value[1], SMALLEST)
      else
        kept[name] = keep(value, SMALLEST)
      end
    end

    kept.__LEFT_OUT = count
    json = cjson.encode(kept)
  end

  return json
end

-- The text of a field as the user sees it, cut ones told so
local function told(value)
  if type(value) ~= 'table' then
    return tostring(value)
  end

  if value.base64 then
    return ('(binary, base64) %s%s'):format(value.base64, value.cut and (' … (cut, %d bytes)'):format(value.cut) or '')
  end

  if value.text then
    return value.text .. (value.cut and (' … (cut, %d bytes)'):format(value.cut) or '')
  end

  local list = { }
  for index, item in ipairs(value) do list[index] = told(item) end
  return table.concat(list, ' | ')
end

-- Lua tells the time by seconds, SQLite by milliseconds: bookmarks of one second keep their order; the base is open
local function microseconds()
  return query("SELECT CAST(ROUND((julianday('now') - 2440587.5) * 86400000.0) AS INTEGER) * 1000 AS now")[1].now
end

-- Titles of items cut to a width by whole characters
local function cut(text, width)
  text = tostring(text or ''):gsub('%s+', ' ')

  if utf8.len(text) and (utf8.len(text) > width) then
    return text:sub(1, utf8.offset(text, width) - 1) .. '…'
  end

  return text
end

-- Topics

local function topics()
  return query([[SELECT t.id, t.name, COUNT(b.id) AS count, MAX(b.created) AS last FROM topics t
                 LEFT JOIN bookmarks b ON b.topic = t.id GROUP BY t.id ORDER BY MAX(b.created) DESC, MAX(b.id) DESC, t.name]])
end

-- The id of a topic of that name, made when it is not there; nil for none
local function topic(name)
  if (not name) or (name == '') then
    return nil
  end

  run('INSERT OR IGNORE INTO topics(name, created) VALUES (?, ?)', name, microseconds())
  return query('SELECT id FROM topics WHERE name = ?', name)[1].id
end

-- A topic chosen: one there is, the last one first, a new one typed, or none. false when cancelled, '' for none
local function choose(current)
  local list    = topics()
  local names   = { 'No topic', 'New topic...' }
  local initial = 1

  for index, item in ipairs(list) do
    names[index + 2] = item.name
    if item.name == (current or recent) then initial = index + 2 end
  end

  local _, index = turbojedi.choose('Topic', names, initial)

  if not index then return false end
  if index == 1 then return '' end

  if index == 2 then
    local name = turbojedi.input('New topic', '')
    name = name and name:match('^%s*(.-)%s*$')
    if (not name) or (name == '') then return false end
    recent = name
    return name
  end

  recent = names[index]
  return names[index]
end

-- Bookmarks

local showing = { }  -- Watches of Show by their keys, to fill anew when the bookmarks change
local fill           -- Of a watch of Show, below

local function refresh()
  for _, item in pairs(showing) do
    item.watch:clear()
    fill(item.watch, item.scope)
  end
end

-- The text the full-text index has of a bookmark: its topic, its comment, and the messages and the sources of its entries
local function reindex(id)
  local row = query([[SELECT b.comment, t.name AS topic FROM bookmarks b LEFT JOIN topics t ON t.id = b.topic
                      WHERE b.id = ?]], id)[1]
  local parts = { }

  for _, entry in ipairs(query('SELECT message, fields FROM entries WHERE bookmark = ?', id)) do
    local kept = cjson.decode(entry.fields)
    parts[#parts + 1] = table.concat({ told(kept.SYSLOG_IDENTIFIER or ''), told(kept._SYSTEMD_UNIT or ''), told(kept._HOSTNAME or ''),
                                       entry.message or '' }, ' ')
  end

  run('DELETE FROM texts WHERE rowid = ?', id)
  run('INSERT INTO texts(rowid, topic, comment, entries) VALUES (?, ?, ?, ?)', id, row.topic or '', row.comment or '',
      table.concat(parts, '\n'))
end

-- A new bookmark of the entries, the oldest first as the window gives them; past BOOKMARK the newest are kept, the rest
-- told by left_out along with the ones the window has not given
local function create(entries, context, comment, name, beyond)
  return change(function()
    local now   = microseconds()
    local kept  = { }
    local bytes = 0
    local left  = beyond or 0

    for number = #entries, 1, -1 do
      local entry = entries[number]
      local json  = copy(entry)

      if (bytes + #json > BOOKMARK) and (#kept > 0) then
        left = left + number
        break
      end

      bytes = bytes + #json
      table.insert(kept, 1, { entry = entry, json = json })
    end

    run([[INSERT INTO bookmarks(created, changed, topic, comment, source, directory, host, left_out)
          VALUES (?, ?, ?, ?, ?, ?, ?, ?)]], now, now, topic(name), (comment ~= '') and comment or nil,
        context.source or 'local', context.directory, kept[1] and kept[1].entry._HOSTNAME, left)

    local id = base:last_insert_rowid()

    for _, item in ipairs(kept) do
      run('INSERT INTO entries(bookmark, cursor, time, message, fields) VALUES (?, ?, ?, ?, ?)', id,
          tostring(item.entry.__CURSOR), tonumber(item.entry.__REALTIME_TIMESTAMP) or 0,
          told(keep(tostring(item.entry.MESSAGE or ''))), item.json)
    end

    reindex(id)
    return id
  end)
end

-- A bookmark is there while it has a comment or a topic
local function settle(id)
  local row = query('SELECT comment, topic FROM bookmarks WHERE id = ?', id)[1]

  if row and (not row.comment) and (not row.topic) then
    run('DELETE FROM texts WHERE rowid = ?', id)
    run('DELETE FROM bookmarks WHERE id = ?', id)
  end
end

local function comment(id, text)
  return change(function()
    run('UPDATE bookmarks SET comment = ?, changed = ? WHERE id = ?', (text ~= '') and text or nil, microseconds(), id)
    reindex(id)
    settle(id)
  end)
end

local function retopic(id, name)
  return change(function()
    run('UPDATE bookmarks SET topic = ?, changed = ? WHERE id = ?', topic(name), microseconds(), id)
    reindex(id)
    settle(id)
  end)
end

local function remove(id)
  return change(function()
    run('DELETE FROM texts WHERE rowid = ?', id)
    run('DELETE FROM bookmarks WHERE id = ?', id)
  end)
end

local function reported(done, reason)
  if not done then turbojedi.message('The bookmark is not kept: ' .. tostring(reason), 'Bookmarks') end
  refresh()
  return done
end

-- A bookmark as text: its comment, its topic, and the copies of its entries
local function describe(id)
  local row = query([[SELECT b.*, t.name AS topic_name FROM bookmarks b LEFT JOIN topics t ON t.id = b.topic
                      WHERE b.id = ?]], id)[1]

  if not row then
    return nil
  end

  local lines = { ('Bookmark of %s%s'):format(date('%F %T', row.created // 1000000), row.topic_name and (', ' .. row.topic_name) or ''),
                  row.comment or '(no comment)', '' }

  for _, entry in ipairs(query('SELECT * FROM entries WHERE bookmark = ? ORDER BY time, id', id)) do
    local kept = cjson.decode(entry.fields)
    lines[#lines + 1] = ('%s %s %s: %s'):format(date('%F %T', entry.time // 1000000), told(kept._HOSTNAME or ''),
      told(kept.SYSLOG_IDENTIFIER or kept._COMM or ''), told(kept.MESSAGE or ''))
  end

  if row.left_out > 0 then
    lines[#lines + 1] = ('… %d entries of the set are not kept, a bookmark keeps up to 1 MiB of copies'):format(row.left_out)
  end

  return table.concat(lines, '\n'), row
end

-- The entries of bookmarks marked in the window of the menu and the window at the oldest of them; told when the journal
-- of the window has not all of them
local function mark(ids)
  local cursors, oldest = { }, nil

  for _, id in ipairs(ids) do
    for _, entry in ipairs(query('SELECT cursor, time FROM entries WHERE bookmark = ?', id)) do
      cursors[#cursors + 1] = entry.cursor
      oldest = math.min(oldest or entry.time, entry.time)
    end
  end

  if #cursors == 0 then
    return turbojedi.message('These bookmarks have no entries', 'Bookmarks')
  end

  local count, reason = turbojedi.mark(cursors, { clear = true })

  if not count then
    return turbojedi.message(reason, 'Bookmarks')
  end

  if count > 0 then
    turbojedi.seek(oldest)
  end

  if count < #cursors then
    turbojedi.message(('%d of %d entries are in the journal of this window, the rest are copies in the bookmarks: ' ..
                       'Show tells them'):format(count, #cursors), 'Bookmarks')
  end
end

-- The actions of a bookmark found in the Plugins menu
local function act(id)
  local text, row = describe(id)

  if not text then
    return turbojedi.message('The bookmark is not there anymore', 'Bookmarks')
  end

  local _, choice = turbojedi.popup({ 'Mark in the window', 'Show', 'Comment', 'Topic', 'Copy', 'Remove bookmark' },
    'Bookmark of ' .. date('%F %T', row.created // 1000000))

  if choice == 1 then
    mark({ id })
  elseif choice == 2 then
    turbojedi.message(text, 'Bookmark')
  elseif choice == 3 then
    local typed = turbojedi.input('Comment', row.comment or '')
    if typed then reported(comment(id, typed:match('^%s*(.-)%s*$'))) end
  elseif choice == 4 then
    local name = choose(row.topic_name)
    if name then reported(retopic(id, name)) end
  elseif choice == 5 then
    turbojedi.copy(text .. '\n')
  elseif (choice == 6) and turbojedi.confirm('Remove this bookmark?\n\n' .. cut(row.comment or text, 200)) then
    reported(remove(id))
  end
end

-- What a scope of bookmarks is: all, without a topic, a topic, the ones a text finds
local function pick(scope, limit, offset)
  local where, values = '1 = 1', { }

  if scope.topic then
    where, values = 'b.topic = (SELECT id FROM topics WHERE name = ?)', { scope.topic }
  elseif scope.none then
    where = 'b.topic IS NULL'
  elseif scope.text then
    -- A phrase of FTS5, its quotes doubled
    where, values = 'b.id IN (SELECT rowid FROM texts WHERE texts MATCH ?)', { '"' .. scope.text:gsub('"', '""') .. '"' }
  end

  if scope.since then where = where .. (' AND b.created >= %d'):format(scope.since) end
  if scope.till then where = where .. (' AND b.created <= %d'):format(scope.till) end

  local total = query('SELECT COUNT(*) AS count FROM bookmarks b WHERE ' .. where, table.unpack(values))[1].count
  local rows  = query(([[SELECT b.*, t.name AS topic_name, (SELECT COUNT(*) FROM entries e WHERE e.bookmark = b.id) AS count,
                                (SELECT message FROM entries e WHERE e.bookmark = b.id ORDER BY time, id LIMIT 1) AS message
                         FROM bookmarks b LEFT JOIN topics t ON t.id = b.topic WHERE %s
                         ORDER BY b.created DESC, b.id DESC LIMIT %d OFFSET %d]]):format(where, limit or LIST, offset or 0), table.unpack(values))
  return rows, total
end

local function label(scope)
  if scope.topic then return scope.topic end
  if scope.none then return 'without a topic' end
  if scope.text then return 'of "' .. scope.text .. '"' end
  return 'all'
end

-- A watch of Show: a line a bookmark, the newest on top, Enter shows the bookmark from the base
fill = function(watch, scope)
  local rows, total = pick(scope, LIST)

  for number = #rows, 1, -1 do
    local row = rows[number]
    watch:add(('%s  %s%s'):format(date('%F %T', row.created // 1000000), row.topic_name and ('[' .. row.topic_name .. '] ') or '',
      cut(row.comment or row.message, 120)), nil, { quiet = true, action = function()
        local text = describe(row.id)
        turbojedi.message(text or 'The bookmark is not there anymore', 'Bookmark')
      end })
  end

  watch:status(('%d bookmark%s%s'):format(total, (total == 1) and '' or 's', (total > #rows) and (', the newest ' .. #rows .. ' shown') or ''))
end

local function show(scope)
  local key = 'bookmarks:' .. (scope.topic and ('topic:' .. scope.topic) or scope.none and 'none' or scope.text and ('text:' .. scope.text) or 'all')

  if showing[key] then
    return turbojedi.message('They are shown already: F12, Bookmarks ' .. label(scope), 'Bookmarks')
  end

  local watch = turbojedi.watch({ title = 'Bookmarks ' .. label(scope), key = key, context = scope, window = 'new',
    removed = function() showing[key] = nil end })

  showing[key] = { watch = watch, scope = scope }
  fill(watch, scope)
end

-- A list of bookmarks, Show and Mark of all of them first
-- Other plugins add items to a list of bookmarks, they share the globals of Lua: an item of Bookmarks.items,
-- { title = ..., action = func }, calls func(bookmarks, what) with the bookmarks of the list, the oldest first, each
-- { id, created, topic, comment, count, left_out, entries }, and what names the list. entries(limit, most) reads the
-- first limit entries of the bookmark from the base, all of them without it, as query() gives them with every field as
-- text; a copy past most bytes gives its message cut to most characters, its time and cursor alone: a list of
-- hundreds of bookmarks of 1 MiB each is not read whole for an item that takes a few entries of each. Arrakis adds Ask
-- Arrakis about them. Made by whichever plugin comes first, read as a list opens
Bookmarks       = Bookmarks       or { }
Bookmarks.items = Bookmarks.items or { }

local function collect(rows)
  local bookmarks = { }

  for number = #rows, 1, -1 do
    local row = rows[number]

    local function entries(limit, most)
      local list  = { }
      local count = math.max(0, math.tointeger(limit) or row.count)
      local size  = math.tointeger(most)

      -- A copy past most bytes is not decoded: its message cut to most characters by SQLite, its time and cursor, the
      -- rest of its fields left in the base. Not even its host: the one of the bookmark is of its first entry, and an
      -- entry of another machine would be told of the wrong one
      local rows = size and query(('SELECT cursor, time, substr(message, 1, %d) AS message, CASE WHEN length(CAST(fields AS BLOB)) <= %d '
                                   .. 'THEN fields END AS fields FROM entries WHERE bookmark = ? ORDER BY time, id LIMIT %d'):format(size, size, count), row.id)
                        or query(('SELECT cursor, time, fields FROM entries WHERE bookmark = ? ORDER BY time, id LIMIT %d'):format(count), row.id)

      for _, entry in ipairs(rows) do
        local item = { }

        if entry.fields then
          for name, value in pairs(cjson.decode(entry.fields)) do item[name] = told(value) end
        else
          item.MESSAGE = entry.message
        end

        item.__REALTIME_TIMESTAMP, item.__CURSOR = entry.time, entry.cursor
        list[#list + 1] = item
      end

      return list
    end

    bookmarks[#bookmarks + 1] = { id = row.id, created = row.created, topic = row.topic_name, comment = row.comment,
                                  count = row.count, left_out = row.left_out, entries = entries }
  end

  return bookmarks
end

local function list(scope)
  local rows, total = pick(scope, LIST)

  if total == 0 then
    return turbojedi.message('No bookmarks ' .. label(scope), 'Bookmarks')
  end

  -- The items of the other plugins after the two of its own
  local names, others = { 'Show them in a watch', 'Mark them in the window' }, { }

  for _, item in ipairs(Bookmarks.items) do
    if (type(item) == 'table') and (type(item.title) == 'string') and (type(item.action) == 'function') then
      others[#others + 1] = item
      names[#names + 1]   = item.title
    end
  end

  local top = #names

  for _, row in ipairs(rows) do
    names[#names + 1] = ('%s  %s%s'):format(date('%F %T', row.created // 1000000), (scope.topic or not row.topic_name) and '' or ('[' .. row.topic_name .. '] '),
      cut(row.comment or row.message, 80))
  end

  local _, choice = turbojedi.choose(('Bookmarks %s: %d%s'):format(label(scope), total, (total > #rows) and (', the newest ' .. #rows) or ''), names)

  if choice == 1 then
    show(scope)
  elseif choice == 2 then
    local ids = { }
    for _, row in ipairs(rows) do ids[#ids + 1] = row.id end
    mark(ids)
  elseif choice and (choice <= top) then
    others[choice - 2].action(collect(rows), ('%d bookmarks %s'):format(#rows, label(scope)))
  elseif choice then
    act(rows[choice - top].id)
  end
end

-- The Plugins menu

local function unavailable()
  local _, reason = open()
  turbojedi.message('The bookmarks cannot be used: ' .. tostring(reason), 'Bookmarks')
end

local function find()
  if not open() then return unavailable() end
  local text = turbojedi.input('Find in bookmarks', '')
  text = text and text:match('^%s*(.-)%s*$')
  if text and (text ~= '') then list({ text = text }) end
end

-- A topic chosen from the list of the topics, with how many bookmarks each has and the time of the last: its name
local function topical(title)
  local list_of = topics()
  local names   = { }

  if #list_of == 0 then
    return turbojedi.message('There are no topics yet', 'Bookmarks')
  end

  for index, item in ipairs(list_of) do
    names[index] = ('%s (%d%s)'):format(item.name, item.count, item.last and (', last ' .. date('%F %T', item.last // 1000000)) or '')
  end

  local _, choice = turbojedi.choose(title, names)
  return choice and list_of[choice].name
end

-- The Plugins menu: Bookmarks and Topics, submenus of what is done, then the list of the bookmarks or the topics
local function all()
  if open() then list({ }) else unavailable() end
end

local function untopical()
  if open() then list({ none = true }) else unavailable() end
end

local function ofTopic()
  if not open() then return unavailable() end
  local name = topical('Bookmarks of the topic')
  if name then list({ topic = name }) end
end

local function newTopic()
  if not open() then return unavailable() end
  local name = turbojedi.input('New topic', '')
  name = name and name:match('^%s*(.-)%s*$')
  if name and (name ~= '') then reported(change(topic, name)) end
end

local function renameTopic()
  if not open() then return unavailable() end
  local name = topical('Rename the topic')
  if not name then return end
  local new = turbojedi.input('Rename ' .. name .. ' to', name)
  new = new and new:match('^%s*(.-)%s*$')
  if (not new) or (new == '') or (new == name) then return end
  reported(change(function()
    run('UPDATE topics SET name = ? WHERE name = ?', new, name)
    for _, row in ipairs(query('SELECT b.id FROM bookmarks b JOIN topics t ON t.id = b.topic WHERE t.name = ?', new)) do reindex(row.id) end
  end))
end

local function removeTopic()
  if not open() then return unavailable() end
  local name = topical('Remove the topic')
  if not name then return end
  local _, how = turbojedi.popup({ 'Keep its bookmarks without a topic', 'Remove its bookmarks as well' }, 'Remove the topic ' .. name)
  if not how then return end
  reported(change(function()
    local ids = query('SELECT b.id FROM bookmarks b JOIN topics t ON t.id = b.topic WHERE t.name = ?', name)
    if how == 2 then
      for _, row in ipairs(ids) do
        run('DELETE FROM texts WHERE rowid = ?', row.id)
        run('DELETE FROM bookmarks WHERE id = ?', row.id)
      end
    end
    run('DELETE FROM topics WHERE name = ?', name)
    if how == 1 then
      for _, row in ipairs(ids) do reindex(row.id) settle(row.id) end
    end
  end))
end

-- The Actions menu: the bookmark of the entry under the cursor, its comment, its topic and its removal; and a bookmark
-- of the selection when there is one

local function set(entries, context, beyond)
  local name = choose()
  if name == false then return end
  local typed = turbojedi.input('Comment', '')
  if not typed then return end
  typed = typed:match('^%s*(.-)%s*$')
  if (typed == '') and (name == '') then return end
  reported(create(entries, context, typed, name, beyond))
end

-- The items of a bookmark in Actions, of the entry under the cursor or of the selection: its comment, its topic and its
-- removal; without one, a comment or a topic makes it
local function offer(menu, mine, whose, make)
  if mine then
    menu:add((mine.comment and ('Comment%s: %s'):format(whose, cut(mine.comment, TITLE))) or ('Comment' .. whose), function()
      local typed = turbojedi.input('Comment', mine.comment or '')
      if typed then reported(comment(mine.id, typed:match('^%s*(.-)%s*$'))) end
    end)
    menu:add((mine.topic and ('Topic%s: %s'):format(whose, cut(mine.topic, TITLE))) or ('Set topic' .. whose), function()
      local name = choose(mine.topic)
      if name then reported(retopic(mine.id, name)) end
    end)
    menu:add('Remove bookmark' .. whose, function()
      if turbojedi.confirm(('Remove the bookmark%s?\n\n%s'):format(whose, cut(mine.comment or mine.topic, 200))) then reported(remove(mine.id)) end
    end)
    return
  end

  menu:add('Comment' .. whose, function()
    local typed = turbojedi.input('Comment', '')
    typed = typed and typed:match('^%s*(.-)%s*$')
    if typed and (typed ~= '') then reported(make(typed, '')) end
  end)
  menu:add('Set topic' .. whose, function()
    local name = choose()
    if name and (name ~= '') then reported(make('', name)) end
  end)
end

local function bookmark(id)
  return query('SELECT b.id, b.comment, t.name AS topic FROM bookmarks b LEFT JOIN topics t ON t.id = b.topic WHERE b.id = ?', id)[1]
end

-- The entries of the cursors taken out of every bookmark they are in; a bookmark left without entries goes
local function unbookmark(cursors)
  local places = ('?,'):rep(#cursors):sub(1, -2)

  return change(function()
    local ids = query('SELECT DISTINCT bookmark FROM entries WHERE cursor IN (' .. places .. ')', table.unpack(cursors))

    run('DELETE FROM entries WHERE cursor IN (' .. places .. ')', table.unpack(cursors))

    for _, row in ipairs(ids) do
      if query('SELECT COUNT(*) AS count FROM entries WHERE bookmark = ?', row.bookmark)[1].count == 0 then
        run('DELETE FROM texts WHERE rowid = ?', row.bookmark)
        run('DELETE FROM bookmarks WHERE id = ?', row.bookmark)
      else
        reindex(row.bookmark)
      end
    end
  end)
end

local function actions(entry, menu, context)
  if not open() then return end

  if context.marked and (#context.marked > 0) then
    -- The selection: the bookmark of just these entries when there is one, so that a second comment changes it and
    -- makes no other; a new one otherwise, and the entries of it taken out of their bookmarks
    local count   = context.selected or #context.marked
    local whole   = (count == #context.marked)
    local cursors = { }

    for _, item in ipairs(context.marked) do
      if type(item.__CURSOR) == 'string' then cursors[#cursors + 1] = item.__CURSOR end
    end

    local places = ('?,'):rep(#cursors):sub(1, -2)
    local same, kept = nil, 0

    if #cursors > 0 then
      for _, row in ipairs(query(('SELECT e.bookmark AS id, COUNT(*) AS hit, (SELECT COUNT(*) FROM entries x WHERE x.bookmark = e.bookmark) AS size '
                                  .. 'FROM entries e WHERE e.cursor IN (%s) GROUP BY e.bookmark ORDER BY e.bookmark DESC'):format(places), table.unpack(cursors))) do
        if whole and (not same) and (row.hit == #cursors) and (row.size == #cursors) then same = row.id end
      end

      kept = query(('SELECT COUNT(DISTINCT cursor) AS count FROM entries WHERE cursor IN (%s)'):format(places), table.unpack(cursors))[1].count
    end

    local whose = (' of the %d selected'):format(count)

    if same then
      offer(menu, bookmark(same), whose)
    else
      menu:add(('Bookmark %d selected entr%s'):format(count, (count == 1) and 'y' or 'ies'), set, context.marked, context, math.max(0, count - #context.marked))
    end

    if (kept > 0) and not same then
      menu:add(('Remove %d selected entr%s from bookmarks'):format(kept, (kept == 1) and 'y' or 'ies'), function()
        if turbojedi.confirm(('Take %d entr%s out of their bookmarks? A bookmark left without entries goes.'):format(kept, (kept == 1) and 'y' or 'ies')) then
          reported(unbookmark(cursors))
        end
      end)
    end

    return
  end

  -- The entry under the cursor: the newest bookmark it is in, a new one of it otherwise
  local own = (type(entry.__CURSOR) == 'string') and query([[SELECT b.id, b.comment, t.name AS topic FROM entries e
    JOIN bookmarks b ON b.id = e.bookmark LEFT JOIN topics t ON t.id = b.topic WHERE e.cursor = ? ORDER BY b.created DESC, b.id DESC]],
    entry.__CURSOR) or { }

  offer(menu, own[1], '', function(typed, name) return create({ entry }, context, typed, name) end)

  if #own > 1 then
    -- The others in a submenu, each with its own items; past OTHERS of them, the rest in a list
    local others = menu:sub(('%d more bookmark%s of this entry'):format(#own - 1, (#own == 2) and '' or 's'))

    for number = 2, math.min(#own, OTHERS + 1) do
      local items = others:sub(cut(own[number].comment or own[number].topic, TITLE))
      items:add('Show', function() turbojedi.message(describe(own[number].id) or 'The bookmark is not there anymore', 'Bookmark') end)
      offer(items, own[number], '')
    end

    if #own > OTHERS + 1 then
      others:add(('All %d of them...'):format(#own - 1), function()
        local names = { }
        for number = 2, #own do names[number - 1] = cut(own[number].comment or own[number].topic, 80) end
        local _, choice = turbojedi.choose('Bookmarks of this entry', names)
        if choice then act(own[choice + 1].id) end
      end)
    end
  end
end

-- Methods for an agent: the bookmarks read, never written

local function methods()
  turbojedi.on('invoke', 'topics', function()
    if not open() then return { error = failure } end
    local list_of = { }
    for index, item in ipairs(topics()) do list_of[index] = { name = item.name, count = item.count, last = item.last or 0 } end
    return { topics = { type = 'aa{sv}', value = list_of },
             none = query('SELECT COUNT(*) AS count FROM bookmarks WHERE topic IS NULL')[1].count }
  end, { description = 'The topics of the bookmarks of the user, with how many bookmarks each has and the time of the last one; '
           .. 'none tells how many have no topic', effect = 'none', arguments = { } })

  turbojedi.on('invoke', 'bookmarks', function(values)
    if not open() then return { error = failure } end
    local rows, total = pick({ topic = values.topic, text = values.text, since = values.since, till = values['until'] },
                               math.min(values.limit or 50, LIST), values.offset or 0)
    local list_of = { }
    for index, row in ipairs(rows) do
      list_of[index] = { id = row.id, created = row.created, topic = row.topic_name or '', comment = row.comment or '',
                         host = row.host or '', source = row.source, count = row.count, left_out = row.left_out, message = row.message or '' }
    end
    return { bookmarks = { type = 'aa{sv}', value = list_of }, total = total }
  end, { description = 'The bookmarks of the user, the newest first: id, created (microseconds), topic, comment, host, source of '
           .. 'the window it has been made in, count of entries, left_out (entries of the set not kept) and the first message. '
           .. 'The user keeps here what earlier investigations have found: look here whether something has happened before '
           .. 'and what has been found then. total tells how many there are',
         effect = 'none', arguments = {
           topic  = { type = 's', description = 'only the bookmarks of this topic' },
           text   = { type = 's', description = 'only the ones whose topic, comment or entries have these words' },
           since  = { type = 't', description = 'made at this time or later, microseconds since the epoch' },
           ['until'] = { type = 't', description = 'made at this time or before, microseconds since the epoch' },
           limit  = { type = 'u', description = 'bookmarks given, 50 by default, 500 at most' },
           offset = { type = 'u', description = 'bookmarks skipped first' } } })

  turbojedi.on('invoke', 'bookmark', function(values)
    if not open() then return { error = failure } end
    local row = query('SELECT b.*, t.name AS topic_name FROM bookmarks b LEFT JOIN topics t ON t.id = b.topic WHERE b.id = ?', values.id)[1]
    if not row then return { error = 'There is no bookmark ' .. tostring(values.id) } end
    local total   = query('SELECT COUNT(*) AS count FROM entries WHERE bookmark = ?', values.id)[1].count
    local entries = { }
    for index, entry in ipairs(query(('SELECT cursor, time, message, fields FROM entries WHERE bookmark = ? ORDER BY time, id LIMIT %d OFFSET %d'):format(
                                       math.min(values.limit or 20, 100), values.offset or 0), values.id)) do
      entries[index] = { cursor = entry.cursor, time = entry.time, message = entry.message or '', fields = entry.fields }
    end
    return { id = row.id, created = row.created, changed = row.changed, topic = row.topic_name or '', comment = row.comment or '',
             source = row.source, directory = row.directory or '', host = row.host or '', left_out = row.left_out, total = total,
             entries = { type = 'aa{sv}', value = entries } }
  end, { description = 'A bookmark whole: its comment and topic, and its entries, the oldest first, each with its cursor and time to '
           .. 'try in a window with get_entry or go_to_entry, its message, and fields, every field of the entry as JSON; a field '
           .. 'cut past 64 KiB is { "text" or "base64", "cut": its bytes }, one not of UTF-8 { "base64" }. total tells how many '
           .. 'entries it has, limit and offset take a part of them',
         effect = 'none', arguments = {
           id     = { type = 't', description = 'the id of the bookmark, from bookmarks', required = true },
           limit  = { type = 'u', description = 'entries given, 20 by default, 100 at most' },
           offset = { type = 'u', description = 'entries skipped first' } } })
end

-- What the plugin offers: Actions, the Plugins menu, the watches of Show a layout has kept, the methods for an agent

turbojedi.on('actions', actions)
turbojedi.on('menu', { 'Bookmarks', 'All...' }, all)
turbojedi.on('menu', { 'Bookmarks', 'Without a topic...' }, untopical)
turbojedi.on('menu', { 'Bookmarks', 'Of a topic...' }, ofTopic)
turbojedi.on('menu', { 'Topics', 'New topic...' }, newTopic)
turbojedi.on('menu', { 'Topics', 'Rename...' }, renameTopic)
turbojedi.on('menu', { 'Topics', 'Remove...' }, removeTopic)
turbojedi.on('menu', 'Find in bookmarks...', find)
turbojedi.on('restore', function(context) if open() then show(context) end end)

if turbojedi.left then methods() end

turbojedi.log('bookmarks in ' .. path)
