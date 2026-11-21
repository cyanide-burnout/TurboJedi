-- TurboJedi, Turbo Journald Interface
-- https://github.com/cyanide-burnout/TurboJedi
-- Artem Prilutskiy, 2026

-- The copies of the entries a bookmark keeps: every field as JSON, cut past a limit, so that a bookmark is there after
-- the journal has rotated

local cjson = require('cjson')

local M = { }

M.BOOKMARK = 1024 * 1024   -- Bytes of copies a bookmark is kept up to, the newest entries of a set first

local FIELD    = 64 * 1024     -- Bytes a field is kept up to, cut beyond it with the bytes it has had
local SMALLEST = 256           -- Bytes a field of an entry larger than BOOKMARK is cut down to at most
local MAIN     = { '__CURSOR', '__REALTIME_TIMESTAMP', 'MESSAGE', 'PRIORITY', 'SYSLOG_IDENTIFIER', '_SYSTEMD_UNIT', '_HOSTNAME',
                   '_PID', '_COMM', '_BOOT_ID' }  -- The fields kept of an entry too large even then

local ALPHABET = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/'

local function base64(data)
  local parts = { }

  for index = 1, #data, 3 do
    local a, b, c = data:byte(index, index + 2)
    local number  = a * 65536 + (b or 0) * 256 + (c or 0)
    local four    = { }

    for place = 1, 4 do
      local digit = (number >> (6 * (4 - place))) & 63
      four[place] = ALPHABET:sub(digit + 1, digit + 1)
    end

    if not b then four[3] = '=' end
    if not c then four[4] = '=' end
    parts[#parts + 1] = table.concat(four)
  end

  return table.concat(parts)
end

-- A field as it is kept: its text, { base64 } when it is not text of UTF-8, and cut past the limit, FIELD by default,
-- with the bytes it has had
function M.keep(value, most)
  if type(value) ~= 'string' then
    return value
  end

  local size = #value

  most = most or FIELD

  if utf8.len(value) then
    if size <= most then
      return value
    end

    -- Cut by whole characters
    local text = value:sub(1, most)
    while not utf8.len(text) do text = text:sub(1, -2) end
    return { text = text, cut = size }
  end

  local limit = (most // 4) * 3

  if size <= limit then
    return { base64 = base64(value) }
  end

  return { base64 = base64(value:sub(1, limit)), cut = size }
end

local function fields(entry, most, only)
  local kept = { }

  for name, value in pairs(entry) do
    if (not only) or only[name] then
      if type(value) == 'table' then
        -- A field the entry has more than once
        local list = { }
        for index, item in ipairs(value) do list[index] = M.keep(item, most) end
        kept[name] = list
      else
        kept[name] = M.keep(value, most)
      end
    end
  end

  return kept
end

-- The copy of an entry within BOOKMARK however large it is: its fields cut shorter, by halves down to SMALLEST; an entry
-- of so many fields that this is not enough keeps its main ones, and says how many are left out
function M.copy(entry)
  local most = FIELD
  local json = cjson.encode(fields(entry))

  while (#json > M.BOOKMARK) and (most > SMALLEST) do
    most = math.max(SMALLEST, most // 2)
    json = cjson.encode(fields(entry, most))
  end

  if #json > M.BOOKMARK then
    -- The main fields, a field there many times by its first value: some hundreds of bytes each, whatever the entry
    local only, count, kept = { }, 0, { }
    for _, name in ipairs(MAIN) do only[name] = true end

    for name, value in pairs(entry) do
      if not only[name] then
        count = count + ((type(value) == 'table') and #value or 1)
      elseif type(value) == 'table' then
        count = count + #value - 1
        kept[name] = M.keep(value[1], SMALLEST)
      else
        kept[name] = M.keep(value, SMALLEST)
      end
    end

    kept.__LEFT_OUT = count
    json = cjson.encode(kept)
  end

  return json
end

-- The text of a field as the user sees it, cut ones told so
function M.told(value)
  if type(value) ~= 'table' then
    return tostring(value)
  end

  if value.base64 then
    return ('(binary, base64) %s%s'):format(value.base64, value.cut and (' … (cut, %d bytes)'):format(value.cut) or '')
  end

  if value.text then
    return value.text .. (value.cut and (' … (cut, %d bytes)'):format(value.cut) or '')
  end

  local list = { }
  for index, item in ipairs(value) do list[index] = M.told(item) end
  return table.concat(list, ' | ')
end

return M
