-- TurboJedi, Turbo Journald Interface
-- https://github.com/cyanide-burnout/TurboJedi
-- Artem Prilutskiy, 2026

-- A request to the service, of the format of OpenAI or of Anthropic, and the text of its answer. Tools that teach a
-- model of one's own make the requests the way this module does: the instructions are prompts/instructions.txt, the
-- entries the lines of listing()

local plugin   = (...):match('^[^.]+')
local cjson    = require('cjson')
local settings = require(plugin .. '.settings')

local M = { }

M.TOKENS = 2048    -- Of an answer, Anthropic wants it said: a few sentences of Cyrillic take a thousand

local LEVELS = { [0] = 'EMERG', 'ALERT', 'CRIT', 'ERR', 'WARNING', 'NOTICE', 'INFO', 'DEBUG' }

-- The instructions of the package; prompt of the settings takes their place
local INSTRUCTIONS = assert(turbojedi.resource('prompts/instructions.txt')):gsub('%s+$', '')

-- A field the journal has several times is a list, the first value tells enough here
function M.first(value)
  if type(value) == 'table' then return value[1] end
  return value
end

local first = M.first

-- One line of an entry for the model: time in the zone of the user, host, unit, process, level, message
function M.line(item)
  local stamp    = item.__REALTIME_TIMESTAMP
  local name     = first(item.SYSLOG_IDENTIFIER) or first(item._COMM) or '?'
  local pid      = first(item._PID)
  local unit     = first(item._SYSTEMD_UNIT)
  local priority = tonumber(first(item.PRIORITY) or '')

  return ('%s %s%s %s%s %s: %s'):format(
    stamp and turbojedi.date('%Y-%m-%d %H:%M:%S', stamp // 1000000) .. ('.%03d'):format((stamp // 1000) % 1000) or '?',
    first(item._HOSTNAME) or '?', unit and (' ' .. unit) or '', name, pid and ('[' .. pid .. ']') or '',
    LEVELS[priority] or 'INFO', ((first(item.MESSAGE) or ''):gsub('%s+$', '')))
end

-- Numbered lines up to the budget, the newest kept when there are too many: what has failed last tells most
local function listing(engine, items)
  local lines = { }
  local size  = 0
  local start = #items

  while start > 0 do
    local text = ('[%d] %s'):format(start, M.line(items[start]))
    if (size + #text > engine.budget) and (#lines > 0) then break end
    table.insert(lines, 1, text)
    size  = size + #text + 1
    start = start - 1
  end

  if start > 0 then
    table.insert(lines, 1, ('(%d older entries left out)'):format(start))
  end

  return table.concat(lines, '\n')
end

-- The request of the format of the service: the instructions and the messages so far, the last one of the user
function M.send(engine, system, messages)
  local headers = { ['Content-Type'] = 'application/json' }
  local body

  if engine.format == 'anthropic' then
    headers['anthropic-version'] = '2023-06-01'
    if engine.key then headers['x-api-key'] = engine.key end
    body = { model = engine.model, max_tokens = engine.tokens or M.TOKENS, stream = false, system = system,
             messages = messages }
  else
    if engine.key then headers.Authorization = 'Bearer ' .. engine.key end
    -- The whole answer at once: some servers stream when not told, fm serve of macOS for one
    body = { model = engine.model, max_tokens = engine.tokens, stream = false,
             messages = { { role = 'system', content = system }, table.unpack(messages) } }
  end

  return { url = engine.url, method = 'POST', headers = headers, body = cjson.encode(body), timeout = engine.timeout }
end

-- A text cut to most characters of UTF-8, never in the middle of one, … told where it is cut
function M.clip(text, most)
  local cut = utf8.offset(text, most + 1)
  return (cut and (cut <= #text)) and (text:sub(1, cut - 1) .. '…') or text
end

-- The first request of a job, the instructions and the entries it has been made of; focus is the number of the entry
-- chosen when the ones before it are only what has led to it. Another plugin may tell more: notes before the entries,
-- within the budget as well, and the question after them
function M.request(engine, items, focus, extra)
  local tongue = settings.language(engine)
  local system = (engine.instructions or INSTRUCTIONS):gsub('{language}', function() return tongue end)
  local user   = listing(engine, items)
  local notes  = extra and extra.notes

  if notes and (notes ~= '') then
    if #notes > engine.budget then notes = M.clip(notes, engine.budget) .. '\n(the rest left out)' end
    user = notes .. '\n\n' .. user
  end

  if focus then
    user = user .. ('\n\nExplain entry [%d]. The entries before it are of the same unit or program, only what has led to it.'):format(focus)
  end

  if extra and extra.question then
    user = user .. '\n\n' .. extra.question
  end

  user = user .. '\n\nAnswer in ' .. tongue .. '.'
  return M.send(engine, system, { { role = 'user', content = user } }), system, user
end

-- An answer with its request as a line of JSON of the file of record, for teaching a model of one's own: the entries are
-- in it, so the core makes the file 0600 and writes only to one of the process alone, never through a link
function M.keep(job, text)
  local engine = job.settings
  local line = cjson.encode({ time = os.time(), url = engine.url, model = engine.model, format = engine.format,
                              language = engine.language, system = job.system, user = job.user, answer = text }) .. '\n'
  local done, reason = turbojedi.append(engine.record, line)

  if not done then
    turbojedi.log(('record %s cannot be written: %s'):format(engine.record, tostring(reason)), { problem = true })
  end
end

-- The text of an answer, or nil and why there is none
function M.answer(engine, code, body)
  local decoded, data = pcall(cjson.decode, body)

  if not decoded or (type(data) ~= 'table') then
    return nil, ('%d, not JSON: %s'):format(code, body:sub(1, 200))
  end

  if (code < 200) or (code > 299) then
    local failure = type(data.error) == 'table' and data.error.message or data.error or data.message
    return nil, ('%d: %s'):format(code, type(failure) == 'string' and failure or body:sub(1, 200))
  end

  local text
  local cut
  local reason

  -- The limit of tokens reached: the text stops where it has been, of a model that thinks first maybe before any
  if engine.format == 'anthropic' then
    local parts = { }
    for _, part in ipairs(type(data.content) == 'table' and data.content or { }) do
      if (type(part) == 'table') and (part.type == 'text') then parts[#parts + 1] = part.text end
    end
    text = table.concat(parts, '\n')
    reason = data.stop_reason
    cut    = reason == 'max_tokens'
  else
    local choice = type(data.choices) == 'table' and data.choices[1]
    text = type(choice) == 'table' and type(choice.message) == 'table' and choice.message.content
    reason = type(choice) == 'table' and choice.finish_reason or nil
    cut    = reason == 'length'
    if cut and (text == cjson.null) then text = '' end
  end

  if type(text) ~= 'string' then
    return nil, 'no text in the answer: ' .. body:sub(1, 200)
  end

  -- A reasoning model thinks aloud first, the answer is after it; cut while thinking, there is none
  text = text:gsub('<think>.-</think>', ''):gsub('<think>.*$', ''):gsub('^%s+', ''):gsub('%s+$', '')

  if cut and (text == '') then
    return nil, 'nothing but thinking before the limit of tokens: raise tokens of arrakis.conf'
  elseif text == '' then
    -- Why the model has stopped tells more than the empty text: a refusal, a filter of the service
    return nil, ('an empty text, stopped by %s'):format((reason ~= cjson.null) and tostring(reason) or 'nil')
  end

  return text, nil, cut
end

return M
