-- TurboJedi, Turbo Journald Interface
-- https://github.com/cyanide-burnout/TurboJedi
-- Artem Prilutskiy, 2026

-- The Actions menu: the bookmark of the entry under the cursor, its comment, its topic and its removal; and a bookmark
-- of the selection when there is one

local plugin = (...):match('^[^.]+')
local base   = require(plugin .. '.base')
local store  = require(plugin .. '.store')
local lists  = require(plugin .. '.lists')

local query = base.query
local cut, choose, reported = lists.cut, lists.choose, lists.reported

local TITLE  = 60   -- Characters of a comment in the title of an item of a menu
local OTHERS = 10   -- Other bookmarks of an entry in its submenu of Actions, the rest in a list

local M = { }

local function set(entries, context, beyond)
  local name = choose()
  if name == false then return end
  local typed = turbojedi.input('Comment', '')
  if not typed then return end
  typed = typed:match('^%s*(.-)%s*$')
  if (typed == '') and (name == '') then return end
  reported(store.create(entries, context, typed, name, beyond))
end

-- The items of a bookmark in Actions, of the entry under the cursor or of the selection: its comment, its topic and its
-- removal; without one, a comment or a topic makes it
local function offer(menu, mine, whose, make)
  if mine then
    menu:add((mine.comment and ('Comment%s: %s'):format(whose, cut(mine.comment, TITLE))) or ('Comment' .. whose), function()
      local typed = turbojedi.input('Comment', mine.comment or '')
      if typed then reported(store.comment(mine.id, typed:match('^%s*(.-)%s*$'))) end
    end)
    menu:add((mine.topic and ('Topic%s: %s'):format(whose, cut(mine.topic, TITLE))) or ('Set topic' .. whose), function()
      local name = choose(mine.topic)
      if name then reported(store.retopic(mine.id, name)) end
    end)
    menu:add('Remove bookmark' .. whose, function()
      if turbojedi.confirm(('Remove the bookmark%s?\n\n%s'):format(whose, cut(mine.comment or mine.topic, 200))) then reported(store.remove(mine.id)) end
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

function M.actions(entry, menu, context)
  if not base.open() then return end

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
      offer(menu, store.bookmark(same), whose)
    else
      menu:add(('Bookmark %d selected entr%s'):format(count, (count == 1) and 'y' or 'ies'), set, context.marked, context, math.max(0, count - #context.marked))
    end

    if (kept > 0) and not same then
      menu:add(('Remove %d selected entr%s from bookmarks'):format(kept, (kept == 1) and 'y' or 'ies'), function()
        if turbojedi.confirm(('Take %d entr%s out of their bookmarks? A bookmark left without entries goes.'):format(kept, (kept == 1) and 'y' or 'ies')) then
          reported(store.unbookmark(cursors))
        end
      end)
    end

    return
  end

  -- The entry under the cursor: the newest bookmark it is in, a new one of it otherwise
  local own = (type(entry.__CURSOR) == 'string') and query([[SELECT b.id, b.comment, t.name AS topic FROM entries e
    JOIN bookmarks b ON b.id = e.bookmark LEFT JOIN topics t ON t.id = b.topic WHERE e.cursor = ? ORDER BY b.created DESC, b.id DESC]],
    entry.__CURSOR) or { }

  offer(menu, own[1], '', function(typed, name) return store.create({ entry }, context, typed, name) end)

  if #own > 1 then
    -- The others in a submenu, each with its own items; past OTHERS of them, the rest in a list
    local others = menu:sub(('%d more bookmark%s of this entry'):format(#own - 1, (#own == 2) and '' or 's'))

    for number = 2, math.min(#own, OTHERS + 1) do
      local items = others:sub(cut(own[number].comment or own[number].topic, TITLE))
      items:add('Show', function() turbojedi.message(store.describe(own[number].id) or 'The bookmark is not there anymore', 'Bookmark') end)
      offer(items, own[number], '')
    end

    if #own > OTHERS + 1 then
      others:add(('All %d of them...'):format(#own - 1), function()
        local names = { }
        for number = 2, #own do names[number - 1] = cut(own[number].comment or own[number].topic, 80) end
        local _, choice = turbojedi.choose('Bookmarks of this entry', names)
        if choice then lists.act(own[choice + 1].id) end
      end)
    end
  end
end

return M
