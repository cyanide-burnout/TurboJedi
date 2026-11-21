-- TurboJedi, Turbo Journald Interface
-- https://github.com/cyanide-burnout/TurboJedi
-- Artem Prilutskiy, 2026

-- Bookmarks and topics in the base: made, changed, removed, looked up and told as text. Everything here takes the base
-- open, a change is one transaction

local plugin = (...):match('^[^.]+')
local cjson  = require('cjson')
local base   = require(plugin .. '.base')
local copies = require(plugin .. '.copies')

local query, run, change, microseconds = base.query, base.run, base.change, base.microseconds
local keep, copy, told = copies.keep, copies.copy, copies.told

local date = turbojedi.date or os.date

local M = { }

M.LIST = 500   -- Bookmarks a list shows, the newest

-- Topics

function M.topics()
  return query([[SELECT t.id, t.name, COUNT(b.id) AS count, MAX(b.created) AS last FROM topics t
                 LEFT JOIN bookmarks b ON b.topic = t.id GROUP BY t.id ORDER BY MAX(b.created) DESC, MAX(b.id) DESC, t.name]])
end

-- The id of a topic of that name, made when it is not there; nil for none
function M.topic(name)
  if (not name) or (name == '') then
    return nil
  end

  run('INSERT OR IGNORE INTO topics(name, created) VALUES (?, ?)', name, microseconds())
  return query('SELECT id FROM topics WHERE name = ?', name)[1].id
end

-- Bookmarks

-- The text the full-text index has of a bookmark: its topic, its comment, and the messages and the sources of its entries
function M.reindex(id)
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
function M.create(entries, context, comment, name, beyond)
  return change(function()
    local now   = microseconds()
    local kept  = { }
    local bytes = 0
    local left  = beyond or 0

    for number = #entries, 1, -1 do
      local entry = entries[number]
      local json  = copy(entry)

      if (bytes + #json > copies.BOOKMARK) and (#kept > 0) then
        left = left + number
        break
      end

      bytes = bytes + #json
      table.insert(kept, 1, { entry = entry, json = json })
    end

    run([[INSERT INTO bookmarks(created, changed, topic, comment, source, directory, host, left_out)
          VALUES (?, ?, ?, ?, ?, ?, ?, ?)]], now, now, M.topic(name), (comment ~= '') and comment or nil,
        context.source or 'local', context.directory, kept[1] and kept[1].entry._HOSTNAME, left)

    local id = base.last()

    for _, item in ipairs(kept) do
      run('INSERT INTO entries(bookmark, cursor, time, message, fields) VALUES (?, ?, ?, ?, ?)', id,
          tostring(item.entry.__CURSOR), tonumber(item.entry.__REALTIME_TIMESTAMP) or 0,
          told(keep(tostring(item.entry.MESSAGE or ''))), item.json)
    end

    M.reindex(id)
    return id
  end)
end

-- A bookmark is there while it has a comment or a topic
function M.settle(id)
  local row = query('SELECT comment, topic FROM bookmarks WHERE id = ?', id)[1]

  if row and (not row.comment) and (not row.topic) then
    run('DELETE FROM texts WHERE rowid = ?', id)
    run('DELETE FROM bookmarks WHERE id = ?', id)
  end
end

function M.comment(id, text)
  return change(function()
    run('UPDATE bookmarks SET comment = ?, changed = ? WHERE id = ?', (text ~= '') and text or nil, microseconds(), id)
    M.reindex(id)
    M.settle(id)
  end)
end

function M.retopic(id, name)
  return change(function()
    run('UPDATE bookmarks SET topic = ?, changed = ? WHERE id = ?', M.topic(name), microseconds(), id)
    M.reindex(id)
    M.settle(id)
  end)
end

function M.remove(id)
  return change(function()
    run('DELETE FROM texts WHERE rowid = ?', id)
    run('DELETE FROM bookmarks WHERE id = ?', id)
  end)
end

-- A bookmark by its id: its comment and the name of its topic
function M.bookmark(id)
  return query('SELECT b.id, b.comment, t.name AS topic FROM bookmarks b LEFT JOIN topics t ON t.id = b.topic WHERE b.id = ?', id)[1]
end

-- The entries of the cursors taken out of every bookmark they are in; a bookmark left without entries goes
function M.unbookmark(cursors)
  local places = ('?,'):rep(#cursors):sub(1, -2)

  return change(function()
    local ids = query('SELECT DISTINCT bookmark FROM entries WHERE cursor IN (' .. places .. ')', table.unpack(cursors))

    run('DELETE FROM entries WHERE cursor IN (' .. places .. ')', table.unpack(cursors))

    for _, row in ipairs(ids) do
      if query('SELECT COUNT(*) AS count FROM entries WHERE bookmark = ?', row.bookmark)[1].count == 0 then
        run('DELETE FROM texts WHERE rowid = ?', row.bookmark)
        run('DELETE FROM bookmarks WHERE id = ?', row.bookmark)
      else
        M.reindex(row.bookmark)
      end
    end
  end)
end

-- A bookmark as text: its comment, its topic, and the copies of its entries
function M.describe(id)
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

-- What a scope of bookmarks is: all, without a topic, a topic, the ones a text finds; the newest first, LIST by default
function M.pick(scope, limit, offset)
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
                         ORDER BY b.created DESC, b.id DESC LIMIT %d OFFSET %d]]):format(where, limit or M.LIST, offset or 0), table.unpack(values))
  return rows, total
end

return M
