-- TurboJedi, Turbo Journald Interface
-- https://github.com/cyanide-burnout/TurboJedi
-- Artem Prilutskiy, 2026

-- Bookmarks: an entry, or the selected ones, kept with a comment and a topic in a base of SQLite of the user, a copy of
-- every entry along, so that they are there after the journal has rotated. The Actions menu tells and changes the
-- bookmark of the entry under the cursor, the Plugins menu looks them up, marks them in the window, shows them in a
-- watch and keeps the topics; an agent reads them. It only keeps what the user says, nothing on the system changes.
--
--   base.lua      the base of SQLite, its queries and transactions
--   copies.lua    the copies of the entries
--   store.lua     bookmarks and topics in the base
--   lists.lua     the lists of bookmarks, Show, Mark, and the dialogs the menus share
--   menu.lua      the Plugins menu
--   actions.lua   the Actions menu
--   methods.lua   the methods for an agent

local plugin = ...
local loaded, sqlite = pcall(require, 'lsqlite3')

if not loaded then
  turbojedi.log('lsqlite3 is not there, nothing to offer: ' .. tostring(sqlite), { problem = true })
  return
end

local base = require(plugin .. '.base')

if not base.path then
  turbojedi.log('there is no directory of data, HOME is not set: nothing to offer')
  return
end

local lists   = require(plugin .. '.lists')
local menu    = require(plugin .. '.menu')
local actions = require(plugin .. '.actions')
local methods = require(plugin .. '.methods')

-- What the plugin offers: Actions, the Plugins menu, the watches of Show a layout has kept, the methods for an agent

turbojedi.on('actions', actions.actions)
turbojedi.on('menu', { 'Bookmarks', 'All...' }, menu.all)
turbojedi.on('menu', { 'Bookmarks', 'Without a topic...' }, menu.untopical)
turbojedi.on('menu', { 'Bookmarks', 'Of a topic...' }, menu.ofTopic)
turbojedi.on('menu', { 'Topics', 'New topic...' }, menu.newTopic)
turbojedi.on('menu', { 'Topics', 'Rename...' }, menu.renameTopic)
turbojedi.on('menu', { 'Topics', 'Remove...' }, menu.removeTopic)
turbojedi.on('menu', 'Find in bookmarks...', menu.find)
turbojedi.on('restore', function(context) if base.open() then lists.show(context) end end)

if turbojedi.left then methods.register() end

turbojedi.log('bookmarks in ' .. base.path)
