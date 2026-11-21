-- TurboJedi, Turbo Journald Interface
-- https://github.com/cyanide-burnout/TurboJedi
-- Artem Prilutskiy, 2026

-- The settings of Arrakis: the engines of arrakis.conf, the one chosen of several, and the addresses of other machines
-- the user has said yes to. Read once the module is required: engines, or nil when nothing is set up, and failure when
-- the settings cannot be taken

local M = { }

M.SYSTEM = '/etc/turbojedi/arrakis.conf'

local BUDGET = 2000  -- Characters of entries a request takes at least, whatever arrakis.conf says

-- A small model follows the name of a language, not its code
local LANGUAGES = { en = 'English', ru = 'Russian', uk = 'Ukrainian', de = 'German', fr = 'French', es = 'Spanish',
                    it = 'Italian', pt = 'Portuguese', pl = 'Polish', nl = 'Dutch', cs = 'Czech', sv = 'Swedish',
                    tr = 'Turkish', ja = 'Japanese', zh = 'Chinese', ko = 'Korean', he = 'Hebrew', ar = 'Arabic' }

-- The settings of a file over the ones there are: the lines before a [name] for every engine, the ones after it of the
-- engine of that name; false when there is no file
local function read(path, found)
  local file = io.open(path)
  if not file then return false end

  local into = found.common

  for line in file:lines() do
    local name = line:match('^%s*%[%s*(.-)%s*%]%s*$')

    if name then
      if not found.sections[name] then
        found.sections[name] = { }
        found.order[#found.order + 1] = name
      end
      into = found.sections[name]
    else
      local key, value = line:match('^%s*([%w_]+)%s*=%s*(.-)%s*$')
      if key then into[key] = value end
    end
  end

  file:close()
  return true
end

-- One engine: the settings of every engine with the ones of its section over them, checked; name is nil without sections
local function engine(common, section, name, files, user)
  local settings = { }

  for key, value in pairs(common) do settings[key] = value end
  for key, value in pairs(section or { }) do settings[key] = value end

  settings.name  = name
  settings.files = files

  settings.format  = settings.format or (settings.url and settings.url:match('/v1/messages$') and 'anthropic') or 'openai'
  -- Room for the lines that tell what a request is made of and for a few entries, the bookmarks for their oldest and
  -- newest and how many there are over time: a smaller budget is taken as the least
  settings.budget  = math.max(BUDGET, tonumber(settings.budget) or 12000)
  settings.timeout = tonumber(settings.timeout) or 600
  settings.tokens  = tonumber(settings.tokens)

  if settings.prompt then
    -- Instructions of one's own, the ones a model has been taught on
    local file = io.open(settings.prompt)
    if not file then error(('prompt %s of %s cannot be read'):format(settings.prompt, files)) end
    settings.instructions = file:read('a'):gsub('%s+$', '')
    file:close()
  end

  if (not settings.url) or (not settings.url:match('^https?://')) or
     ((settings.format ~= 'openai') and (settings.format ~= 'anthropic')) or
     ((settings.format == 'anthropic') and (not settings.model)) then
    error(('%s or %s%s needs url of http or https, format of openai or anthropic, and model for anthropic'):format(M.SYSTEM,
      user, name and (', [' .. name .. '] there,') or ''))
  end

  -- The language of the answers, of the locale when not said: ru of ru_RU.UTF-8, en of C.UTF-8 and POSIX
  local locale = os.getenv('LC_ALL') or os.getenv('LC_MESSAGES') or os.getenv('LANG') or ''
  settings.language = settings.language or locale:match('^(%l%l%l?)_') or locale:match('^(%l%l%l?)$') or 'en'

  -- The host of the address, and whether it is this machine: another one is asked about before the first request
  settings.host    = settings.url:match('^https?://%[([^%]]+)%]') or settings.url:match('^https?://([^/:]+)') or '?'
  settings.trusted = (settings.host == 'localhost') or (settings.host == '::1') or
                     (settings.host:match('^127%.%d+%.%d+%.%d+$') ~= nil)

  return settings
end

-- The engines of the files, one without sections, or nil when there is no file
local function load()
  local home  = os.getenv('XDG_CONFIG_HOME') or ((os.getenv('HOME') or '') .. '/.config')
  local user  = home .. '/turbojedi/arrakis.conf'
  local found = { common = { }, sections = { }, order = { } }
  local sudo  = turbojedi.sudo and turbojedi.sudo()
  local paths = { M.SYSTEM, user }
  local used  = { }

  if sudo then
    -- The user who has run sudo, not root, has set up the engine
    user = sudo.home .. '/.config/turbojedi/arrakis.conf'
    paths[#paths + 1] = user
  end

  for index, path in ipairs(paths) do
    local values = { common = { }, sections = { }, order = { } }

    if read(path, values) then
      used[#used + 1] = path

      -- A section of the same name in a file after is over the one before, key by key, as the lines of every engine
      local function over(into, from)
        for key, value in pairs(from) do
          if sudo and (index == 3) and ((key == 'prompt') or (key == 'record')) then
            -- Root reads and writes no path a user names: /etc/turbojedi/arrakis.conf or the one of root tell them
            turbojedi.log(('%s of %s is left out under sudo: a path for root goes to %s'):format(key, path, M.SYSTEM), { problem = true })
          else
            into[key] = value
          end
        end
      end

      over(found.common, values.common)

      for _, name in ipairs(values.order) do
        if not found.sections[name] then
          found.sections[name] = { }
          found.order[#found.order + 1] = name
        end
        over(found.sections[name], values.sections[name])
      end
    end
  end

  if #used == 0 then
    -- Not set up: nothing is offered, nothing is sent anywhere
    turbojedi.log('no ' .. table.concat(paths, ', ') .. ': nothing to offer, nothing sent anywhere')
    return nil
  end

  local files   = table.concat(used, ', ')
  local engines = { }

  if #found.order == 0 then
    engines[1] = engine(found.common, nil, nil, files, user)
  end

  for _, name in ipairs(found.order) do
    engines[#engines + 1] = engine(found.common, found.sections[name], name, files, user)
  end

  return engines
end

-- Off without settings, or with settings that cannot be taken
local loaded, engines = pcall(load)

M.failure = (not loaded) and engines or nil
M.engines = (not M.failure) and engines or nil

-- An address another than this machine a user has said yes to is kept in arrakis-trusted.txt among the settings of
-- TurboJedi, a url a line, until it changes; under sudo the file of the user of sudo, whose arrakis.conf tells the url.
-- The engine chosen of several is kept in arrakis-chosen.txt, the first one offered the next time
local TRUSTED = 'arrakis-trusted.txt'
local CHOSEN  = 'arrakis-chosen.txt'

for _, settings in ipairs(M.engines or { }) do
  if not settings.trusted then
    for line in (turbojedi.settings(TRUSTED) or ''):gmatch('[^\n]+') do
      if line:match('^%s*(.-)%s*$') == settings.url then settings.trusted = true end
    end
  end

  turbojedi.log(('%s: %s%s at %s, model %s, answers in %s%s%s%s'):format(settings.files,
    settings.name and (settings.name .. ', ') or '', settings.format, settings.url,
    settings.model or 'of the server', settings.language, settings.prompt and (', instructions of ' .. settings.prompt) or '',
    settings.record and (', answers recorded to ' .. settings.record) or '',
    settings.trusted and '' or ', another machine: asked before the first request'))
end

-- The engine of the requests: of several, the first request of a run asks which one, and Choose the model of the
-- Plugins menu asks again; nil when the choice is cancelled
local current

function M.chosen(again)
  local engines = M.engines

  if #engines == 1 then return engines[1] end
  if current and not again then return current end

  local last    = current and current.name or (turbojedi.settings(CHOSEN) or ''):match('^%s*(.-)%s*$')
  local names   = { }
  local initial = 1

  for index, settings in ipairs(engines) do
    names[index] = ('%s: %s at %s'):format(settings.name, settings.model or 'the model of the server', settings.host)
    if settings.name == last then initial = index end
  end

  local _, index = turbojedi.choose('Arrakis: the model', names, initial)
  if not index then return nil end

  current = engines[index]

  if current.name ~= last then
    local kept, reason = turbojedi.settings(CHOSEN, current.name .. '\n')
    if not kept then turbojedi.log(('the choice is not kept: %s'):format(tostring(reason)), { problem = true }) end
  end

  return current
end

-- Whether the entries may go to the machine of the engine: this one, one said yes to before, or one the user says yes
-- to now, not asked again for this url
function M.trust(settings)
  if settings.trusted then return true end
  if not turbojedi.confirm(('The entries go to %s, another machine. Send them?'):format(settings.host)) then return false end

  settings.trusted = true

  -- A file that cannot be written leaves it to this run
  local kept, reason = turbojedi.settings(TRUSTED, (turbojedi.settings(TRUSTED) or '') .. settings.url .. '\n')
  if not kept then turbojedi.log(('the answer is not kept: %s'):format(tostring(reason)), { problem = true }) end
  return true
end

-- The name of the language of the answers
function M.language(settings)
  return LANGUAGES[settings.language] or settings.language
end

return M
