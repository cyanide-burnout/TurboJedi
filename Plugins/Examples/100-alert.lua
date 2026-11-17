-- TurboJedi, Turbo Journald Interface
-- https://github.com/cyanide-burnout/TurboJedi
-- Artem Prilutskiy, 2026

-- Alerts while TurboJedi is open: a watch for every rule of ~/.config/turbojedi/alert.conf, its count and last time in
-- the status of the watch, the entries in its lines, F12 shows them. Without the file the plugin does nothing. Only while
-- TurboJedi runs: it is not a monitoring of the server
--
--   [nginx restarts]
--   unit     = nginx.service
--   text     = Started
--
--   [disk errors]
--   identifier = kernel
--   text       = I/O error|EXT4-fs error
--   regex      = yes
--   raise      = yes
--
-- A rule takes unit, identifier, priority (up to a level: err, warning...), match = FIELD=VALUE (several times), text,
-- regex = yes and case = yes for the text, and raise = yes to bring the list up with a bell for every entry

local LEVELS = { emerg = true, alert = true, crit = true, err = true, warning = true, notice = true, info = true, debug = true }

-- The rules in the order of the file
local function load()
  local home = os.getenv('XDG_CONFIG_HOME') or ((os.getenv('HOME') or '') .. '/.config')
  local path = home .. '/turbojedi/alert.conf'
  local file = io.open(path)
  if not file then return nil end

  local rules = { }
  local rule
  local number = 0

  for raw in file:lines() do
    local line = raw:match('^%s*(.-)%s*$')
    number = number + 1

    if (line ~= '') and not line:match('^[#;]') then
      local name = line:match('^%[(.+)%]$')
      local key, value = line:match('^([%w_]+)%s*=%s*(.-)$')

      if name then
        rule = { name = name, matches = { } }
        rules[#rules + 1] = rule
      elseif not (key and rule) then
        error(('%s:%d: neither [rule] nor key = value in a rule'):format(path, number))
      elseif key == 'unit' then
        rule.matches._SYSTEMD_UNIT = value
      elseif key == 'identifier' then
        rule.matches.SYSLOG_IDENTIFIER = value
      elseif key == 'match' then
        rule.matches[#rule.matches + 1] = value
      elseif (key == 'priority') and LEVELS[value] then
        rule.priority = value
      elseif (key == 'text') or (key == 'regex') or (key == 'case') or (key == 'raise') then
        rule[key] = (key == 'text') and value or (value == 'yes')
      else
        error(('%s:%d: unknown key %s'):format(path, number, key))
      end
    end
  end

  file:close()
  return rules
end

local rules = load()

if not rules then
  turbojedi.log('no alert.conf: no watches')
  return
end

turbojedi.log(#rules .. ((#rules == 1) and ' rule' or ' rules') .. ' of alert.conf')

-- Times in the zone the user has chosen for the watches, UTC or local; os.date() before TurboJedi had date()
local date = turbojedi.date or os.date

local function clock(microseconds)
  return date('%H:%M:%S', microseconds // 1000000)
end

local failures = { }

for _, rule in ipairs(rules) do
  local count = 0

  local watch = turbojedi.watch({ title = rule.name })

  local followed, failure = watch:follow(
  {
    matches  = rule.matches,
    priority = rule.priority,
    text     = rule.text,
    regex    = rule.regex,
    case     = rule.case,

    -- The journal has gone too fast for the plugin, or has lost a file under it: said in the lines, as nothing came
    skipped  = function(gap, watch)
      watch:add(('entries between %s and %s are not seen%s'):format(clock(gap.since), clock(gap.till), gap.uncertain and ', maybe none' or ''))
    end
  },
  function(entry, watch)
    count = count + 1
    watch:status(('%d time%s, last at %s'):format(count, (count == 1) and '' or 's', clock(entry.__REALTIME_TIMESTAMP)))
    watch:add(tostring(entry.MESSAGE), entry, { raise = rule.raise })
  end)

  if not followed then
    -- The journal cannot be read, the other rules go on
    watch:remove()
    failures[#failures + 1] = rule.name .. ': ' .. failure
  end
end

if #failures > 0 then
  error('some rules do not watch: ' .. table.concat(failures, '; '))
end
