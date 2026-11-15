-- TurboJedi, Turbo Journald Interface
-- https://github.com/cyanide-burnout/TurboJedi
-- Artem Prilutskiy, 2026

-- The unit and the process of an entry: they are only looked at, nothing is changed

if not turbojedi.which('systemctl') then
  return
end

local function status(unit)
  -- systemctl paints its status only for a terminal unless asked
  turbojedi.output { 'env', 'SYSTEMD_COLORS=1', 'systemctl', 'status', '--no-pager', '--full', unit }
end

local function file(unit)
  turbojedi.output { 'systemctl', 'cat', '--no-pager', unit }
end

local function top(pid)
  turbojedi.terminal { 'top', '-p', pid }
end

turbojedi.registerHandler(function(entry, menu, context)
  -- Units and processes belong to this machine, a journal of other ones tells about theirs
  if context.source ~= 'local' then
    return
  end

  local unit = entry._SYSTEMD_UNIT

  if type(unit) == 'string' then
    menu:add('Status of ' .. unit, status, unit)
    menu:add('Unit file of ' .. unit, file, unit)
  end

  local pid  = entry._PID
  local name = entry._COMM

  if (type(pid) == 'string') and (type(name) == 'string') then
    -- Only while the process runs, and is still the same program: a number of a gone one may be taken by another
    local comm = io.open('/proc/' .. pid .. '/comm')

    if comm then
      local current = comm:read('l')
      comm:close()

      if current == name then
        menu:add('Process ' .. pid .. ' (' .. name .. ') in top', top, pid)
      end
    end
  end
end)
