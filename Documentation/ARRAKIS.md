# Arrakis

On Arrakis they mine the spice; Arrakis of TurboJedi mines the sense of the journal. Mark entries, Actions →
Explain N marked entries of arrakis, or Explain this entry, which takes along the 30 entries of its unit before it. A
language model behind an HTTP API tells what has happened, the likely cause and what to check; the answer comes up
in a window, stays in a watch "Arrakis N" of F12, and the entries it names are lines there that go to them by Enter.
The request waits for the answer in a window with the seconds so far and Cancel, the journal windows go on behind it.

Without marking anything, Explain the errors of the window of arrakis in the Plugins menu sends the errors among what
the window shows, its filter and its time, the newest when there are too many. Status of arrakis there tells the
settings in effect and the files they come from, and sends a short request to see that the service answers.

A list of bookmarks has Ask Arrakis about them, see [BOOKMARKS.md](BOOKMARKS.md): the bookmarks with their times,
topics and comments and the copies of their entries, the model asked what they have in common, whether what they have
found comes back and when, and what to look at next. A pile gathered over weeks is told at once, rotated entries
included: each bookmark with its comment and its first three entries, the newest bookmarks first as far as `budget`
goes, the older ones it has no room for told by their number.

Under the answer is a line for a question about it, what a number of an error means for one: Ask sends it with the
entries, the answer and the questions before it, as a conversation of that job alone, waits for the answer in a
window with the seconds so far and Cancel, and the answer comes the same way, with a line for the next question; Close
ends it. PgUp and PgDn scroll a long answer, Copy puts all of it to the
clipboard, a command in it whole. An answer cut at the limit of tokens says so: `tokens` of `arrakis.conf` gives more,
and a model that thinks first needs more of them. Enter or a double click on an answer or a question among the
lines of its watch in F12 brings it back; the questions and the answers are lines there as well. Every job starts on
its own, the model remembers nothing of the others.

## Settings

Nothing happens without a file: `/etc/turbojedi/arrakis.conf` of root for every user, `~/.config/turbojedi/arrakis.conf`
of the user over it. Run by `sudo turbojedi`, the file in the home of the user who has run sudo goes over both, so the
same file serves TurboJedi run as yourself and as root.

```
url      = http://127.0.0.1:8080/v1/chat/completions
model    = qwen2.5-1.5b-instruct
# openai, or anthropic, which /v1/messages tells as well
format   = openai
# a token for a service that wants one: chmod 600 the file then
key      = ...
# of the answers, the one of the locale without it
language = ru
# characters of entries a request takes, the newest kept
budget   = 12000
# seconds a request may take, 0 for no limit
timeout  = 600
# tokens of an answer at most, the service decides without it, 2048 for Anthropic;
# a model that thinks first needs several thousand
tokens   = 2048
# instructions of your own instead of the ones built in, {language} there the language of the answers
prompt   = /etc/turbojedi/arrakis.prompt
# every answer with its request as a line of JSON, to teach a model of your own on
record   = /var/tmp/arrakis.jsonl
```

A line is `key = value` to its end, a comment is a line of its own that starts with `#`. `prompt` and `record` are for a
model of your own: `record` keeps what a large model has answered on your journals, a set to teach a small one on, and
`prompt` gives that small one the instructions it has been taught with. The record holds the entries: it is made with 0600, and
written only when it is a file of the user alone, not a link. Under `sudo`, `prompt` and `record` of your own file
are left out, and the console tells so: root reads and writes no path a user names, give them in
`/etc/turbojedi/arrakis.conf` then.

### Several models

A file can name several engines, a large model of a service and a small one of your own to compare them on the same
entries: `[name]` starts the lines of one, the lines before the first `[name]` are of every engine.

```
language = ru

[claude]
url   = https://api.anthropic.com/v1/messages
model = claude-sonnet-5-5
key   = ...

[openai]
url   = https://api.openai.com/v1/chat/completions
model = gpt-5
key   = ...

[arrakis]
url    = http://127.0.0.1:8080/v1/chat/completions
budget = 12000
```

The first request of a run asks which one, the one chosen the last time offered first (it is kept in
`~/.config/turbojedi/arrakis-chosen.txt`); Choose the model of arrakis, first in the Plugins menu, changes it, and
Status shows the one chosen. A job keeps the engine it has started with, its title names it, `Arrakis 2 (claude): ...`:
the questions about an answer go where the answer has come from, so conversations with different models go on side by
side. A section of the same name in `~/.config/turbojedi/arrakis.conf` is over the one of
`/etc/turbojedi/arrakis.conf` key by key, so a user adds a model of their own next to the ones of root, or changes one
of them, without copying the file. Without a `[name]`, the file is one engine, as before.

## Engines

Any service of the API of OpenAI or of Anthropic does, on this machine or not:

| Engine | `url` |
|---|---|
| llama-server of llama.cpp | `http://127.0.0.1:8080/v1/chat/completions`; a model of 0.5–1.5B runs on the CPU of a VM of 2 GB |
| Ollama | `http://127.0.0.1:11434/v1/chat/completions` |
| colibri | `/v1/chat/completions` of `coli serve`; 8 GB of memory at least |
| vLLM, a server of your own | its `/v1/chat/completions` |
| Foundation Models of Apple, `fm serve` of macOS 26 | `http://127.0.0.1:1976/v1/chat/completions` with `model = system`, on the server over `ssh -R`; see [The model of your Mac](#the-model-of-your-mac) |
| OpenAI, Anthropic | `https://api.openai.com/v1/chat/completions`, `https://api.anthropic.com/v1/messages`, with `key` |

An address that is not of this machine is asked about before the first request: the entries leave it then. A yes is
kept in `~/.config/turbojedi/arrakis-trusted.txt`, under `sudo` the one of your home, a `url` a line, and holds while
`url` stays the same; another `url` is asked about again, and deleting the line or the file forgets it. Without a file
nothing leaves, which is what a contour of PCI DSS wants: put a local engine there or no file at all.

## The model of your Mac

macOS 26 has a language model on the device and `fm serve` that serves it with
the API of OpenAI. Forward it to the server over SSH, and TurboJedi there asks your Mac, the entries going nowhere else:

```
fm serve --port 1976                      # on the Mac, listens on 127.0.0.1
ssh -R 1976:127.0.0.1:1976 server         # the port of the Mac on 127.0.0.1 of the server
```

```
url   = http://127.0.0.1:1976/v1/chat/completions
model = system
```

The forwarded port is open to every user of the server while you are logged in: they cannot read anything through it,
but they could ask your Mac too.

JSON is lua-cjson of OpenResty, built into TurboJedi: the plugin needs nothing of the distribution.
