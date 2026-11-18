-- TurboJedi, Turbo Journald Interface
-- https://github.com/cyanide-burnout/TurboJedi
-- Artem Prilutskiy, 2026

-- Sends an entry, or the marked ones, to a chat through its webhook: Slack, Mattermost, Rocket.Chat, Discord, Telegram
-- or ntfy. An example to link on purpose: the entries leave the machine. The chat is told by ~/.config/turbojedi/chat.conf
-- of the user who runs TurboJedi, without it the plugin adds nothing:
--
--   url    = https://chat.example.com/hooks/xxxxxxxx
--   format = slack
--
-- format is slack (Mattermost and Rocket.Chat take the same), discord, telegram or ntfy. Telegram takes the address of
-- its bot, https://api.telegram.org/bot<token>/sendMessage, and chat, the id of the chat; token gives a header
-- Authorization: Bearer for ntfy or any hook behind one. The file holds a secret: chmod 600

local FORMATS = { slack = true, discord = true, telegram = true, ntfy = true }
local LIMIT   = 3500  -- Characters of a message, Telegram takes 4096, Discord 2000 in content

-- The settings, nil when there are none or they are wrong
local function load()
  local home = os.getenv('XDG_CONFIG_HOME') or ((os.getenv('HOME') or '') .. '/.config')
  local file = io.open(home .. '/turbojedi/chat.conf')
  if not file then return nil end

  local settings = { format = 'slack' }
  for line in file:lines() do
    local key, value = line:match('^%s*([%w_]+)%s*=%s*(.-)%s*$')
    if key then settings[key] = value end
  end
  file:close()

  if (not settings.url) or (not FORMATS[settings.format]) or ((settings.format == 'telegram') and (not settings.chat)) then
    error(home .. '/turbojedi/chat.conf needs url, a format of slack, discord, telegram or ntfy, and chat for telegram')
  end

  return settings
end

local settings = load()

if not settings then
  turbojedi.log('no chat.conf: nothing to offer')
  return
end

turbojedi.log('entries sent to ' .. settings.format .. ' at ' .. (settings.url:match('^https?://[^/]+') or settings.url))

-- A field the journal has several times is a list, the first value tells enough here
local function first(value)
  if type(value) == 'table' then return value[1] end
  return value
end

-- One line of an entry as journalctl shows it
local function line(item)
  local stamp = item.__REALTIME_TIMESTAMP
  local name  = first(item.SYSLOG_IDENTIFIER) or first(item._COMM) or '?'
  local pid   = first(item._PID)

  return ('%s %s %s%s: %s'):format(stamp and (turbojedi.date or os.date)('%Y-%m-%d %H:%M:%S', stamp // 1000000) or '?',
    first(item._HOSTNAME) or '?', name, pid and ('[' .. pid .. ']') or '', first(item.MESSAGE) or '')
end

-- A string of JSON: quotes, backslashes and control characters escaped, bytes that are not UTF-8 become ?
local ESCAPES = { ['"'] = '\\"', ['\\'] = '\\\\', ['\n'] = '\\n', ['\r'] = '\\r', ['\t'] = '\\t' }

local function json(text)
  if not utf8.len(text) then text = text:gsub('[\128-\255]', '?') end
  return '"' .. text:gsub('[%c"\\]', function(character)
    return ESCAPES[character] or ('\\u%04x'):format(character:byte())
  end) .. '"'
end

-- The request of the chat for a text
local function request(text)
  local headers = { }
  local body

  if settings.token then headers.Authorization = 'Bearer ' .. settings.token end

  if settings.format == 'ntfy' then
    headers['Content-Type'] = 'text/plain; charset=utf-8'
    body = text
  else
    headers['Content-Type'] = 'application/json'
    if settings.format == 'telegram' then body = ('{"chat_id": %s, "text": %s}'):format(json(settings.chat), json(text))
    elseif settings.format == 'discord' then body = ('{"content": %s}'):format(json(text))
    else body = ('{"text": %s}'):format(json(text)) end
  end

  return { url = settings.url, method = 'POST', headers = headers, body = body, timeout = 15 }
end

-- Nothing waits for the chat: the answer comes to the callback along with what has been sent, only a failure is told
local function told(code, answer, more, what)
  if not code then
    turbojedi.message(('%s not sent to the chat: %s, %s'):format(what, answer, more))
  elseif (code < 200) or (code > 299) then
    turbojedi.message(('%s not sent, the chat has answered %d: %s'):format(what, code, answer:sub(1, 200)))
  end
end

local function send(text, what)
  if (utf8.len(text) or #text) > LIMIT then
    -- Cut between the characters
    text = text:sub(1, (utf8.len(text) and utf8.offset(text, LIMIT + 1) or (LIMIT + 1)) - 1) .. '\n...'
  end

  turbojedi.fetch(request(text), told, what)
end

-- The line of the entry, to be added to or changed before it goes
local function entry(text)
  text = turbojedi.input('Send to the chat', text)
  if text and text:find('%S') then send(text, 'The entry') end
end

-- Lines of the marked entries, as a block of code where the chat shows one
local function marked(items)
  local lines = { }
  for index, item in ipairs(items) do lines[index] = line(item) end

  local text = table.concat(lines, '\n')
  if (settings.format == 'slack') or (settings.format == 'discord') then text = '```\n' .. text .. '\n```' end

  if turbojedi.confirm(('Send %d entries to the chat?'):format(#items)) then send(text, #items .. ' entries') end
end

-- The items of the entry in Actions
local function actions(item, menu, context)
  if context.marked then
    menu:add(('Send %d marked to chat'):format(#context.marked), marked, context.marked)
  else
    menu:add('Send to chat', entry, line(item))
  end
end

turbojedi.on('actions', actions)
