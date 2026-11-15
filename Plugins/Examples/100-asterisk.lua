-- TurboJedi, Turbo Journald Interface
-- https://github.com/cyanide-burnout/TurboJedi
-- Artem Prilutskiy, 2026

-- Asterisk: the calls, channels, endpoints, modules and steps of the dialplan an entry names, an item for each with its
-- actions chosen there. An example to link on purpose: Hang up, Load and Reload change the PBX, and asterisk -rx needs
-- root or the group asterisk

local CALL     = [=[\[(C-[0-9a-f]{8})\]]=]  -- VERBOSE[1234][C-0000001a]
local CHANNEL  = [=[\b([A-Z][A-Za-z0-9]*/[^\s,()'"\[\]]+-[0-9a-f]{8}(?:;[12])?)]=]  -- PJSIP/2505-0000002b, Local/1@ctx-0000000a;1
local ENDPOINT = [=[\bEndpoint '?([\w.+-]+)'? is now]=]  -- Endpoint 2505 is now Reachable
local AOR      = [=[\bContact ([^\s/]+)/(?:sips?|tel):]=]  -- Contact 2505/sip:2505@203.0.113.7:5060 is now Reachable
local MODULE   = [=[\b((?:res|chan|app|func|cdr|cel|pbx|codec|format|bridge|bridges|stasis)_[a-z0-9_]+)(?:\.so)?\b]=]
local STEP     = [=[Executing \[([^@\]\s]+@[^:\]\s]+):\d+\]]=]  -- Executing [2505@from-internal:1]
local SAFE     = '^[%w._@:;/+#*-]+$'  -- A value the CLI takes as one argument, nothing more

local cli = turbojedi.which('asterisk')

local function run(command)
  turbojedi.output { 'asterisk', '-rx', command }
end

local function sure(question, command)
  if turbojedi.confirm(question) then run(command) end
end

-- The actions of an object in a list, the chosen one is run
local function offer(title, actions)
  local names = { }
  for index, action in ipairs(actions) do names[index] = action[1] end
  local _, index = turbojedi.choose(title, names)
  if index then actions[index][2]() end
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

turbojedi.registerHandler(function(entry, menu, context)
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
      menu:add('Call ' .. id, offer, 'Call ' .. id, trace(entry, '[' .. id .. ']'))
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
      menu:add('Channel ' .. name, offer, 'Channel ' .. name, add(actions, trace(entry, name)))
    end
    -- PJSIP/2505-0000002b is of the endpoint 2505
    local endpoint = name:match('^PJSIP/([%w._+-]+)%-%x+')
    if here and endpoint and once('endpoint', endpoint) then
      menu:add('Endpoint ' .. endpoint, offer, 'Endpoint ' .. endpoint,
      {
        { 'Show',    function() run('pjsip show endpoint ' .. endpoint) end },
        { 'Qualify', function() run('pjsip qualify ' .. endpoint) end }
      })
    end
  end

  for found in turbojedi.matches(text, ENDPOINT) do
    local endpoint = found[1]
    if here and once('endpoint', endpoint) then
      menu:add('Endpoint ' .. endpoint, offer, 'Endpoint ' .. endpoint,
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
      menu:add('AOR ' .. aor, offer, 'AOR ' .. aor, { { 'Show', function() run('pjsip show aor ' .. aor) end } })
    end
  end

  -- Names of modules come up in messages of the loader and of modules, not in any text
  if here and (text:find('loader') or text:lower():find('module')) then
    for found in turbojedi.matches(text, MODULE) do
      local module = found[1]
      if once('module', module) then
        menu:add('Module ' .. module, offer, 'Module ' .. module,
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
      menu:add('Dialplan ' .. step, offer, 'Dialplan ' .. step, { { 'Show', function() run('dialplan show ' .. step) end } })
    end
  end
end)
