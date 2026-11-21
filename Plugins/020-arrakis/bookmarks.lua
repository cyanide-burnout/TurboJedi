-- TurboJedi, Turbo Journald Interface
-- https://github.com/cyanide-burnout/TurboJedi
-- Artem Prilutskiy, 2026

local plugin   = (...):match('^[^.]+')
local settings = require(plugin .. '.settings')
local request  = require(plugin .. '.request')
local job      = require(plugin .. '.job')

local clip, line, analyze = request.clip, request.line, job.analyze

-- What the model is asked of them, prompts/bookmarks.txt
local QUESTION = assert(turbojedi.resource('prompts/bookmarks.txt')):gsub('%s+$', '')

-- An item of a list of bookmarks, which share the globals of Lua with Arrakis: the bookmarks of the list, what each
-- has found, the model asked what they have in common, whether it comes back and when. A comment is what an
-- investigation has found, its entries what it rests on: when the budget has no room for all, the detail goes first,
-- never the time the list spans. All bookmarks with their first entries, then with their first entry, then their
-- comments alone, then the comments of a sample evenly over their time, the oldest and the newest among them, with how
-- many there are by day, week or month for all of them. The model is told which, not to take a sample for the whole
local SHARE   = 3     -- Entries of a bookmark told at most, its first ones
local SAID    = 300   -- Characters of a message of them
local COMMENT = 1000  -- Characters of a comment
local SPARE   = 40    -- Characters of the numbers of the entries after a comment, of the line of an entry
local HEAD    = 600   -- Characters of the lines that open the request and tell what it is made of
local LIGHT   = 16384 -- Bytes of a copy of an entry decoded whole, a larger one gives its message alone

-- How many bookmarks there are by day, by week when they span more than two months, by month past two years, by year
-- past that; coarser while the line is longer than most
local UNITS = { { '%Y-%m-%d', 'day' }, { '%G-W%V', 'week' }, { '%Y-%m', 'month' }, { '%Y', 'year' } }

local function spread(bookmarks, most)
  local span = (bookmarks[#bookmarks].created - bookmarks[1].created) // 1000000
  local unit = (span <= 62 * 86400) and 1 or (span <= 2 * 366 * 86400) and 2 or 3
  local line

  repeat
    local format, counts, order = UNITS[unit][1], { }, { }

    for _, bookmark in ipairs(bookmarks) do
      local key = turbojedi.date(format, bookmark.created // 1000000)
      if not counts[key] then order[#order + 1] = key end
      counts[key] = (counts[key] or 0) + 1
    end

    for index, key in ipairs(order) do order[index] = key .. ' ' .. counts[key] end
    line = ('How many bookmarks of the list there are by %s, all %d of them: %s.'):format(UNITS[unit][2], #bookmarks, table.concat(order, ', '))
    unit = unit + 1
  until (#line <= most) or (unit > #UNITS)

  return (#line <= most) and line or clip(line, most)
end

-- The indices of a sample of count of the bookmarks, the oldest first: the oldest and the newest, then the ones nearest
-- to points evenly over their time, so that a burst does not take the sample; the points a burst leaves on one
-- bookmark are made up evenly by their order
local function sample(bookmarks, count)
  local total, picked, seen = #bookmarks, { }, { }
  local first, last, cursor = bookmarks[1].created, bookmarks[#bookmarks].created, 1

  local function take(index)
    if not seen[index] then seen[index], picked[#picked + 1] = true, index end
  end

  take(1)
  take(total)

  for step = 1, count - 2 do
    local target = first + (last - first) * step // (count - 1)
    while (cursor < total) and (bookmarks[cursor + 1].created <= target) do cursor = cursor + 1 end
    local index = cursor
    if (cursor < total) and (bookmarks[cursor + 1].created - target < target - bookmarks[cursor].created) then index = cursor + 1 end
    take(index)
  end

  for step = 1, count - 2 do
    if #picked >= count then break end
    take(1 + ((step * (total - 1) + (count - 1) // 2) // (count - 1)))
  end

  table.sort(picked)
  return picked
end

Bookmarks       = Bookmarks       or { }
Bookmarks.items = Bookmarks.items or { }
table.insert(Bookmarks.items, { title = 'Ask Arrakis about them', action = function(bookmarks, what)
  local engine = settings.chosen()
  if (not engine) or (#bookmarks == 0) then return end

  -- The lines that open the request and tell what it is made of take their room first
  local budget, total = engine.budget - HEAD, #bookmarks
  local notes, comments = { }, 0

  local function note(bookmark, most)
    return ('- made %s%s: %s'):format(turbojedi.date('%Y-%m-%d %H:%M:%S', bookmark.created // 1000000),
             bookmark.topic and (', topic ' .. bookmark.topic) or '', clip(bookmark.comment or '(no comment)', most))
  end

  for index, bookmark in ipairs(bookmarks) do
    notes[index] = note(bookmark, COMMENT)
    comments = comments + #notes[index] + SPARE
  end

  -- The entries when the comments of all leave room for them: the first ones, then the first one alone
  local entries, share, cost = { }, 0, comments

  if comments <= budget then
    for index, bookmark in ipairs(bookmarks) do
      entries[index] = bookmark.entries(SHARE, LIGHT)
      for _, entry in ipairs(entries[index]) do entry.MESSAGE = clip(tostring(entry.MESSAGE or ''), SAID) end
    end

    for _, most in ipairs { SHARE, 1 } do
      cost = comments
      for index in ipairs(bookmarks) do
        for number = 1, math.min(most, #entries[index]) do cost = cost + #line(entries[index][number]) + SPARE end
      end
      if cost <= budget then share = most break end
    end
  end

  -- The comments of a sample when even they leave no room: evenly over the list, the oldest and the newest in it
  local taken, stats = { }, nil

  if comments > budget then
    -- The room of the statistics and of the oldest and the newest comes first, the comments of these two shortened
    -- when they have no room whole: the time the list spans is never lost
    stats = spread(bookmarks, budget // 3)
    local room  = budget - #stats - 1
    local most  = COMMENT

    while (#notes[1] + #notes[total] + 2 * SPARE > room) and (most > 20) do
      most = most * 3 // 4
      notes[1], notes[total] = note(bookmarks[1], most), note(bookmarks[total], most)
    end

    local count = total

    while count > 2 do
      local size = 0
      taken = sample(bookmarks, count)
      for _, index in ipairs(taken) do size = size + #notes[index] + SPARE end
      if size <= room then break end
      count = math.max(2, math.min(count - 1, count * room // size))
    end

    taken = sample(bookmarks, math.max(2, count))
  else
    for index = 1, total do taken[index] = index end
  end

  local told = (comments > budget) and
                 ('A sample of %d of the %d bookmarks of the list, evenly over their time, the oldest and the newest among them, their comments alone: '
                  .. 'the budget of the request has no room for more. Do not take it for the whole history: how many there are '
                  .. 'over time is told below for all of them.'):format(#taken, total)
            or (share == SHARE) and ('All %d bookmarks of the list, each with its first entries, up to %d.'):format(total, SHARE)
            or (share == 1) and ('All %d bookmarks of the list, each with its first entry alone: the budget of the request has no room for more of them.'):format(total)
            or ('All %d bookmarks of the list, their comments alone, none of their entries: the budget of the request has no room for them.'):format(total)

  local items, lines = { }, { 'These are bookmarks the user has kept over time, each with the comment that tells what an investigation has found. ' .. told }

  for _, index in ipairs(taken) do
    local bookmark, from = bookmarks[index], #items + 1
    for number = 1, math.min(share, entries[index] and #entries[index] or 0) do items[#items + 1] = entries[index][number] end
    local rest = bookmark.count - (#items - from + 1) + bookmark.left_out
    lines[#lines + 1] = (share > 0) and ('%s; entries %s%s'):format(notes[index],
                          (#items >= from) and (('[%d]'):format(from) .. ((#items > from) and ('-[%d]'):format(#items) or '')) or 'none',
                          (rest > 0) and (', %d more not told'):format(rest) or '')
                        or ('%s (%d entries kept)'):format(notes[index], bookmark.count + bookmark.left_out)
  end

  if stats then lines[#lines + 1] = stats end

  analyze(items, what, nil, { notes = table.concat(lines, '\n'), question = QUESTION })
end })

