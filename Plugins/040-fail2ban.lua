-- TurboJedi, Turbo Journald Interface
-- https://github.com/cyanide-burnout/TurboJedi
-- Artem Prilutskiy, 2026

-- Tells whether fail2ban has banned an address of an entry, bans or unbans it. A ban of the wrong address can lock its
-- user out, the own one included: Ban asks for the jail and a confirmation, warns about the address of the own SSH
-- session and is never offered for an address of this machine

-- Only where fail2ban is installed, and for root: fail2ban-client answers nobody else, the items would only fail
if not turbojedi.which('fail2ban-client') then
  turbojedi.log('fail2ban is not installed, nothing to offer')
  return
end

-- The effective one of real, effective, saved and file system UIDs
local status = io.open('/proc/self/status')
local uid

if status then
  uid = status:read('a'):match('\nUid:%s+%d+%s+(%d+)')
  status:close()
end

if uid ~= '0' then
  turbojedi.log('not root: fail2ban-client answers root only, nothing to offer')
  return
end

turbojedi.log('status, ban and unban by fail2ban-client')

-- IPv4, or IPv6 in full or with ::, not a time such as 12:34:56
local ADDRESS = [[(?<![\w.:])(?<address>(?:\d{1,3}\.){3}\d{1,3}|(?:[0-9a-fA-F]{1,4}:){7}[0-9a-fA-F]{1,4}|(?:[0-9a-fA-F]{1,4}(?::[0-9a-fA-F]{1,4})*)?::(?:[0-9a-fA-F]{1,4}(?::[0-9a-fA-F]{1,4})*)?)(?![\w.:])]]

-- The pattern finds the form, an address is told by its values
local function valid(address)
  local a, b, c, d = address:match('^(%d+)%.(%d+)%.(%d+)%.(%d+)$')

  if a then
    return (tonumber(a) <= 255) and (tonumber(b) <= 255) and (tonumber(c) <= 255) and (tonumber(d) <= 255)
  end

  -- :: alone or ::1 is not worth a ban
  return select(2, address:gsub('%x+', '')) >= 2
end

-- An IPv6 address in 32 hex digits, so that ::1 and 0:0:0:0:0:0:0:1 are one
local function expand(address)
  local function split(text)
    local list = { }
    for group in text:gmatch('[^:]+') do list[#list + 1] = group end
    return list
  end

  local head, tail = address:match('^(.-)::(.*)$')
  local groups     = split(head or address)

  if head then
    local right = split(tail)
    for _ = 1, 8 - #groups - #right do groups[#groups + 1] = '0' end
    for _, group in ipairs(right) do groups[#groups + 1] = group end
  end

  for index, group in ipairs(groups) do groups[index] = ('0000' .. group:lower()):sub(-4) end
  return table.concat(groups)
end

-- Addresses of this machine, a ban of one of them locks the machine out of itself: "to 149.5.8.49" of sshd is one.
-- The kernel tells them, IPv4 in /proc/net/fib_trie as host LOCAL, IPv6 in /proc/net/if_inet6
local function own()
  local list = { }
  local last
  local file = io.open('/proc/net/fib_trie')

  if file then
    for line in file:lines() do
      last = line:match('|%-%- ([%d.]+)') or last
      if last and line:find('/32 host LOCAL', 1, true) then list[last] = true end
    end
    file:close()
  end

  file = io.open('/proc/net/if_inet6')

  if file then
    for line in file:lines() do
      local hex = line:match('^(%x+)')
      if hex then list[hex:lower()] = true end
    end
    file:close()
  end

  return list
end

-- Jails fail2ban has, nil when it does not answer: it needs root
local function jails()
  local how, code, text, errors = turbojedi.execute({ 'fail2ban-client', 'status' }, { mode = 'capture' })

  if (how ~= 'exit') or (code ~= 0) then
    turbojedi.message('fail2ban does not answer: ' .. (errors or (how .. ' ' .. tostring(code))))
    return nil
  end

  local list = { }

  for jail in (text:match('Jail list:%s*([^\n]*)') or ''):gmatch('[^,%s]+') do
    list[#list + 1] = jail
  end

  return list
end

-- One address in either form of IPv6
local function same(one, other)
  return (one == other) or ((one:find(':') ~= nil) and (other:find(':') ~= nil) and (expand(one) == expand(other)))
end

-- The jails an address is banned in now, by the list of each; the ones that have not answered are told apart
local function banned(address, list)
  local found  = { }
  local silent = { }

  for _, jail in ipairs(list) do
    local how, code, text = turbojedi.execute({ 'fail2ban-client', 'status', jail }, { mode = 'capture' })

    if (how ~= 'exit') or (code ~= 0) then
      silent[#silent + 1] = jail
    else
      for item in (text:match('Banned IP list:%s*([^\n]*)') or ''):gmatch('%S+') do
        if same(item, address) then
          found[#found + 1] = jail
          break
        end
      end
    end
  end

  return found, silent
end

local function check(address)
  local list = jails()

  if not list then
    return
  end

  local found, silent = banned(address, list)
  local text

  if #found > 0 then
    text = ('%s is banned in %s'):format(address, table.concat(found, ', '))
  else
    text = ('%s is not banned in any of the %d jails: %s'):format(address, #list, table.concat(list, ', '))
  end

  if #silent > 0 then
    text = text .. ('\n\nNot answered: %s'):format(table.concat(silent, ', '))
  end

  turbojedi.message(text, 'fail2ban')
end

local function ban(address, jail)
  local list = jails()

  if not list then
    return
  end

  jail = turbojedi.choose('Jail for ' .. address, list, jail)

  if not jail then
    return
  end

  local warning = ''
  local client  = (os.getenv('SSH_CLIENT') or ''):match('^(%S+)')

  if client == address then
    warning = '\n\nThis is the address of your own SSH session!'
  end

  if turbojedi.confirm(('Ban %s in jail %s?%s'):format(address, jail, warning)) then
    turbojedi.execute { 'fail2ban-client', 'set', jail, 'banip', address }
  end
end

local function unban(address)
  if turbojedi.confirm(('Unban %s in every jail?'):format(address)) then
    turbojedi.execute { 'fail2ban-client', 'unban', address }
  end
end

-- The items of the entry in Actions
local function actions(entry, menu, context)
  -- OpenSSH of 9.8 and later logs as sshd-session and sshd-auth
  local identifier = entry.SYSLOG_IDENTIFIER
  local jail       = (type(identifier) == 'string') and identifier:match('^sshd') and 'sshd' or nil
  local seen = { }
  local mine = own()

  for found in turbojedi.matches(entry.MESSAGE, ADDRESS) do
    local address = found.address

    if (not seen[address]) and valid(address) and not (mine[address] or (address:find(':') and mine[expand(address)])) then
      seen[address] = true
      menu:add('Status of ' .. address .. ' in fail2ban', check, address)
      menu:add('Ban ' .. address, ban, address, jail)
      menu:add('Unban ' .. address, unban, address)
    end
  end
end

turbojedi.on('actions', actions)
