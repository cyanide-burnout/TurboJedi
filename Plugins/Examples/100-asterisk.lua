-- TurboJedi, Turbo Journald Interface
-- https://github.com/cyanide-burnout/TurboJedi
-- Artem Prilutskiy, 2026

-- Asterisk: the calls, channels, endpoints, modules and steps of the dialplan an entry names, an item for each with its
-- actions chosen there, a watch of a call. For an agent over MCP the calls of a number, what a call has been, and the
-- watch of it. An example to link on purpose: Hang up, Load and Reload change the PBX, and asterisk -rx needs root or the
-- group asterisk

local CALL     = [=[\[(C-[0-9a-f]{8})\]]=]  -- VERBOSE[1234][C-0000001a]
local CHANNEL  = [=[\b([A-Z][A-Za-z0-9]*/[^\s,()'"\[\]]+-[0-9a-f]{8}(?:;[12])?)]=]  -- PJSIP/2505-0000002b, Local/1@ctx-0000000a;1
local ENDPOINT = [=[\bEndpoint '?([\w.+-]+)'? is now]=]  -- Endpoint 2505 is now Reachable
local AOR      = [=[\bContact ([^\s/]+)/(?:sips?|tel):]=]  -- Contact 2505/sip:2505@203.0.113.7:5060 is now Reachable
local MODULE   = [=[\b((?:res|chan|app|func|cdr|cel|pbx|codec|format|bridge|bridges|stasis)_[a-z0-9_]+)(?:\.so)?\b]=]
local STEP     = [=[Executing \[([^@\]\s]+@[^:\]\s]+):\d+\]]=]  -- Executing [2505@from-internal:1]
local SAFE     = '^[%w._@:;/+#*-]+$'  -- A value the CLI takes as one argument, nothing more
local NUMBER   = '^[%w.+#*_-]+$'       -- An extension or an endpoint a search takes
local FOUND    = 200                   -- Entries a search reads at most
local IDENTIFIER = 'asterisk'

-- The CLI in the PATH, or where a build of Asterisk of its own goes: /opt and sbin are out of the PATH of sudo
local PLACES = { '/opt/asterisk/sbin/asterisk', '/usr/local/sbin/asterisk', '/usr/sbin/asterisk', '/opt/asterisk/bin/asterisk' }

local cli = turbojedi.which('asterisk')

for _, place in ipairs(PLACES) do
  local file = (not cli) and io.open(place)
  if file then
    file:close()
    cli = place
  end
end

turbojedi.log(cli and ('calls, channels and the rest by ' .. cli .. ' -rx, the journal for their history')
                   or 'asterisk is not in the PATH, nor in ' .. table.concat(PLACES, ', ') .. ': only the history of an entry in the journal')

local function run(command)
  turbojedi.execute({ cli, '-rx', command }, { mode = 'window' })
end

local function sure(question, command)
  if turbojedi.confirm(question) then run(command) end
end

-- The actions of an object in a popup menu, the chosen one is run
local function offer(title, actions)
  local names = { }
  for index, action in ipairs(actions) do names[index] = action[1] end
  local _, index = turbojedi.popup(names, title)
  if index then actions[index][2]() end
end

-- The actions of an object in a submenu of Actions
local function sub(menu, title, actions)
  local items = menu:sub(title)
  for _, action in ipairs(actions) do items:add(action[1], action[2]) end
end

-- Where else the journal tells about it, as the plugins of TetraPack and BrandMeister do
local function trace(entry, text)
  local unit = { _SYSTEMD_UNIT = entry._SYSTEMD_UNIT }
  return
  {
    { 'History',          function() turbojedi.open { matches = unit, text = text } end },
    { 'Mark history',     function()
                            local entries, stopped = turbojedi.query { matches = unit, text = text, limit = 1000 }
                            local count = turbojedi.mark(entries, { clear = true })
                            if stopped or (count < #entries) then
                              turbojedi.message(('Marked %d of %d entries with %s%s'):format(count, #entries, text,
                                stopped and (', there may be more: the reading has stopped by ' .. stopped) or ', the rest are not in the window'))
                            end
                          end },
    { 'Previous mention', function() if not turbojedi.find(text) then turbojedi.message('No earlier mention of ' .. text) end end },
    { 'Copy',             function() turbojedi.copy(text) end }
  }
end

local function add(list, more)
  for _, action in ipairs(more) do list[#list + 1] = action end
  return list
end

-- Watches of calls, kept by a layout over a restart: a line for a step of the dialplan or a channel that comes or goes,
-- the status tells the steps and the last of them
local watching = { }  -- By the process and the identifier of the call, until Stop watching or the user removes the watch

-- A call of a process: 4242/C-0000001a
local function reference(pid, id)
  return tostring(pid) .. '/' .. id
end

-- What a line of a call says without the prefix of the logger: Executing [2505@from-internal:1] Dial(...)
local function said(message)
  return (tostring(message):gsub('^.-%]: ', ''):gsub('^[%w_.]+:%d+ in [%w_]+: ', ''):gsub('^%-%- ', ''):gsub('%s+$', ''))
end

-- Of the process the call has been in: Asterisk counts its calls from C-00000001 again once started anew
local function follow(context, position)
  local matches = context.unit and { _SYSTEMD_UNIT = context.unit } or { SYSLOG_IDENTIFIER = context.identifier or IDENTIFIER }
  matches._PID  = context.pid
  local steps   = 0
  local ref     = reference(context.pid, context.id)

  watching[ref] = turbojedi.watch({ title = 'Call ' .. context.id, key = 'call:' .. ref, matches = matches,
    text = '[' .. context.id .. ']', context = context, from = position, removed = function() watching[ref] = nil end },
    function(entry, watch)
      local message = said(entry.MESSAGE)
      if message:match('^Executing %[') then steps = steps + 1 end
      watch:status(('%d step%s, last: %s'):format(steps, (steps == 1) and '' or 's', message))
      watch:add(message, entry)
    end)
end

local function watch(entry, id)
  follow { id = id, unit = entry._SYSTEMD_UNIT, identifier = entry.SYSLOG_IDENTIFIER, pid = entry._PID }
end

local function unwatch(entry, id)
  local ref = reference(entry._PID, id)
  watching[ref]:remove()
  watching[ref] = nil
end

-- The actions of a call: its history and its watch; alone when found by the Plugins menu, with no entry of the window to
-- look back from
local function follows(entry, id, alone)
  local actions = { }
  for _, action in ipairs(trace(entry, '[' .. id .. ']')) do
    if not (alone and (action[1] == 'Previous mention')) then actions[#actions + 1] = action end
  end
  if turbojedi.watch then
    actions[#actions + 1] = watching[reference(entry._PID, id)] and { 'Stop watching', function() unwatch(entry, id) end } or { 'Watch', function() watch(entry, id) end }
  end
  return actions
end

-- The items of the entry in Actions
local function actions(entry, menu, context)
  local text = entry.MESSAGE
  if (type(text) ~= 'string') or not ((entry.SYSLOG_IDENTIFIER == 'asterisk') or tostring(entry._SYSTEMD_UNIT):match('^asterisk')) then return end
  local here = cli and (context.source == 'local')  -- The CLI is of the PBX of this machine
  local seen = { }

  local function once(kind, value)
    if seen[kind .. value] or not value:match(SAFE) then return false end
    seen[kind .. value] = true
    return true
  end

  for found in turbojedi.matches(text, CALL) do
    local id = found[1]
    if once('call', id) then
      sub(menu, 'Call ' .. id, follows(entry, id))
    end
  end

  for found in turbojedi.matches(text, CHANNEL) do
    local name = found[1]
    if once('channel', name) then
      local actions = here and
      {
        { 'Show',    function() run('core show channel ' .. name) end },
        { 'Hang up', function() sure('Hang up channel ' .. name .. '?', 'channel request hangup ' .. name) end }
      } or { }
      sub(menu, 'Channel ' .. name, add(actions, trace(entry, name)))
    end
    -- PJSIP/2505-0000002b is of the endpoint 2505
    local endpoint = name:match('^PJSIP/([%w._+-]+)%-%x+')
    if here and endpoint and once('endpoint', endpoint) then
      sub(menu, 'Endpoint ' .. endpoint,
      {
        { 'Show',    function() run('pjsip show endpoint ' .. endpoint) end },
        { 'Qualify', function() run('pjsip qualify ' .. endpoint) end }
      })
    end
  end

  for found in turbojedi.matches(text, ENDPOINT) do
    local endpoint = found[1]
    if here and once('endpoint', endpoint) then
      sub(menu, 'Endpoint ' .. endpoint,
      {
        { 'Show',    function() run('pjsip show endpoint ' .. endpoint) end },
        { 'Qualify', function() run('pjsip qualify ' .. endpoint) end }
      })
    end
  end

  -- A contact tells its AOR, which need not be named as an endpoint is
  for found in turbojedi.matches(text, AOR) do
    local aor = found[1]
    if here and once('aor', aor) then
      sub(menu, 'AOR ' .. aor, { { 'Show', function() run('pjsip show aor ' .. aor) end } })
    end
  end

  -- Names of modules come up in messages of the loader and of modules, not in any text
  if here and (text:find('loader') or text:lower():find('module')) then
    for found in turbojedi.matches(text, MODULE) do
      local module = found[1]
      if once('module', module) then
        sub(menu, 'Module ' .. module,
        {
          { 'Show',   function() run('module show like ' .. module) end },
          { 'Load',   function() sure('Load module ' .. module .. '?', 'module load ' .. module .. '.so') end },
          { 'Reload', function() sure('Reload module ' .. module .. '?', 'module reload ' .. module .. '.so') end }
        })
      end
    end
  end

  for found in turbojedi.matches(text, STEP) do
    local step = found[1]
    if here and once('step', step) then
      sub(menu, 'Dialplan ' .. step, { { 'Show', function() run('dialplan show ' .. step) end } })
    end
  end
end

-- Methods for an agent over Invoke() of TurboJedi 0.18 and later, in the journal of the window they are called in: the
-- calls of a number, what a call has been, and the watch of it. They read the journal and set a watch, the CLI is left
-- to the user

-- A text the regular expression takes as it is
local function literal(text)
  return (text:gsub('[%^%$%(%)%.%[%]%*%+%-%?%{%}%|\\]', '\\%0'))
end

-- The calls a number has made or taken, the newest first: an extension of the dialplan, Executing [2505@from-internal:1],
-- has taken it; otherwise an endpoint of a channel, PJSIP/2505-0000002b, has made it. Every mention of the number in a
-- call tells, the callee has a channel of its own as well
local function findCalls(number)
  local entries, stopped = turbojedi.query { matches = { SYSLOG_IDENTIFIER = IDENTIFIER }, regex = true, limit = FOUND,
    text = [=[\[C-[0-9a-f]{8}\].*(?:Executing \[]=] .. literal(number) .. [=[@|/]=] .. literal(number) .. [=[-[0-9a-f]{8}\b)]=] }
  local calls, found = { }, { }
  for _, entry in ipairs(entries) do
    local message = tostring(entry.MESSAGE)
    local id      = turbojedi.regex(message, CALL)
    if id then
      -- A call is of its process, the identifiers begin anew with Asterisk
      local key  = tostring(entry._PID) .. id[1]
      local call = calls[key]
      if not call then
        -- Its newest mention tells where it has got to
        call = { id = id[1], pid = tonumber(entry._PID), cursor = entry.__CURSOR, time = entry.__REALTIME_TIMESTAMP,
                 role = 'made', said = said(message) }
        calls[key] = call
        found[#found + 1] = call
      end
      local step    = message:match('Executing %[([^%]]+)%]')
      local channel = turbojedi.regex(message, CHANNEL)
      if step and (step:match('^([^@]+)@') == number) then call.role = 'taken' end
      call.step    = step or call.step
      call.channel = channel and channel[1] or call.channel
    end
  end
  return found, stopped
end

-- A call by its mentions in its process, the newest one of that identifier without one, the oldest first: its channels
-- in the order they have come, the steps of its dialplan, the last thing said
local function findCall(id, pid)
  local matches = { SYSLOG_IDENTIFIER = IDENTIFIER }
  if not pid then
    local newest = turbojedi.query { matches = matches, text = '[' .. id .. ']', limit = 1 } [1]
    if not newest then return nil, 'No call ' .. id .. ' in the journal of Asterisk' end
    pid = newest._PID
  end
  matches._PID = tostring(pid)
  local entries, stopped = turbojedi.query { matches = matches, text = '[' .. id .. ']', limit = FOUND }
  if #entries == 0 then return nil, 'No call ' .. id .. ' of process ' .. tostring(pid) .. ' in the journal of Asterisk' end
  local channels, steps, seen = { }, { }, { }
  for index = #entries, 1, -1 do
    local entry   = entries[index]
    local message = tostring(entry.MESSAGE)
    for found in turbojedi.matches(message, CHANNEL) do
      if not seen[found[1]] then
        seen[found[1]] = true
        channels[#channels + 1] = found[1]
      end
    end
    local step, rest = message:match('Executing %[([^%]]+)%]%s*(.-)%s*$')
    if step then steps[#steps + 1] = { step = step, application = rest, time = entry.__REALTIME_TIMESTAMP } end
  end
  return { first = entries[#entries], last = entries[1], channels = channels, steps = steps, count = #entries }, stopped
end

-- The Plugins menu: the calls of a number, a call by its identifier, as the journal of the window tells. A call found
-- gets the actions of the journal and the watch, the CLI is for the entries of the window
local date = turbojedi.date or os.date

-- What a search has left out: the older ones past its limit, or the rest past where the reading has stopped
local function more(stopped)
  if stopped == 'limit' then return ', the newest ones' end
  return stopped and (', the reading has stopped by ' .. stopped) or ''
end

local function ask(prompt)
  local text = turbojedi.input(prompt, '')
  return text and text:match('^%s*(.-)%s*$')
end

local function calls()
  local number = ask('Calls of the number')
  if not number or (number == '') then return end
  if not number:match(NUMBER) then return turbojedi.message(number .. ' is not an extension or an endpoint') end
  local found, stopped = findCalls(number)
  if #found == 0 then return turbojedi.message('No call of ' .. number .. ' in the journal' .. more(stopped)) end
  local names = { }
  for index, call in ipairs(found) do
    names[index] = ('%s  %s  %s'):format(date('%F %T', call.time // 1000000), call.id,
      (call.role == 'taken') and ('taken by ' .. call.step) or ('made from ' .. tostring(call.channel)))
  end
  local _, index = turbojedi.choose('Calls of ' .. number .. more(stopped), names)
  if not index then return end
  local id = found[index].id
  local call, reason = findCall(id, found[index].pid)
  if not call then return turbojedi.message(reason) end
  offer('Call ' .. id, follows(call.first, id, true))
end

local function identify()
  local id = ask('Call, such as C-0000001a')
  if not id or (id == '') then return end
  if not id:match('^C%-%x%x%x%x%x%x%x%x$') then return turbojedi.message(id .. ' is not a call, such as C-0000001a') end
  local call, stopped = findCall(id)
  if not call then return turbojedi.message(stopped) end
  offer('Call ' .. id, follows(call.first, id, true))
end

local function methods()
  turbojedi.on('invoke', 'calls', function(values)
    local number = tostring(values.number)
    if not number:match(NUMBER) then return { error = number .. ' is not an extension or an endpoint' } end
    local found, stopped = findCalls(number)
    return { calls = { type = 'aa{sv}', value = found }, stopped = stopped or '' }
  end, { description = 'The calls a number has made or taken in the journal of Asterisk, the newest first: id, the identifier '
           .. 'of the call as C-0000001a, pid of the process of Asterisk it has been in, cursor and time of its newest mention '
           .. 'and what it says, role (taken by an extension of the number, made by an endpoint of it otherwise), the first '
           .. 'step of the dialplan and channel among its mentions. '
           .. 'stopped tells why the reading has ended early: limit (200), scan or time',
         effect = 'none', arguments = { number = { type = 's', description = 'an extension or an endpoint, such as 2505', required = true } } })

  local PID = { type = 'u', description = 'the process of Asterisk the call has been in, as calls tells it; the newest call of '
                .. 'that identifier without it, Asterisk counts them anew when started again' }

  turbojedi.on('invoke', 'call', function(values)
    local id = tostring(values.id)
    if not id:match('^C%-%x%x%x%x%x%x%x%x$') then return { error = id .. ' is not a call, such as C-0000001a' } end
    local call, stopped = findCall(id, values.pid)
    if not call then return { error = stopped } end
    return { first = said(call.first.MESSAGE), last = said(call.last.MESSAGE), since = call.first.__REALTIME_TIMESTAMP,
             till = call.last.__REALTIME_TIMESTAMP, cursor = call.first.__CURSOR, mentions = call.count, stopped = stopped or '',
             channels = { type = 'as', value = call.channels }, steps = { type = 'aa{sv}', value = call.steps } }
  end, { description = 'What a call of Asterisk has been, by its mentions in the journal: its channels in the order they have '
           .. 'come, the steps of its dialplan with their applications, the first and the last thing said, since and till, '
           .. 'the cursor of its first mention',
         effect = 'none', arguments = { id = { type = 's', description = 'the identifier of the call the logger of Asterisk gives it, such as C-0000001a', required = true }, pid = PID } })

  turbojedi.on('invoke', 'watch', function(values)
    local id = tostring(values.id)
    if not id:match('^C%-%x%x%x%x%x%x%x%x$') then return { error = id .. ' is not a call, such as C-0000001a' } end
    local call, stopped = findCall(id, values.pid)
    if not call then return { error = stopped } end
    local ref    = reference(call.first._PID, id)
    local answer = { key = 'call:' .. ref, title = 'Call ' .. id }
    if watching[ref] then answer.already = true return answer end
    follow({ id = id, unit = call.first._SYSTEMD_UNIT, identifier = call.first.SYSLOG_IDENTIFIER, pid = call.first._PID }, call.first)
    if not watching[ref] then return { error = 'The journal of the call cannot be followed' } end
    return answer
  end, { description = 'Sets the watch of a call the user sees in the list of watches, as Watch of its Actions: a line for every '
           .. 'step of its dialplan and every mention, from its first one on, and the ones as they come while it goes on; key '
           .. 'and title tell it in list_watches. Only the user removes it',
         effect = 'screen', arguments = { id = { type = 's', description = 'the identifier of the call the logger of Asterisk gives it, such as C-0000001a', required = true }, pid = PID } })
end

turbojedi.on('actions', actions)
turbojedi.on('restore', follow)
turbojedi.on('menu', 'Calls of a number...', calls)
turbojedi.on('menu', 'Call by its identifier...', identify)
if turbojedi.left then methods() end
