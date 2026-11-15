-- TurboJedi, Turbo Journald Interface
-- https://github.com/cyanide-burnout/TurboJedi
-- Artem Prilutskiy, 2026

-- Bans or unbans an address of an entry in fail2ban. An example to copy into a directory of plugins on purpose:
-- a ban of the wrong address can lock its user out, the own one included

if not turbojedi.which('fail2ban-client') then
  return
end

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

-- Jails fail2ban has, nil when it does not answer: it needs root
local function jails()
  local text, how, code, errors = turbojedi.capture { 'fail2ban-client', 'status' }

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

turbojedi.registerHandler(function(entry, menu, context)
  -- OpenSSH of 9.8 and later logs as sshd-session and sshd-auth
  local identifier = entry.SYSLOG_IDENTIFIER
  local jail       = (type(identifier) == 'string') and identifier:match('^sshd') and 'sshd' or nil
  local seen = { }

  for found in turbojedi.matches(entry.MESSAGE, ADDRESS) do
    local address = found.address

    if (not seen[address]) and valid(address) then
      seen[address] = true
      menu:add('Ban ' .. address, ban, address, jail)
      menu:add('Unban ' .. address, unban, address)
    end
  end
end)
