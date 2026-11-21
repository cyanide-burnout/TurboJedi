-- TurboJedi, Turbo Journald Interface
-- https://github.com/cyanide-burnout/TurboJedi
-- Artem Prilutskiy, 2026

-- What the user sees of the bookmarks: a list of them with its items and the ones of other plugins, a bookmark chosen
-- from it, the watch of Show, the entries marked in the window, and the dialogs the menus share

local plugin = (...):match('^[^.]+')
local cjson  = require('cjson')
local base   = require(plugin .. '.base')
local copies = require(plugin .. '.copies')
local store  = require(plugin .. '.store')

local query = base.query
local told  = copies.told

local date = turbojedi.date or os.date

local M = { }

local recent         -- The topic chosen last, offered first
local showing = { }  -- Watches of Show by their keys, to fill anew when the bookmarks change

-- Titles of items cut to a width by whole characters
function M.cut(text, width)
  text = tostring(text or ''):gsub('%s+', ' ')

  if utf8.len(text) and (utf8.len(text) > width) then
    return text:sub(1, utf8.offset(text, width) - 1) .. '…'
  end

  return text
end

local cut = M.cut

-- A topic chosen: one there is, the last one first, a new one typed, or none. false when cancelled, '' for none
function M.choose(current)
  local list    = store.topics()
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

-- A topic chosen from the list of the topics, with how many bookmarks each has and the time of the last: its name
function M.topical(title)
  local list  = store.topics()
  local names = { }

  if #list == 0 then
    return turbojedi.message('There are no topics yet', 'Bookmarks')
  end

  for index, item in ipairs(list) do
    names[index] = ('%s (%d%s)'):format(item.name, item.count, item.last and (', last ' .. date('%F %T', item.last // 1000000)) or '')
  end

  local _, choice = turbojedi.choose(title, names)
  return choice and list[choice].name
end

function M.label(scope)
  if scope.topic then return scope.topic end
  if scope.none then return 'without a topic' end
  if scope.text then return 'of "' .. scope.text .. '"' end
  return 'all'
end

local label = M.label

-- A watch of Show: a line a bookmark, the newest on top, Enter shows the bookmark from the base
local function fill(watch, scope)
  local rows, total = store.pick(scope, store.LIST)

  for number = #rows, 1, -1 do
    local row = rows[number]
    watch:add(('%s  %s%s'):format(date('%F %T', row.created // 1000000), row.topic_name and ('[' .. row.topic_name .. '] ') or '',
      cut(row.comment or row.message, 120)), nil, { quiet = true, action = function()
        local text = store.describe(row.id)
        turbojedi.message(text or 'The bookmark is not there anymore', 'Bookmark')
      end })
  end

  watch:status(('%d bookmark%s%s'):format(total, (total == 1) and '' or 's', (total > #rows) and (', the newest ' .. #rows .. ' shown') or ''))
end

function M.show(scope)
  local key = 'bookmarks:' .. (scope.topic and ('topic:' .. scope.topic) or scope.none and 'none' or scope.text and ('text:' .. scope.text) or 'all')

  if showing[key] then
    return turbojedi.message('They are shown already: F12, Bookmarks ' .. label(scope), 'Bookmarks')
  end

  local watch = turbojedi.watch({ title = 'Bookmarks ' .. label(scope), key = key, context = scope, window = 'new',
    removed = function() showing[key] = nil end })

  showing[key] = { watch = watch, scope = scope }
  fill(watch, scope)
end

local function refresh()
  for _, item in pairs(showing) do
    item.watch:clear()
    fill(item.watch, item.scope)
  end
end

-- A change done, or told why not; the watches of Show follow it
function M.reported(done, reason)
  if not done then turbojedi.message('The bookmark is not kept: ' .. tostring(reason), 'Bookmarks') end
  refresh()
  return done
end

local reported = M.reported

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
function M.act(id)
  local text, row = store.describe(id)

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
    if typed then reported(store.comment(id, typed:match('^%s*(.-)%s*$'))) end
  elseif choice == 4 then
    local name = M.choose(row.topic_name)
    if name then reported(store.retopic(id, name)) end
  elseif choice == 5 then
    turbojedi.copy(text .. '\n')
  elseif (choice == 6) and turbojedi.confirm('Remove this bookmark?\n\n' .. cut(row.comment or text, 200)) then
    reported(store.remove(id))
  end
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

function M.list(scope)
  local rows, total = store.pick(scope, store.LIST)

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
    M.show(scope)
  elseif choice == 2 then
    local ids = { }
    for _, row in ipairs(rows) do ids[#ids + 1] = row.id end
    mark(ids)
  elseif choice and (choice <= top) then
    others[choice - 2].action(collect(rows), ('%d bookmarks %s'):format(#rows, label(scope)))
  elseif choice then
    M.act(rows[choice - top].id)
  end
end

return M
