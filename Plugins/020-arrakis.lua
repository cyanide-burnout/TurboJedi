-- TurboJedi, Turbo Journald Interface
-- https://github.com/cyanide-burnout/TurboJedi
-- Artem Prilutskiy, 2026

-- Arrakis: entries explained by a language model behind an HTTP API of the format of OpenAI (llama-server, Ollama,
-- vLLM, colibri, OpenAI) or of Anthropic. On Arrakis they mine the spice, here the sense of the journal. Actions explains
-- the entry under the cursor or the marked ones; the Plugins menu explains the errors among what the window shows
-- without marking them, and tells the settings in effect and asks the service whether it answers.
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
--   tokens   = 2048
--   prompt   = /etc/turbojedi/arrakis.prompt
--   record   = /var/tmp/arrakis.jsonl
--
-- format is openai or anthropic, which /v1/messages in the url tells as well; key a token for a service that wants one,
-- chmod 600 the file then; language of the answers, the one of the locale without it; budget characters of entries a
-- request takes at most; timeout seconds a request may take, 0 for no limit; tokens of an answer at most, the service
-- decides without it (2048 for Anthropic, which wants it said). prompt is a file of instructions instead of the ones
-- built in, {language} there the language of the answers: a model taught on instructions of its own gets them. record
-- is a file every answer goes to as a line of JSON with its request, for teaching a model of one's own on them: the
-- entries are in it, so it is made 0600 and written only when it is a file of the process alone. Under sudo, prompt and
-- record of the file of the user of sudo are left out: root reads and writes no path a user names. A value goes to the
-- end of its line, a comment is a line of its own.
--
-- Several engines, a large model of a service and a small one of one's own: [name] starts the lines of one, the lines
-- before the first [name] are of every engine, and a section of the same name in a file after is over the one before,
-- key by key. Of several, the first request of a run asks which one, the one chosen the last time offered first (kept in
-- ~/.config/turbojedi/arrakis-chosen.txt), and Choose the model of the Plugins menu changes it; a job keeps the engine it
-- has started with, its questions go there.
--
-- An answer comes with a line for a question under it, and Enter on an answer or a question in the lines of its watch
-- brings it back: the question goes with the whole conversation of its job, the entries, the answers and the questions
-- before it; a job starts on its own, nothing of the others goes along. The record keeps the first
-- request of a job and its answer only.
--
-- An address of another machine is asked about before the first request goes there, the entries leave the machine then;
-- a yes is kept in ~/.config/turbojedi/arrakis-trusted.txt, under sudo of the user of sudo, and holds while the url stays.
-- JSON is lua-cjson of OpenResty, built into TurboJedi.

local SYSTEM  = '/etc/turbojedi/arrakis.conf'
local CONTEXT = 30      -- Entries before a single one, of its unit or identifier, to tell the model what has led to it
local TOKENS  = 2048    -- Of an answer, Anthropic wants it said: a few sentences of Cyrillic take a thousand

local LEVELS = { [0] = 'EMERG', 'ALERT', 'CRIT', 'ERR', 'WARNING', 'NOTICE', 'INFO', 'DEBUG' }

-- A small model follows the name of a language, not its code
local LANGUAGES = { en = 'English', ru = 'Russian', uk = 'Ukrainian', de = 'German', fr = 'French', es = 'Spanish',
                    it = 'Italian', pt = 'Portuguese', pl = 'Polish', nl = 'Dutch', cs = 'Czech', sv = 'Swedish',
                    tr = 'Turkish', ja = 'Japanese', zh = 'Chinese', ko = 'Korean', he = 'Hebrew', ar = 'Arabic' }

-- The settings of a file over the ones there are: the lines before a [name] for every engine, the ones after it of the
-- engine of that name; false when there is no file
local function read(path, found)
  local file = io.open(path)
  if not file then return false end

  local into = found.common

  for line in file:lines() do
    local name = line:match('^%s*%[%s*(.-)%s*%]%s*$')

    if name then
      if not found.sections[name] then
        found.sections[name] = { }
        found.order[#found.order + 1] = name
      end
      into = found.sections[name]
    else
      local key, value = line:match('^%s*([%w_]+)%s*=%s*(.-)%s*$')
      if key then into[key] = value end
    end
  end

  file:close()
  return true
end

-- One engine: the settings of every engine with the ones of its section over them, checked; name is nil without sections
local function engine(common, section, name, files, user)
  local settings = { }

  for key, value in pairs(common) do settings[key] = value end
  for key, value in pairs(section or { }) do settings[key] = value end

  settings.name  = name
  settings.files = files

  settings.format  = settings.format or (settings.url and settings.url:match('/v1/messages$') and 'anthropic') or 'openai'
  settings.budget  = tonumber(settings.budget) or 12000
  settings.timeout = tonumber(settings.timeout) or 600
  settings.tokens  = tonumber(settings.tokens)

  if settings.prompt then
    -- Instructions of one's own, the ones a model has been taught on
    local file = io.open(settings.prompt)
    if not file then error(('prompt %s of %s cannot be read'):format(settings.prompt, files)) end
    settings.instructions = file:read('a'):gsub('%s+$', '')
    file:close()
  end

  if (not settings.url) or (not settings.url:match('^https?://')) or
     ((settings.format ~= 'openai') and (settings.format ~= 'anthropic')) or
     ((settings.format == 'anthropic') and (not settings.model)) then
    error(('%s or %s%s needs url of http or https, format of openai or anthropic, and model for anthropic'):format(SYSTEM,
      user, name and (', [' .. name .. '] there,') or ''))
  end

  -- The language of the answers, of the locale when not said: ru of ru_RU.UTF-8, en of C.UTF-8 and POSIX
  local locale = os.getenv('LC_ALL') or os.getenv('LC_MESSAGES') or os.getenv('LANG') or ''
  settings.language = settings.language or locale:match('^(%l%l%l?)_') or locale:match('^(%l%l%l?)$') or 'en'

  -- The host of the address, and whether it is this machine: another one is asked about before the first request
  settings.host    = settings.url:match('^https?://%[([^%]]+)%]') or settings.url:match('^https?://([^/:]+)') or '?'
  settings.trusted = (settings.host == 'localhost') or (settings.host == '::1') or
                     (settings.host:match('^127%.%d+%.%d+%.%d+$') ~= nil)

  return settings
end

-- The engines of the files, one without sections, or nil when there is no file
local function load()
  local home  = os.getenv('XDG_CONFIG_HOME') or ((os.getenv('HOME') or '') .. '/.config')
  local user  = home .. '/turbojedi/arrakis.conf'
  local found = { common = { }, sections = { }, order = { } }
  local sudo  = turbojedi.sudo and turbojedi.sudo()
  local paths = { SYSTEM, user }
  local used  = { }

  if sudo then
    -- The user who has run sudo, not root, has set up the engine
    user = sudo.home .. '/.config/turbojedi/arrakis.conf'
    paths[#paths + 1] = user
  end

  for index, path in ipairs(paths) do
    local values = { common = { }, sections = { }, order = { } }

    if read(path, values) then
      used[#used + 1] = path

      -- A section of the same name in a file after is over the one before, key by key, as the lines of every engine
      local function over(into, from)
        for key, value in pairs(from) do
          if sudo and (index == 3) and ((key == 'prompt') or (key == 'record')) then
            -- Root reads and writes no path a user names: /etc/turbojedi/arrakis.conf or the one of root tell them
            turbojedi.log(('%s of %s is left out under sudo: a path for root goes to %s'):format(key, path, SYSTEM), { problem = true })
          else
            into[key] = value
          end
        end
      end

      over(found.common, values.common)

      for _, name in ipairs(values.order) do
        if not found.sections[name] then
          found.sections[name] = { }
          found.order[#found.order + 1] = name
        end
        over(found.sections[name], values.sections[name])
      end
    end
  end

  if #used == 0 then
    -- Not set up: nothing is offered, nothing is sent anywhere
    turbojedi.log('no ' .. table.concat(paths, ', ') .. ': nothing to offer, nothing sent anywhere')
    return nil
  end

  local files   = table.concat(used, ', ')
  local engines = { }

  if #found.order == 0 then
    engines[1] = engine(found.common, nil, nil, files, user)
  end

  for _, name in ipairs(found.order) do
    engines[#engines + 1] = engine(found.common, found.sections[name], name, files, user)
  end

  return engines
end

-- Off without settings, or with settings or a module that cannot be taken
local cjson = require('cjson')

local loaded, engines = pcall(load)
local failure = (not loaded) and engines or nil

if failure then
  engines = nil
end

-- An address another than this machine a user has said yes to is kept in arrakis-trusted.txt among the settings of
-- TurboJedi, a url a line, until it changes; under sudo the file of the user of sudo, whose arrakis.conf tells the url.
-- The engine chosen of several is kept in arrakis-chosen.txt, the first one offered the next time
local TRUSTED = 'arrakis-trusted.txt'
local CHOSEN  = 'arrakis-chosen.txt'

for _, settings in ipairs(engines or { }) do
  if not settings.trusted then
    for line in (turbojedi.settings(TRUSTED) or ''):gmatch('[^\n]+') do
      if line:match('^%s*(.-)%s*$') == settings.url then settings.trusted = true end
    end
  end

  turbojedi.log(('%s: %s%s at %s, model %s, answers in %s%s%s%s'):format(settings.files,
    settings.name and (settings.name .. ', ') or '', settings.format, settings.url,
    settings.model or 'of the server', settings.language, settings.prompt and (', instructions of ' .. settings.prompt) or '',
    settings.record and (', answers recorded to ' .. settings.record) or '',
    settings.trusted and '' or ', another machine: asked before the first request'))
end

-- The engine of the requests: of several, the first request of a run asks which one, and Choose the model of the
-- Plugins menu asks again; nil when the choice is cancelled
local current

local function chosen(again)
  if #engines == 1 then return engines[1] end
  if current and not again then return current end

  local last    = current and current.name or (turbojedi.settings(CHOSEN) or ''):match('^%s*(.-)%s*$')
  local names   = { }
  local initial = 1

  for index, settings in ipairs(engines) do
    names[index] = ('%s: %s at %s'):format(settings.name, settings.model or 'the model of the server', settings.host)
    if settings.name == last then initial = index end
  end

  local _, index = turbojedi.choose('Arrakis: the model', names, initial)
  if not index then return nil end

  current = engines[index]

  if current.name ~= last then
    local kept, reason = turbojedi.settings(CHOSEN, current.name .. '\n')
    if not kept then turbojedi.log(('the choice is not kept: %s'):format(tostring(reason)), { problem = true }) end
  end

  return current
end

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
local function listing(settings, items)
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
local function send(settings, system, messages)
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

local function language(settings)
  return LANGUAGES[settings.language] or settings.language
end

-- The first request of a job, the instructions and the entries it has been made of; focus is the number of the entry
-- chosen when the ones before it are only what has led to it. Another plugin may tell more: notes before the entries,
-- within the budget as well, and the question after them
local function request(settings, items, focus, extra)
  local tongue = language(settings)
  local system = (settings.instructions or INSTRUCTIONS):gsub('{language}', function() return tongue end)
  local user   = listing(settings, items)
  local notes  = extra and extra.notes

  if notes and (notes ~= '') then
    if #notes > settings.budget then notes = notes:sub(1, settings.budget) .. '\n(the rest left out)' end
    user = notes .. '\n\n' .. user
  end

  if focus then
    user = user .. ('\n\nExplain entry [%d]. The entries before it are of the same unit or program, only what has led to it.'):format(focus)
  end

  if extra and extra.question then
    user = user .. '\n\n' .. extra.question
  end

  user = user .. '\n\nAnswer in ' .. tongue .. '.'
  return send(settings, system, { { role = 'user', content = user } }), system, user
end

-- An answer with its request as a line of JSON of the file of record, for teaching a model of one's own: the entries are
-- in it, so the core makes the file 0600 and writes only to one of the process alone, never through a link
local function keep(job, text)
  local settings = job.settings
  local line = cjson.encode({ time = os.time(), url = settings.url, model = settings.model, format = settings.format,
                              language = settings.language, system = job.system, user = job.user, answer = text }) .. '\n'
  local done, reason = turbojedi.append(settings.record, line)

  if not done then
    turbojedi.log(('record %s cannot be written: %s'):format(settings.record, tostring(reason)), { problem = true })
  end
end

-- The text of an answer, or nil and why there is none
local function answer(settings, code, body)
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
  if settings.format == 'anthropic' then
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

-- Of the screen only: the conversation keeps the text as the model has given it
local CUT = '...\n\n(Cut at the limit of tokens: tokens of arrakis.conf gives more.)'

local function shown(job, index)
  return job.messages[index].content .. (job.cut[index] and CUT or '')
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
  local settings = job.settings
  local host     = settings.host

  if not code then
    failed(job, ('No answer from %s: %s, %s'):format(host, tostring(body), tostring(more)))
    return
  end

  local text, failure, cut = answer(settings, code, body)

  if not text then
    failed(job, ('%s has answered %s'):format(host, failure))
    return
  end

  if not job.messages then
    -- The first answer: the record is of single requests, the way a model of one's own is taught
    job.messages = { { role = 'user', content = job.user } }
    job.named    = { }
    job.cut      = { }
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
  job.cut[index] = cut
  job.watch:status('done')
  job.watch:add(shown(job, index), nil, { quiet = true, action = function() converse(job, index) end })

  converse(job)
end

-- An answer of a job, the last one without the index of its message, with a line for a question under it: the question
-- goes with the whole conversation of the job, the entries, the answers and the questions before it, and its answer
-- comes the same way
converse = function(job, index)
  local settings = job.settings
  local chosen   = job.messages[index or #job.messages]
  local text     = shown(job, (chosen and (chosen.role == 'assistant')) and (index or #job.messages) or #job.messages)
  local question = turbojedi.message(text, job.title, { input = '~Q~uestion about the answer' })
  if not question then return end

  job.messages[#job.messages + 1] = { role = 'user', content = question .. '\n\nAnswer in ' .. language(settings) .. '.' }

  -- The question in the watch brings its answer back, the one that comes next
  local following = #job.messages + 1
  local asked = job.watch:add('? ' .. question, nil, { quiet = true, action = function() converse(job, following) end })
  -- The answer is waited for in a dialog with the seconds and Cancel, the conversation goes on from it
  job.watch:status(('working at %s'):format(settings.host))
  local code, body, more = turbojedi.fetch(send(settings, job.system, job.messages))

  if (not code) and (body == 'cancel') then
    -- The question goes, from the conversation and from the watch; the answers before it stay to ask about
    table.remove(job.messages)
    job.watch:delete(asked)
    job.watch:status('done, the last question cancelled')
    return
  end

  done(code, body, more, job)
end

-- A request of a job waits for its answer in a window with the seconds so far and Cancel, the windows go on meanwhile
local function analyze(items, what, focus, extra)
  local settings = chosen()
  if not settings then return end

  if not settings.trusted then
    if not turbojedi.confirm(('The entries go to %s, another machine. Send them?'):format(settings.host)) then return end
    settings.trusted = true

    -- Not asked again for this url; a file that cannot be written leaves it to this run
    local kept, reason = turbojedi.settings(TRUSTED, (turbojedi.settings(TRUSTED) or '') .. settings.url .. '\n')
    if not kept then turbojedi.log(('the answer is not kept: %s'):format(tostring(reason)), { problem = true }) end
  end

  number = number + 1

  -- A watch of the job, its status and the answer in its lines; it reads no journal. Of several models the title names
  -- the one of the job, its questions go there whatever is chosen later
  local title = ('Arrakis %d%s: %s'):format(number, settings.name and (' (' .. settings.name .. ')') or '', what or (#items .. ' entries'))
  local watch = turbojedi.watch({ title = title })

  local query, system, user = request(settings, items, focus, extra)
  local job = { number = number, title = title, items = items, watch = watch, system = system, user = user,
                settings = settings }

  watch:status(('working at %s'):format(settings.host))
  local code, body, more = turbojedi.fetch(query)

  if (not code) and (body == 'cancel') then
    watch:status('cancelled')
    return
  end

  done(code, body, more, job)
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

-- The errors among what the window shows, its filter and its time, without marking them: the newest go when there are
-- too many
local ERRORS = 200  -- Entries read at most, the budget takes the newest of them

local function errors()
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
local function show()
  local settings = chosen()
  if not settings then return end

  local host = settings.host
  local card = table.concat({
    ('Files     %s'):format(settings.files),
    ('Engine    %s'):format(settings.name and ('%s of %d, Choose the model of the Plugins menu changes it'):format(settings.name, #engines) or 'the one of the files'),
    ('Service   %s at %s'):format(settings.format, settings.url),
    ('Model     %s'):format(settings.model or 'of the server'),
    ('Key       %s'):format(settings.key and 'set' or 'none'),
    ('Language  %s'):format(language(settings)),
    ('Budget    %d characters of entries'):format(settings.budget),
    ('Timeout   %s'):format((settings.timeout > 0) and (settings.timeout .. ' seconds') or 'none'),
    ('Tokens    %s'):format(settings.tokens or ((settings.format == 'anthropic') and TOKENS) or 'of the server'),
    ('Prompt    %s'):format(settings.prompt or 'built in'),
    ('Record    %s'):format(settings.record or 'none'),
    ('Machine   %s'):format((host == 'localhost' or host == '::1' or host:match('^127%.')) and 'this one' or
      (settings.trusted and ('%s, the entries go there'):format(host) or ('%s, asked before the first request'):format(host))),
  }, '\n')

  if not turbojedi.confirm(card .. '\n\nSend a short request to see that the service answers?') then return end

  local started = os.time()
  local code, body, more = turbojedi.fetch(send(settings, 'Answer with the single word OK.', { { role = 'user', content = 'Are you there?' } }))
  if (not code) and (body == 'cancel') then return end
  if not code then return turbojedi.message(('No answer from %s: %s, %s'):format(host, tostring(body), tostring(more)), 'Arrakis') end

  local text, failure = answer(settings, code, body)
  turbojedi.message(text and ('%s has answered in %d s: %s'):format(host, os.time() - started, text) or ('%s has answered %s'):format(host, failure), 'Arrakis')
end

-- The items of Actions: the marked entries, or the entry under the cursor with what has come before it
local function actions(item, menu, context)
  if context.marked then
    menu:add(('Explain %d marked entries'):format(#context.marked), analyze, context.marked)
  else
    menu:add('Explain this entry', single, item)
  end
end

-- Off, Arrakis offers nothing: a failure goes to the problems of the start, no file is told in the console
if failure then
  error(failure, 0)
end

-- The model first when there are several, then what it does, then what is in effect
if engines then
  turbojedi.on('actions', actions)

  if #engines > 1 then
    turbojedi.on('menu', 'Choose the model', function() chosen(true) end)
  end

  turbojedi.on('menu', 'Explain the errors of the window', errors)
  turbojedi.on('menu', 'Status', show)

  -- An item of a list of bookmarks, which share the globals of Lua with Arrakis: the bookmarks of the list and their
  -- entries, what each has found told before them, the model asked what they have in common
  Bookmarks       = Bookmarks       or { }
  Bookmarks.items = Bookmarks.items or { }
  table.insert(Bookmarks.items, { title = 'Ask Arrakis about them', action = function(bookmarks, what)
    local items, notes = { }, { 'These are bookmarks the user has kept over time, each with its comment and topic, and the entries it keeps:' }

    for _, bookmark in ipairs(bookmarks) do
      local from = #items + 1
      for _, entry in ipairs(bookmark.entries) do items[#items + 1] = entry end
      notes[#notes + 1] = ('- made %s%s: %s; entries %s%s'):format(turbojedi.date('%Y-%m-%d %H:%M:%S', bookmark.created // 1000000),
        bookmark.topic and (', topic ' .. bookmark.topic) or '', bookmark.comment or '(no comment)',
        (#items >= from) and (('[%d]'):format(from) .. ((#items > from) and ('-[%d]'):format(#items) or '')) or 'none',
        (bookmark.left_out > 0) and (', %d more not kept'):format(bookmark.left_out) or '')
    end

    analyze(items, what, nil, { notes = table.concat(notes, '\n'),
      question = 'Tell what these bookmarks have in common, whether what they have found comes back and when, what may be '
              .. 'one cause behind several of them, and what to look at next.' })
  end })
end
