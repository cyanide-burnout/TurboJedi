-- TurboJedi, Turbo Journald Interface
-- https://github.com/cyanide-burnout/TurboJedi
-- Artem Prilutskiy, 2026

-- The Actions menu and the Plugins menu: an entry, the marked ones, the errors of the window explained, and Status

local plugin   = (...):match('^[^.]+')
local settings = require(plugin .. '.settings')
local request  = require(plugin .. '.request')
local job      = require(plugin .. '.job')

local first, analyze = request.first, job.analyze

local CONTEXT = 30    -- Entries before a single one, of its unit or identifier, to tell the model what has led to it
local ERRORS  = 200   -- Errors of the window read at most, the budget takes the newest of them

local M = { }

-- A single entry is told with what has come before it from the same unit or program
local function single(item)
  local items   = { item }
  local unit    = first(item._SYSTEMD_UNIT)
  local name    = first(item.SYSLOG_IDENTIFIER)
  local matches = unit and { _SYSTEMD_UNIT = unit } or (name and { SYSLOG_IDENTIFIER = name })

  if matches and item.__CURSOR then
    local before = turbojedi.query({ matches = matches, before = item.__CURSOR, limit = CONTEXT })

    for _, earlier in ipairs(before or { }) do
      -- From the newest back: each goes in front
      table.insert(items, 1, earlier)
    end
  end

  -- The model is told which one is asked about, or it explains them all
  if #items > 1 then
    analyze(items, ('an entry and %d before it'):format(#items - 1), #items)
  else
    analyze(items, 'an entry')
  end
end

-- The items of Actions: the marked entries, or the entry under the cursor with what has come before it
function M.actions(item, menu, context)
  if context.marked then
    menu:add(('Explain %d marked entries'):format(#context.marked), analyze, context.marked)
  else
    menu:add('Explain this entry', single, item)
  end
end

-- The errors among what the window shows, its filter and its time, without marking them: the newest go when there are
-- too many
function M.errors()
  local found, stopped = turbojedi.query({ window = true, priority = 'err', limit = ERRORS })
  if #found == 0 then
    return turbojedi.message('No errors in the window' .. ((stopped and stopped ~= 'limit') and (', the reading has stopped by ' .. stopped) or ''), 'Arrakis')
  end

  -- The query gives the newest first, the model reads them as they have come
  local items = { }
  for position = #found, 1, -1 do items[#items + 1] = found[position] end
  analyze(items, ('%d errors of the window'):format(#items))
end

-- What Arrakis has read and from where, and a short request to the service to see that it answers; no entry goes along
function M.status()
  local engine = settings.chosen()
  if not engine then return end

  local host = engine.host
  local card = table.concat({
    ('Files     %s'):format(engine.files),
    ('Engine    %s'):format(engine.name and ('%s of %d, Choose the model of the Plugins menu changes it'):format(engine.name, #settings.engines) or 'the one of the files'),
    ('Service   %s at %s'):format(engine.format, engine.url),
    ('Model     %s'):format(engine.model or 'of the server'),
    ('Key       %s'):format(engine.key and 'set' or 'none'),
    ('Language  %s'):format(settings.language(engine)),
    ('Budget    %d characters of entries'):format(engine.budget),
    ('Timeout   %s'):format((engine.timeout > 0) and (engine.timeout .. ' seconds') or 'none'),
    ('Tokens    %s'):format(engine.tokens or ((engine.format == 'anthropic') and request.TOKENS) or 'of the server'),
    ('Prompt    %s'):format(engine.prompt or 'built in'),
    ('Record    %s'):format(engine.record or 'none'),
    ('Machine   %s'):format((host == 'localhost' or host == '::1' or host:match('^127%.')) and 'this one' or
      (engine.trusted and ('%s, the entries go there'):format(host) or ('%s, asked before the first request'):format(host))),
  }, '\n')

  if not turbojedi.confirm(card .. '\n\nSend a short request to see that the service answers?') then return end

  local started = os.time()
  local code, body, more = turbojedi.fetch(request.send(engine, 'Answer with the single word OK.', { { role = 'user', content = 'Are you there?' } }))
  if (not code) and (body == 'cancel') then return end
  if not code then return turbojedi.message(('No answer from %s: %s, %s'):format(host, tostring(body), tostring(more)), 'Arrakis') end

  local text, failure = request.answer(engine, code, body)
  turbojedi.message(text and ('%s has answered in %d s: %s'):format(host, os.time() - started, text) or ('%s has answered %s'):format(host, failure), 'Arrakis')
end

return M
