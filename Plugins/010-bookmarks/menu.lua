-- TurboJedi, Turbo Journald Interface
-- https://github.com/cyanide-burnout/TurboJedi
-- Artem Prilutskiy, 2026

-- The Plugins menu: Bookmarks and Topics, submenus of what is done, then the list of the bookmarks or the topics, and
-- Find in bookmarks

local plugin = (...):match('^[^.]+')
local base   = require(plugin .. '.base')
local store  = require(plugin .. '.store')
local lists  = require(plugin .. '.lists')

local open, query, run, change = base.open, base.query, base.run, base.change
local list, topical, reported = lists.list, lists.topical, lists.reported

local M = { }

local function unavailable()
  local _, reason = open()
  turbojedi.message('The bookmarks cannot be used: ' .. tostring(reason), 'Bookmarks')
end

function M.find()
  if not open() then return unavailable() end
  local text = turbojedi.input('Find in bookmarks', '')
  text = text and text:match('^%s*(.-)%s*$')
  if text and (text ~= '') then list({ text = text }) end
end

function M.all()
  if open() then list({ }) else unavailable() end
end

function M.untopical()
  if open() then list({ none = true }) else unavailable() end
end

function M.ofTopic()
  if not open() then return unavailable() end
  local name = topical('Bookmarks of the topic')
  if name then list({ topic = name }) end
end

function M.newTopic()
  if not open() then return unavailable() end
  local name = turbojedi.input('New topic', '')
  name = name and name:match('^%s*(.-)%s*$')
  if name and (name ~= '') then reported(change(store.topic, name)) end
end

function M.renameTopic()
  if not open() then return unavailable() end
  local name = topical('Rename the topic')
  if not name then return end
  local new = turbojedi.input('Rename ' .. name .. ' to', name)
  new = new and new:match('^%s*(.-)%s*$')
  if (not new) or (new == '') or (new == name) then return end
  reported(change(function()
    run('UPDATE topics SET name = ? WHERE name = ?', new, name)
    for _, row in ipairs(query('SELECT b.id FROM bookmarks b JOIN topics t ON t.id = b.topic WHERE t.name = ?', new)) do store.reindex(row.id) end
  end))
end

function M.removeTopic()
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
      for _, row in ipairs(ids) do store.reindex(row.id) store.settle(row.id) end
    end
  end))
end

return M
