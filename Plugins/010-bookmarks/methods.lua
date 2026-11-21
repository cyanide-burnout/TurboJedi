-- TurboJedi, Turbo Journald Interface
-- https://github.com/cyanide-burnout/TurboJedi
-- Artem Prilutskiy, 2026

-- Methods for an agent: the bookmarks read, never written

local plugin = (...):match('^[^.]+')
local base   = require(plugin .. '.base')
local store  = require(plugin .. '.store')

local query = base.query

local M = { }

function M.register()
  turbojedi.on('invoke', 'topics', function()
    local open, failure = base.open()
    if not open then return { error = failure } end
    local list = { }
    for index, item in ipairs(store.topics()) do list[index] = { name = item.name, count = item.count, last = item.last or 0 } end
    return { topics = { type = 'aa{sv}', value = list },
             none = query('SELECT COUNT(*) AS count FROM bookmarks WHERE topic IS NULL')[1].count }
  end, { description = 'The topics of the bookmarks of the user, with how many bookmarks each has and the time of the last one; '
           .. 'none tells how many have no topic', effect = 'none', arguments = { } })

  turbojedi.on('invoke', 'bookmarks', function(values)
    local open, failure = base.open()
    if not open then return { error = failure } end
    local rows, total = store.pick({ topic = values.topic, text = values.text, since = values.since, till = values['until'] },
                                   math.min(values.limit or 50, store.LIST), values.offset or 0)
    local list = { }
    for index, row in ipairs(rows) do
      list[index] = { id = row.id, created = row.created, topic = row.topic_name or '', comment = row.comment or '',
                      host = row.host or '', source = row.source, count = row.count, left_out = row.left_out, message = row.message or '' }
    end
    return { bookmarks = { type = 'aa{sv}', value = list }, total = total }
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
    local open, failure = base.open()
    if not open then return { error = failure } end
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

return M
