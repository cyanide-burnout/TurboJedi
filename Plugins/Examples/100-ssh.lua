-- TurboJedi, Turbo Journald Interface
-- https://github.com/cyanide-burnout/TurboJedi
-- Artem Prilutskiy, 2026

-- Attacks on the SSH server of this machine as they come, in a watch while TurboJedi is open: an address a line with its
-- failures in the last minute and in all, the user names it has tried and the time of its last try, the busiest on top.
-- A login after failures from the same address brings the list of watches up with a bell, so does an address that tries
-- more than FLOOD times in a minute, once. It only looks: banning is the business of fail2ban. An example to link into a
-- directory of plugins on purpose: it watches and rings on every machine with sshd while TurboJedi is open

-- Only where an SSH server is installed, running or not: one started later is watched as well. sshd is in sbin, out of
-- the PATH of a user
if not (turbojedi.which('sshd') or io.open('/usr/sbin/sshd')) then
  turbojedi.log('no sshd on this machine, no watch')
  return
end

local WINDOW = 60   -- Seconds the rate is counted over
local FLOOD  = 30   -- Failures in that time that raise the list, once for an address
local QUIET  = 600  -- Seconds of silence an address leaves the list after
local NAMES  = 3    -- User names shown, the rest are counted

-- One try may come in several lines: Invalid user and Failed ... for invalid user are one, the closing lines only tell the
-- user and the time
local ATTEMPT = [=[^(?:Invalid user (?<user>\S*) from|Failed \S+ for (?!invalid user )(?<valid>\S+) from) (?<address>\S+) port]=]
local TOUCH   = [=[^(?:Connection closed by|Disconnected from|error: maximum authentication attempts exceeded for) (?:authenticating |invalid )?(?:user )?(?<user>\S*) (?:from )?(?<address>[0-9a-fA-F:.]+) port]=]
local LOGIN   = [=[^Accepted \S+ for (?<user>\S+) from (?<address>\S+) port]=]
local LOGOUT  = [=[^Disconnected from user \S+ \S+ port]=]  -- The end of a session that has logged in, not a try

-- Times in the zone the user has chosen for the watches, UTC or local; os.date() before TurboJedi had date()
local date = turbojedi.date or os.date

local sources = { }  -- By the address: tries, the times of the recent ones, the names, the last time, its line
local width   = 0    -- Of the column of the addresses: the longest one there is, 15 for IPv4 alone, up to 39 for IPv6
local watch

local function clock(seconds)
  return date('%H:%M:%S', seconds)
end

-- The tries of the window, the older ones go
local function recent(source, now)
  local kept = { }
  for _, moment in ipairs(source.times) do
    if moment > now - WINDOW then kept[#kept + 1] = moment end
  end
  source.times = kept
  return #kept
end

local function describe(address, source, now)
  local names = { }
  source.shown = recent(source, now)  -- The rate the line tells, told again once it is not the one of the time
  for index = 1, math.min(NAMES, #source.names) do names[index] = source.names[index] end
  local more = (#source.names > NAMES) and (', +' .. (#source.names - NAMES)) or ''
  return ('%-' .. width .. 's %3d/min %6d total  %d user%s: %s%s, last %s'):format(address, source.shown, source.total,
    #source.names, (#source.names == 1) and '' or 's', table.concat(names, ', '), more, clock(source.last))
end

-- How many addresses there are, the failures of the last minute and the busiest address
local function tell(now)
  local count, failures, busiest, most = 0, 0, nil, 0
  for address, source in pairs(sources) do
    local rate = recent(source, now)
    count, failures = count + 1, failures + rate
    if rate > most then busiest, most = address, rate end
  end
  watch:status(('%d source%s, %d failure%s in the last minute%s'):format(count, (count == 1) and '' or 's',
    failures, (failures == 1) and '' or 's', busiest and (', busiest ' .. busiest) or ''))
end

-- The line of an address on top, read already: the list is a state, not news
local function show(address, source, now)
  if source.line then watch:delete(source.line) end
  source.line = watch:add(describe(address, source, now), source.entry, { quiet = true })
end

-- Every line again in the order of their last tries, the latest on top
local function redraw(now)
  local order = { }
  for address in pairs(sources) do order[#order + 1] = address end
  table.sort(order, function(one, other) return sources[one].last < sources[other].last end)
  for _, address in ipairs(order) do show(address, sources[address], now) end
end

-- The column as wide as the longest address; when that changes, every line again
local function fit(now)
  local longest = 0
  for address in pairs(sources) do longest = math.max(longest, #address) end
  if longest == width then return false end

  width = longest
  redraw(now)
  return true
end

local function source(address)
  sources[address] = sources[address] or { times = { }, names = { }, known = { }, total = 0, last = 0 }
  return sources[address]
end

local function name(source, user)
  if (user ~= nil) and (user ~= '') and not source.known[user] then
    source.known[user] = true
    source.names[#source.names + 1] = user
  end
end

watch = turbojedi.watch({ title = 'SSH attacks', key = 'attacks' })

local followed = watch:follow(
{
  matches = { SYSLOG_IDENTIFIER = { 'sshd', 'sshd-session', 'sshd-auth' } },  -- OpenSSH of 9.8 and later logs as all three
  -- The text of a filter is matched against identifier[pid]: message, as a window matches it
  text    = ': (?:Invalid user|Failed|Accepted|Connection closed by|Disconnected from|error: maximum authentication)',
  regex   = true,
  skipped = function(gap, watch)
    watch:add(('entries between %s and %s are not seen%s'):format(clock(gap.since // 1000000), clock(gap.till // 1000000), gap.uncertain and ', maybe none' or ''))
  end
},
function(entry)
  local message = tostring(entry.MESSAGE)
  local now     = entry.__REALTIME_TIMESTAMP // 1000000
  local found

  found = turbojedi.regex(message, LOGIN)

  if found then
    -- A login: worth a bell only after failures of the same address
    local known = sources[found.address]
    if known and (known.total > 0) then
      watch:add(('%s logged in as %s after %d failure%s'):format(found.address, found.user, known.total, (known.total == 1) and '' or 's'),
        entry, { raise = true })
    end
    return
  end

  -- A user of this machine leaving: Disconnected from user would pass for a try of an attacker, with no failures
  if turbojedi.regex(message, LOGOUT) then return end

  local attempt = turbojedi.regex(message, ATTEMPT)
  found = attempt or turbojedi.regex(message, TOUCH)
  if not found then return end

  local item = source(found.address)
  name(item, found.user or found.valid)
  item.last, item.entry = now, entry

  if attempt then
    item.total = item.total + 1
    item.times[#item.times + 1] = now

    if (not item.flooded) and (recent(item, now) >= FLOOD) then
      item.flooded = true
      watch:add(('%s tries more than %d times a minute'):format(found.address, FLOOD), entry, { raise = true })
    end
  end

  if not fit(now) then show(found.address, item, now) end
  tell(now)
end)

if not followed then
  watch:remove()
  return
end

-- Addresses silent for long go, now and then; the rates of the lines follow the time, as the status does
turbojedi.timeout(30000, true, function()
  local now = os.time()
  local stale = false
  for address, item in pairs(sources) do
    if item.last < now - QUIET then
      watch:delete(item.line)
      sources[address] = nil
    elseif recent(item, now) ~= item.shown then
      stale = true
    end
  end
  if (not fit(now)) and stale then redraw(now) end
  tell(now)
end)
