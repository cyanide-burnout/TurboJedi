-- TurboJedi, Turbo Journald Interface
-- https://github.com/cyanide-burnout/TurboJedi
-- Artem Prilutskiy, 2026

-- Addresses of an entry: one item for each, its route, lookup, whois, ping, path or a copy are chosen there; they only look

-- IPv4, or IPv6 in full or with ::, not a time such as 12:34:56
local ADDRESS = [[(?<![\w.:])(?<address>(?:\d{1,3}\.){3}\d{1,3}|(?:[0-9a-fA-F]{1,4}:){7}[0-9a-fA-F]{1,4}|(?:[0-9a-fA-F]{1,4}(?::[0-9a-fA-F]{1,4})*)?::(?:[0-9a-fA-F]{1,4}(?::[0-9a-fA-F]{1,4})*)?)(?![\w.:])]]

local function valid(address)
  local a, b, c, d = address:match('^(%d+)%.(%d+)%.(%d+)%.(%d+)$')
  if a then return math.max(tonumber(a), tonumber(b), tonumber(c), tonumber(d)) <= 255 end
  return select(2, address:gsub('%x+', '')) >= 2  -- :: alone and ::1 say nothing
end

-- The names of an address: host asks the DNS and tells all of them, getent goes the way the system resolves
local function lookup(address)
  if turbojedi.which('host') then return turbojedi.output { 'host', address } end
  local text, how, code = turbojedi.capture { 'getent', 'hosts', address }
  turbojedi.message(((how == 'exit') and (code == 0)) and text:gsub('%s+$', '') or (address .. ' has no name'))
end

-- Title, the programs it can do with, any of them, the action; only the ones whose program is there are offered
local ACTIONS =
{
  { 'Route',      'ip',        function(address) turbojedi.output { 'ip', 'route', 'get', address } end },
  { 'Lookup',     { 'host', 'getent' }, lookup },
  { 'Whois',      'whois',     function(address) turbojedi.output { 'whois', address } end },
  { 'Ping',       'ping',      function(address) turbojedi.output { 'ping', '-c', '4', '-W', '2', address } end },
  { 'Trace path', 'tracepath', function(address) turbojedi.output { 'tracepath', '-n', address } end },
  { 'Copy',       nil,         turbojedi.copy }
}

local titles  = { }
local actions = { }

local function present(programs)
  for _, program in ipairs((type(programs) == 'table') and programs or { programs }) do
    if turbojedi.which(program) then return true end
  end
  return programs == nil
end

for _, action in ipairs(ACTIONS) do
  if present(action[2]) then
    titles[#titles + 1]   = action[1]
    actions[#actions + 1] = action[3]
  end
end

turbojedi.log('for an address: ' .. table.concat(titles, ', ') .. ', the ones whose programs are installed')

local function choose(address)
  local _, index = turbojedi.choose('Address ' .. address, titles)
  if index then actions[index](address) end
end

turbojedi.registerHandler(function(entry, menu)
  local seen = { }
  for found in turbojedi.matches(entry.MESSAGE, ADDRESS) do
    local address = found.address
    if (not seen[address]) and valid(address) then
      seen[address] = true
      menu:add('Address ' .. address, choose, address)
    end
  end
end)
