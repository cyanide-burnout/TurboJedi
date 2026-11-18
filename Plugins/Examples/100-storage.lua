-- TurboJedi, Turbo Journald Interface
-- https://github.com/cyanide-burnout/TurboJedi
-- Artem Prilutskiy, 2026

-- Block devices of an entry of the kernel or smartd: the device, its mounts, udev, SMART or a copy are chosen for each.
-- They only look; an example to link on purpose, as names of devices come up in messages by chance, and SMART needs root

-- sda, sda1, vdb, xvda, nvme0n1, nvme0n1p2, mmcblk0p1, dm-3, md127, as the kernel writes them: [sda], dev sda, (sda1)
local DEVICE = [[(?<![\w/-])((?:[svx]?d[a-z]+|hd[a-z]+)\d*|nvme\d+n\d+(?:p\d+)?|mmcblk\d+(?:p\d+)?|dm-\d+|md\d+)(?![\w-])]]

-- The disk of a partition, SMART is of the disk
local function disk(name)
  return name:match('^(nvme%d+n%d+)p%d+$') or name:match('^(mmcblk%d+)p%d+$') or name:match('^([svx]?d%a+)%d+$') or name:match('^(hd%a+)%d+$') or name
end

-- Title, the program it needs, the action; only the ones whose program is there are offered
local ACTIONS =
{
  { 'Device', 'lsblk',    function(name) turbojedi.execute({ 'lsblk', '-o', 'NAME,SIZE,TYPE,FSTYPE,LABEL,MOUNTPOINTS,MODEL,SERIAL,STATE', '/dev/' .. name }, { mode = 'window' }) end },
  { 'Mounts', 'findmnt',  function(name) turbojedi.execute({ 'findmnt', '--source', '/dev/' .. name }, { mode = 'window' }) end },
  { 'udev',   'udevadm',  function(name) turbojedi.execute({ 'udevadm', 'info', '--query=all', '--name=' .. name }, { mode = 'window' }) end },
  { 'SMART',  'smartctl', function(name) turbojedi.execute({ 'smartctl', '-a', '/dev/' .. disk(name) }, { mode = 'window' }) end },
  { 'Copy',   nil,        turbojedi.copy }
}

local titles    = { }
local functions = { }

for _, action in ipairs(ACTIONS) do
  if (action[2] == nil) or turbojedi.which(action[2]) then
    titles[#titles + 1]       = action[1]
    functions[#functions + 1] = action[3]
  end
end

turbojedi.log('for a block device: ' .. table.concat(titles, ', ') .. ', the ones whose programs are installed')

local function choose(name)
  local _, index = turbojedi.choose('Device ' .. name, titles)
  if index then functions[index](name) end
end

-- The items of the entry in Actions
local function actions(entry, menu, context)
  -- Devices are of this machine, and of the kernel or smartd that tell about them
  if (context.source ~= 'local') or not ((entry._TRANSPORT == 'kernel') or (entry.SYSLOG_IDENTIFIER == 'smartd')) then return end
  local seen = { }
  for found in turbojedi.matches(entry.MESSAGE, DEVICE) do
    local name = found[1]
    -- Only a device the system has, a word that looks like one is not
    local file = (not seen[name]) and io.open('/sys/class/block/' .. name .. '/dev')
    if file then
      file:close()
      seen[name] = true
      menu:add('Device ' .. name, choose, name)
    end
  end
end

turbojedi.on('actions', actions)
