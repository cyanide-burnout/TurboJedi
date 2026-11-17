-- TurboJedi, Turbo Journald Interface
-- https://github.com/cyanide-burnout/TurboJedi
-- Artem Prilutskiy, 2026

-- Arrakis: entries explained by a language model behind an HTTP API of the format of OpenAI (llama-server, Ollama,
-- vLLM, colibri, OpenAI) or of Anthropic. On Arrakis they mine the spice, here the sense of the journal.
--
-- Nothing happens without a file of settings: /etc/turbojedi/arrakis.conf of root for every user, then
-- ~/.config/turbojedi/arrakis.conf of the user over it; run by sudo, the one of the user of sudo over both, settings and
-- not code, which the user could send anywhere from the screen anyway:
--
--   url      = http://127.0.0.1:8080/v1/chat/completions
--   model    = qwen2.5-1.5b-instruct
--   format   = openai
--   key      = ...
--   language = ru
--   budget   = 12000
--   timeout  = 600
--   tokens   = 512
--   prompt   = /etc/turbojedi/arrakis.prompt
--   record   = /var/tmp/arrakis.jsonl
--
-- format is openai or anthropic, which /v1/messages in the url tells as well; key a token for a service that wants one,
-- chmod 600 the file then; language of the answers, the one of the locale without it; budget characters of entries a
-- request takes at most; timeout seconds a request may take, 0 for no limit; tokens of an answer at most, the service
-- decides without it (1024 for Anthropic, which wants it said). prompt is a file of instructions instead of the ones
-- built in, {language} there the language of the answers: a model taught on instructions of its own gets them. record
-- is a file every answer goes to as a line of JSON with its request, for teaching a model of one's own on them: the
-- entries are in it, so it is made 0600 and written only when it is a file of the process alone. Under sudo, prompt and
-- record of the file of the user of sudo are left out: root reads and writes no path a user names. A value goes to the
-- end of its line, a comment is a line of its own.
--
-- An answer comes with a line for a question under it, and Enter on an answer or a question in the lines of its watch
-- brings it back: the question goes with the whole conversation of its job, the entries, the answers and the questions
-- before it; a job starts on its own, nothing of the others goes along. The record keeps the first
-- request of a job and its answer only.
--
-- An address of another machine is told once before the first request goes there: the entries leave the machine then.
-- JSON is lua-cjson for Lua 5.4: lua-cjson of Debian and Ubuntu, lua54-cjson of openSUSE; elsewhere
-- luarocks --lua-version 5.4 install lua-cjson

local SYSTEM  = '/etc/turbojedi/arrakis.conf'
local CONTEXT = 30      -- Entries before a single one, of its unit or identifier, to tell the model what has led to it
local TOKENS  = 1024    -- Of an answer, Anthropic wants it said

local LEVELS = { [0] = 'EMERG', 'ALERT', 'CRIT', 'ERR', 'WARNING', 'NOTICE', 'INFO', 'DEBUG' }

-- A small model follows the name of a language, not its code
local LANGUAGES = { en = 'English', ru = 'Russian', uk = 'Ukrainian', de = 'German', fr = 'French', es = 'Spanish',
                    it = 'Italian', pt = 'Portuguese', pl = 'Polish', nl = 'Dutch', cs = 'Czech', sv = 'Swedish',
                    tr = 'Turkish', ja = 'Japanese', zh = 'Chinese', ko = 'Korean', he = 'Hebrew', ar = 'Arabic' }

-- Settings of a file over the ones there are; false when there is no file
local function read(path, settings)
  local file = io.open(path)
  if not file then return false end

  for line in file:lines() do
    local key, value = line:match('^%s*([%w_]+)%s*=%s*(.-)%s*$')
    if key then settings[key] = value end
  end

  file:close()
  return true
end

local function load()
  local home     = os.getenv('XDG_CONFIG_HOME') or ((os.getenv('HOME') or '') .. '/.config')
  local user     = home .. '/turbojedi/arrakis.conf'
  local settings = { }
  local sudo     = turbojedi.sudo and turbojedi.sudo()
  local paths    = { SYSTEM, user }
  local used     = { }

  if sudo then
    -- The user who has run sudo, not root, has set up the engine
    user = sudo.home .. '/.config/turbojedi/arrakis.conf'
    paths[#paths + 1] = user
  end

  for index, path in ipairs(paths) do
    local values = { }

    if read(path, values) then
      used[#used + 1] = path

      for key, value in pairs(values) do
        if sudo and (index == 3) and ((key == 'prompt') or (key == 'record')) then
          -- Root reads and writes no path a user names: /etc/turbojedi/arrakis.conf or the one of root tell them
          turbojedi.log(('%s of %s is left out under sudo: a path for root goes to %s'):format(key, path, SYSTEM), { problem = true })
        else
          settings[key] = value
        end
      end
    end
  end

  if #used == 0 then
    -- Not set up: nothing is offered, nothing is sent anywhere
    turbojedi.log('no ' .. table.concat(paths, ', ') .. ': nothing to offer, nothing sent anywhere')
    return nil
  end

  settings.files = table.concat(used, ', ')

  settings.format  = settings.format or (settings.url and settings.url:match('/v1/messages$') and 'anthropic') or 'openai'
  settings.budget  = tonumber(settings.budget) or 12000
  settings.timeout = tonumber(settings.timeout) or 600
  settings.tokens  = tonumber(settings.tokens)

  if settings.prompt then
    -- Instructions of one's own, the ones a model has been taught on
    local file = io.open(settings.prompt)
    if not file then error(('prompt %s of %s cannot be read'):format(settings.prompt, settings.files)) end
    settings.instructions = file:read('a'):gsub('%s+$', '')
    file:close()
  end

  if (not settings.url) or (not settings.url:match('^https?://')) or
     ((settings.format ~= 'openai') and (settings.format ~= 'anthropic')) or
     ((settings.format == 'anthropic') and (not settings.model)) then
    error(('%s or %s needs url of http or https, format of openai or anthropic, and model for anthropic'):format(SYSTEM, user))
  end

  -- The language of the answers, of the locale when not said: ru of ru_RU.UTF-8, en of C.UTF-8 and POSIX
  local locale = os.getenv('LC_ALL') or os.getenv('LC_MESSAGES') or os.getenv('LANG') or ''
  settings.language = settings.language or locale:match('^(%l%l%l?)_') or locale:match('^(%l%l%l?)$') or 'en'

  return settings
end

local settings = load()

if not settings then
  return
end

local found, cjson = pcall(require, 'cjson')

if not found then
  error('lua-cjson for Lua 5.4 is needed: lua-cjson of Debian and Ubuntu, lua54-cjson of openSUSE; ' ..
        'elsewhere luarocks --lua-version 5.4 install lua-cjson')
end

-- The host of the address, and whether it is this machine: another one is told once before the first request
local host    = settings.url:match('^https?://%[([^%]]+)%]') or settings.url:match('^https?://([^/:]+)') or '?'
local trusted = (host == 'localhost') or (host == '::1') or (host:match('^127%.%d+%.%d+%.%d+$') ~= nil)

turbojedi.log(('%s: %s at %s, model %s, answers in %s%s%s%s'):format(settings.files, settings.format, settings.url,
  settings.model or 'of the server', settings.language, settings.prompt and (', instructions of ' .. settings.prompt) or '',
  settings.record and (', answers recorded to ' .. settings.record) or '', trusted and '' or ', another machine: asked before the first request'))

local jobs   = { }   -- Requests on the way, by their number
local number = 0

-- A field the journal has several times is a list, the first value tells enough here
local function first(value)
  if type(value) == 'table' then return value[1] end
  return value
end

-- One line of an entry for the model: time in the zone of the user, host, unit, process, level, message
local function line(item)
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
local function listing(items)
  local lines = { }
  local size  = 0
  local start = #items

  while start > 0 do
    local text = ('[%d] %s'):format(start, line(items[start]))
    if (size + #text > settings.budget) and (#lines > 0) then break end
    table.insert(lines, 1, text)
    size  = size + #text + 1
    start = start - 1
  end

  if start > 0 then
    table.insert(lines, 1, ('(%d older entries left out)'):format(start))
  end

  return table.concat(lines, '\n')
end

-- The instructions built in; prompt of the settings takes their place. Tools that teach a model of one's own read them
-- here, the way the plugin sends them
local INSTRUCTIONS = [[You help an administrator of a Linux server read entries of its systemd journal. Each entry is a
line [number] time host unit identifier[pid] LEVEL: message. Tell in a few sentences what has happened, the likely
cause, and what to check or do next. Name the entries that matter by their numbers in square brackets, as [3]. Do not
repeat the entries. Plain text, no Markdown. Answer in {language}.]]

-- The request of the format of the service: the instructions and the messages so far, the last one of the user
local function send(system, messages)
  local headers = { ['Content-Type'] = 'application/json' }
  local body

  if settings.format == 'anthropic' then
    headers['anthropic-version'] = '2023-06-01'
    if settings.key then headers['x-api-key'] = settings.key end
    body = { model = settings.model, max_tokens = settings.tokens or TOKENS, stream = false, system = system,
             messages = messages }
  else
    if settings.key then headers.Authorization = 'Bearer ' .. settings.key end
    -- The whole answer at once: some servers stream when not told, fm serve of macOS for one
    body = { model = settings.model, max_tokens = settings.tokens, stream = false,
             messages = { { role = 'system', content = system }, table.unpack(messages) } }
  end

  return { url = settings.url, method = 'POST', headers = headers, body = cjson.encode(body), timeout = settings.timeout }
end

local function language()
  return LANGUAGES[settings.language] or settings.language
end

-- The first request of a job, the instructions and the entries it has been made of; focus is the number of the entry
-- chosen when the ones before it are only what has led to it
local function request(items, focus)
  local tongue = language()
  local system = (settings.instructions or INSTRUCTIONS):gsub('{language}', function() return tongue end)
  local user   = listing(items)

  if focus then
    user = user .. ('\n\nExplain entry [%d]. The entries before it are of the same unit or program, only what has led to it.'):format(focus)
  end

  user = user .. '\n\nAnswer in ' .. tongue .. '.'
  return send(system, { { role = 'user', content = user } }), system, user
end

-- An answer with its request as a line of JSON of the file of record, for teaching a model of one's own: the entries are
-- in it, so the core makes the file 0600 and writes only to one of the process alone, never through a link
local function keep(job, text)
  local line = cjson.encode({ time = os.time(), url = settings.url, model = settings.model, format = settings.format,
                              language = settings.language, system = job.system, user = job.user, answer = text }) .. '\n'
  local done, reason = turbojedi.append(settings.record, line)

  if not done then
    turbojedi.log(('record %s cannot be written: %s'):format(settings.record, tostring(reason)), { problem = true })
  end
end

-- The text of an answer, or nil and why there is none
local function answer(code, body)
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

  -- The limit of tokens reached: the text stops where it has been, of a model that thinks first maybe before any
  if settings.format == 'anthropic' then
    local parts = { }
    for _, part in ipairs(type(data.content) == 'table' and data.content or { }) do
      if (type(part) == 'table') and (part.type == 'text') then parts[#parts + 1] = part.text end
    end
    text = table.concat(parts, '\n')
    cut  = data.stop_reason == 'max_tokens'
  else
    local choice = type(data.choices) == 'table' and data.choices[1]
    text = type(choice) == 'table' and type(choice.message) == 'table' and choice.message.content
    cut  = type(choice) == 'table' and choice.finish_reason == 'length'
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
    return nil, 'an empty text'
  elseif cut then
    text = text .. '...\n\n(Cut at the limit of tokens: tokens of arrakis.conf gives more.)'
  end

  return text
end

local converse

-- A failure in the watch and on the screen; the window has told it, so the line comes read. A question that has no
-- answer is not a part of the conversation
local function failed(job, text)
  if job.messages and (job.messages[#job.messages].role == 'user') then
    table.remove(job.messages)
  end

  job.watch:status(job.messages and 'done, the last question failed' or 'failed')
  job.watch:add(text, nil, { quiet = true })
  turbojedi.message(text, 'Arrakis')
end

-- The answer has come: its text in the watch and on the screen, the entries it names as lines that go to them. The
-- window shows it, so the lines come read and the watch does not call for attention
local function done(code, body, more, job)
  jobs[job.number] = nil

  if not code then
    failed(job, ('No answer from %s: %s, %s'):format(host, tostring(body), tostring(more)))
    return
  end

  local text, failure = answer(code, body)

  if not text then
    failed(job, ('%s has answered %s'):format(host, failure))
    return
  end

  if not job.messages then
    -- The first answer: the record is of single requests, the way a model of one's own is taught
    job.messages = { { role = 'user', content = job.user } }
    job.named    = { }
    if settings.record then keep(job, text) end
  end

  job.messages[#job.messages + 1] = { role = 'assistant', content = text }

  -- The entries the answer names first, then the answer: the newest line is on top, the answer over what it is about
  for found in text:gmatch('%[(%d+)%]') do
    local number = tonumber(found)
    if job.items[number] and not job.named[number] then
      job.named[number] = true
      job.watch:add(('[%d] %s'):format(number, line(job.items[number])), job.items[number], { quiet = true })
    end
  end

  -- Enter or a double click on the answer in the watch brings it back with a line for a question
  local index = #job.messages
  job.watch:status('done')
  job.watch:add(text, nil, { quiet = true, action = function() converse(job, index) end })

  converse(job)
end

-- An answer of a job, the last one without the index of its message, with a line for a question under it: the question
-- goes with the whole conversation of the job, the entries, the answers and the questions before it, and its answer
-- comes the same way
converse = function(job, index)
  if jobs[job.number] then
    turbojedi.message(('Arrakis is still working at %s'):format(job.title), 'Arrakis')
    return
  end

  local shown    = job.messages[index or #job.messages]
  local text     = ((shown and (shown.role == 'assistant')) and shown or job.messages[#job.messages]).content
  local question = turbojedi.message(text, job.title, { input = '~Q~uestion about the answer' })
  if not question then return end

  job.messages[#job.messages + 1] = { role = 'user', content = question .. '\n\nAnswer in ' .. language() .. '.' }

  -- The question in the watch brings its answer back, the one that comes next
  local following = #job.messages + 1
  job.watch:add('? ' .. question, nil, { quiet = true, action = function() converse(job, following) end })
  job.watch:status(('working at %s'):format(host))
  job.request = turbojedi.fetch(send(job.system, job.messages), done, job)
  jobs[job.number] = job
end

local function analyze(items, what, focus)
  if not trusted then
    if not turbojedi.confirm(('The entries go to %s, another machine. Send them?'):format(host)) then return end
    trusted = true
  end

  number = number + 1

  -- A watch of the job, its status and the answer in its lines; it reads no journal
  local title = ('Arrakis %d: %s'):format(number, what or (#items .. ' entries'))
  local watch = turbojedi.watch({ title = title })

  local query, system, user = request(items, focus)
  local job = { number = number, title = title, items = items, watch = watch, system = system, user = user }

  watch:status(('working at %s'):format(host))
  job.request = turbojedi.fetch(query, done, job)
  jobs[number] = job
end

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

local function cancel()
  for key, job in pairs(jobs) do
    job.request:cancel()
    jobs[key] = nil

    if job.messages then
      -- A question cancelled, the answers before it are there to ask about
      table.remove(job.messages)
      job.watch:status('done, the last question cancelled')
    else
      job.watch:status('cancelled')
    end
  end
end

turbojedi.registerHandler(function(item, menu, context)
  if context.marked then
    menu:add(('Arrakis: explain %d marked entries'):format(#context.marked), analyze, context.marked)
  else
    menu:add('Arrakis: explain this entry', single, item)
  end

  if next(jobs) then
    menu:add('Arrakis: cancel the jobs going on', cancel)
  end
end)
