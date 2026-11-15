-- TurboJedi, Turbo Journald Interface
-- https://github.com/cyanide-burnout/TurboJedi
-- Artem Prilutskiy, 2026

-- A core dump systemd-coredump has told about: what it knows of it, a copy of that, its stack in gdb; they only look

if not turbojedi.which('coredumpctl') then return end

local DUMPED = 'fc2e22bc6ee647b6b90729ab34a250b1'  -- MESSAGE_ID of systemd-coredump

-- The PID and the time of the dump pick this one alone, a PID alone may stand for several dumps
local function command(verb, entry, context)
  local arguments = { 'coredumpctl', '--no-pager' }
  if context.source == 'directory' then arguments[#arguments + 1] = '--directory=' .. context.directory end
  arguments[#arguments + 1] = verb
  arguments[#arguments + 1] = 'COREDUMP_PID=' .. entry.COREDUMP_PID
  if type(entry.COREDUMP_TIMESTAMP) == 'string' then arguments[#arguments + 1] = 'COREDUMP_TIMESTAMP=' .. entry.COREDUMP_TIMESTAMP end
  return arguments
end

local function info(entry, context)
  turbojedi.output(command('info', entry, context))
end

local function copy(entry, context)
  local text, how, code, errors = turbojedi.capture(command('info', entry, context))
  if (how == 'exit') and (code == 0) then return turbojedi.copy(text) end
  turbojedi.message('coredumpctl: ' .. (errors or how .. ' ' .. tostring(code)))
end

local function debug(entry, context)
  turbojedi.terminal(command('debug', entry, context))
end

turbojedi.registerHandler(function(entry, menu, context)
  if (entry.MESSAGE_ID ~= DUMPED) or (type(entry.COREDUMP_PID) ~= 'string') then return end
  local name = tostring(entry.COREDUMP_COMM or entry.COREDUMP_EXE or 'process') .. ' ' .. entry.COREDUMP_PID
  menu:add('Core dump of ' .. name, info, entry, context)
  menu:add('Copy core dump of ' .. name, copy, entry, context)
  -- The core file is on the machine where it has been dumped
  if (context.source == 'local') and turbojedi.which('gdb') then menu:add('Debug core dump of ' .. name, debug, entry, context) end
end)
