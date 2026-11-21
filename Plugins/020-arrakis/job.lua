-- TurboJedi, Turbo Journald Interface
-- https://github.com/cyanide-burnout/TurboJedi
-- Artem Prilutskiy, 2026

-- A job: a request in a watch of its own, its answer there and on the screen, the entries the answer names as lines that
-- go to them, and the questions about the answer with the whole conversation of the job

local plugin   = (...):match('^[^.]+')
local settings = require(plugin .. '.settings')
local request  = require(plugin .. '.request')

local line, send, answer = request.line, request.send, request.answer

local M = { }

local number = 0

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
  local engine = job.settings
  local host   = engine.host

  if not code then
    failed(job, ('No answer from %s: %s, %s'):format(host, tostring(body), tostring(more)))
    return
  end

  local text, failure, cut = answer(engine, code, body)

  if not text then
    failed(job, ('%s has answered %s'):format(host, failure))
    return
  end

  if not job.messages then
    -- The first answer: the record is of single requests, the way a model of one's own is taught
    job.messages = { { role = 'user', content = job.user } }
    job.named    = { }
    job.cut      = { }
    if engine.record then request.keep(job, text) end
  end

  job.messages[#job.messages + 1] = { role = 'assistant', content = text }

  -- The entries the answer names first, then the answer: the newest line is on top, the answer over what it is about
  for found in text:gmatch('%[(%d+)%]') do
    local index = tonumber(found)
    if job.items[index] and not job.named[index] then
      job.named[index] = true
      job.watch:add(('[%d] %s'):format(index, line(job.items[index])), job.items[index], { quiet = true })
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
  local engine   = job.settings
  local chosen   = job.messages[index or #job.messages]
  local text     = shown(job, (chosen and (chosen.role == 'assistant')) and (index or #job.messages) or #job.messages)
  local question = turbojedi.message(text, job.title, { input = '~Q~uestion about the answer' })
  if not question then return end

  job.messages[#job.messages + 1] = { role = 'user', content = question .. '\n\nAnswer in ' .. settings.language(engine) .. '.' }

  -- The question in the watch brings its answer back, the one that comes next
  local following = #job.messages + 1
  local asked = job.watch:add('? ' .. question, nil, { quiet = true, action = function() converse(job, following) end })
  -- The answer is waited for in a dialog with the seconds and Cancel, the conversation goes on from it
  job.watch:status(('working at %s'):format(engine.host))
  local code, body, more = turbojedi.fetch(send(engine, job.system, job.messages))

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
function M.analyze(items, what, focus, extra)
  local engine = settings.chosen()
  if (not engine) or (not settings.trust(engine)) then return end

  number = number + 1

  -- A watch of the job, its status and the answer in its lines; it reads no journal. Of several models the title names
  -- the one of the job, its questions go there whatever is chosen later
  local title = ('Arrakis %d%s: %s'):format(number, engine.name and (' (' .. engine.name .. ')') or '', what or (#items .. ' entries'))
  local watch = turbojedi.watch({ title = title })

  local query, system, user = request.request(engine, items, focus, extra)
  local job = { number = number, title = title, items = items, watch = watch, system = system, user = user,
                settings = engine }

  watch:status(('working at %s'):format(engine.host))
  local code, body, more = turbojedi.fetch(query)

  if (not code) and (body == 'cancel') then
    watch:status('cancelled')
    return
  end

  done(code, body, more, job)
end

return M
