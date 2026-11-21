-- TurboJedi, Turbo Journald Interface
-- https://github.com/cyanide-burnout/TurboJedi
-- Artem Prilutskiy, 2026

-- The base of SQLite of the user the bookmarks are kept in: opened once it is needed, its queries, its transactions. The
-- schema is in BOOKMARKS.md, for anything else that reads it

local sqlite = require('lsqlite3')

local M = { }

M.NAME = 'bookmarks.sqlite'

local VERSION  = 1             -- Of the schema, PRAGMA user_version
local NOFOLLOW = 0x01000000    -- SQLITE_OPEN_NOFOLLOW: a link in the place of the base is refused
local ENOENT   = 2             -- errno of a file that is not there, whatever language the system tells it in

-- Among the data of the user, nil when there is no directory of them
M.path = turbojedi.where(M.NAME, { data = true })

local path = M.path
local base       -- Open once it is needed, nil when it cannot be
local failure    -- Why it cannot be

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
function M.query(text, ...)
  local rows      = { }
  local statement = prepare(text, ...)

  for row in statement:nrows() do
    rows[#rows + 1] = row
  end

  statement:finalize()
  return rows
end

function M.run(text, ...)
  local statement = prepare(text, ...)
  local result    = statement:step()

  statement:finalize()

  if (result ~= sqlite.DONE) and (result ~= sqlite.ROW) then
    error(base:errmsg(), 0)
  end
end

-- The id of the row inserted last
function M.last()
  return base:last_insert_rowid()
end

-- A change is one transaction: two instances of one user wait for each other, neither loses its change
function M.change(func, ...)
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

-- Lua tells the time by seconds, SQLite by milliseconds: bookmarks of one second keep their order; the base is open
function M.microseconds()
  return M.query("SELECT CAST(ROUND((julianday('now') - 2440587.5) * 86400000.0) AS INTEGER) * 1000 AS now")[1].now
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
-- journal of rollback and not WAL, so that no file of root stays beside it under sudo. The base, or nil and why not
function M.open()
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
    local made, why = turbojedi.settings(M.NAME, '', { private = true, data = true })
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

  local version = M.query('PRAGMA user_version')[1]

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

return M
