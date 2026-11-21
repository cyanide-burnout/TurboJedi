-- TurboJedi, Turbo Journald Interface
-- https://github.com/cyanide-burnout/TurboJedi
-- Artem Prilutskiy, 2026

-- Arrakis: entries explained by a language model behind an HTTP API of the format of OpenAI (llama-server, Ollama,
-- vLLM, colibri, OpenAI) or of Anthropic. On Arrakis they mine the spice, here the sense of the journal. Actions explains
-- the entry under the cursor or the marked ones; the Plugins menu explains the errors among what the window shows
-- without marking them, and tells the settings in effect and asks the service whether it answers.
--
-- Nothing happens without a file of settings: /etc/turbojedi/arrakis.conf of root for every user, then
-- ~/.config/turbojedi/arrakis.conf of the user over it; run by sudo, the one of the user of sudo over both, settings and
-- not code, which the user could send anywhere from the screen anyway:
--
--   url      = http://127.0.0.1:8080/v1/chat/completions
--   model    = qwen2.5-1.5b-instruct
--   format   = openai
--   key      = ...
--   language = ru
--   budget   = 12000
--   timeout  = 600
--   tokens   = 2048
--   prompt   = /etc/turbojedi/arrakis.prompt
--   record   = /var/tmp/arrakis.jsonl
--
-- format is openai or anthropic, which /v1/messages in the url tells as well; key a token for a service that wants one,
-- chmod 600 the file then; language of the answers, the one of the locale without it; budget characters of entries a
-- request takes at most, 2000 at least; timeout seconds a request may take, 0 for no limit; tokens of an answer at most, the service
-- decides without it (2048 for Anthropic, which wants it said). prompt is a file of instructions instead of the ones
-- built in, {language} there the language of the answers: a model taught on instructions of its own gets them. record
-- is a file every answer goes to as a line of JSON with its request, for teaching a model of one's own on them: the
-- entries are in it, so it is made 0600 and written only when it is a file of the process alone. Under sudo, prompt and
-- record of the file of the user of sudo are left out: root reads and writes no path a user names. A value goes to the
-- end of its line, a comment is a line of its own.
--
-- Several engines, a large model of a service and a small one of one's own: [name] starts the lines of one, the lines
-- before the first [name] are of every engine, and a section of the same name in a file after is over the one before,
-- key by key. Of several, the first request of a run asks which one, the one chosen the last time offered first (kept in
-- ~/.config/turbojedi/arrakis-chosen.txt), and Choose the model of the Plugins menu changes it; a job keeps the engine it
-- has started with, its questions go there.
--
-- An answer comes with a line for a question under it, and Enter on an answer or a question in the lines of its watch
-- brings it back: the question goes with the whole conversation of its job, the entries, the answers and the questions
-- before it; a job starts on its own, nothing of the others goes along. The record keeps the first
-- request of a job and its answer only.
--
-- An address of another machine is asked about before the first request goes there, the entries leave the machine then;
-- a yes is kept in ~/.config/turbojedi/arrakis-trusted.txt, under sudo of the user of sudo, and holds while the url stays.
-- JSON is lua-cjson of OpenResty, built into TurboJedi.
--
--   settings.lua            arrakis.conf, the engine chosen, the machines said yes to
--   request.lua             the request of the format of the service and the text of its answer
--   job.lua                 a job in a watch of its own and the questions about its answer
--   menu.lua                Actions and the Plugins menu
--   bookmarks.lua           Ask Arrakis about them in a list of bookmarks
--   prompts/instructions.txt  the instructions built in, {language} the language of the answers
--   prompts/bookmarks.txt     what the model is asked of a list of bookmarks

local plugin   = ...
local settings = require(plugin .. '.settings')

-- Off, Arrakis offers nothing: a failure goes to the problems of the start, no file is told in the console
if settings.failure then
  error(settings.failure, 0)
end

if not settings.engines then
  return
end

local menu = require(plugin .. '.menu')

-- The model first when there are several, then what it does, then what is in effect
turbojedi.on('actions', menu.actions)

if #settings.engines > 1 then
  turbojedi.on('menu', 'Choose the model', function() settings.chosen(true) end)
end

turbojedi.on('menu', 'Explain the errors of the window', menu.errors)
turbojedi.on('menu', 'Status', menu.status)

-- An item of a list of bookmarks, Bookmarks.items of the globals the plugins share
require(plugin .. '.bookmarks')
