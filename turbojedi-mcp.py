#!/usr/bin/python3
# TurboJedi, Turbo Journald Interface
# https://github.com/cyanide-burnout/TurboJedi
# Artem Prilutskiy, 2026

# A server of the Model Context Protocol over stdio that gives an AI agent the workspace of TurboJedi: the instances
# running on this machine, their windows, the entry under the cursor, the marked entries, queries of the journal, the
# watches of the plugins. It speaks to TurboJedi on the system bus, see DBUS.md, and is started by the agent, on another
# machine as `ssh server turbojedi-mcp.py`, see MCP.md. The agent points, the user acts: nothing here changes the
# system, only what the user sees.

import base64
import datetime
import json
import os
import sys
import traceback

VERSION = 'unknown'  # Of TurboJedi, set when installed

PROTOCOLS = ('2025-11-25', '2025-06-18', '2025-03-26', '2024-11-05')  # The newest first
STRUCTURED = ('2025-11-25', '2025-06-18')                              # Versions that know structuredContent

PREFIX = 'me.burnaway.TurboJedi.'
PATH = '/me/burnaway/TurboJedi'
INTERFACE = 'me.burnaway.TurboJedi'
TIMEOUT = 40  # Seconds a call is waited for, longer than the 30 TurboJedi gives it
POLICIES = ('/usr/share/dbus-1/system.d/me.burnaway.TurboJedi.conf', '/etc/dbus-1/system.d/me.burnaway.TurboJedi.conf')

INSTRUCTIONS = '''TurboJedi is a terminal viewer of the systemd journal that a user has open on this machine, in one or more
instances. These tools read what the user has in front of them: windows of the journal with their filters, the entry
under the cursor, the entries the user has marked, the watches plugins keep of the journal and their lines. They also
query the journal the way a window reads it.

The agent points, the user acts. The screen tools mark entries, put the cursor on an entry, take a window to a moment or
open a new window; the user sees each of them in the status line of TurboJedi. Nothing changes the system: actions of
the plugins and commands are never run from here, list_actions only names them for the user to run. The filters of the
user's windows are not changed either: open a window of your own with open_window.

Start with list_instances. Windows are named by window_id from list_windows, watches by watch_id from list_watches;
entries by their __CURSOR. Times are microseconds since the epoch.'''


class Failure(Exception):
    '''A tool fails: told to the agent as the result of the tool, not as an error of the protocol'''


# D-Bus

dbus = None


def connect():
    global dbus

    if dbus is None:
        try:
            import dbus as module
        except ImportError:
            raise Failure('dbus-python is not installed on this machine: python3-dbus of Debian, Ubuntu and Fedora, '
                          'python3-dbus-python of openSUSE, python-dbus of Arch')
        dbus = module

    try:
        return dbus.SystemBus()
    except dbus.exceptions.DBusException as error:
        raise Failure('the system bus cannot be reached: %s' % error.get_dbus_message())


def plain(value):
    '''A value of dbus-python as JSON takes it: binary data as {"base64": ...}'''
    if isinstance(value, (bytes, bytearray)):
        return {'base64': base64.b64encode(bytes(value)).decode('ascii')}
    if isinstance(value, dbus.Boolean):
        return bool(value)
    if isinstance(value, (int, float)):
        return int(value) if isinstance(value, int) else float(value)
    if isinstance(value, str):
        return str(value)
    if isinstance(value, dict):
        return dict((str(key), plain(item)) for key, item in value.items())
    if isinstance(value, (list, tuple)):
        return [plain(item) for item in value]
    return value


def names(bus):
    '''Names of the instances by their PID'''
    found = {}

    for name in bus.list_names():
        if name.startswith(PREFIX):
            try:
                found[int(name.rsplit('.P', 1)[1])] = str(name)
            except (IndexError, ValueError):
                pass

    return found


def call(bus, name, method, *arguments):
    try:
        interface = dbus.Interface(bus.get_object(name, PATH, introspect=False), INTERFACE)
        return getattr(interface, method)(*arguments, timeout=TIMEOUT, byte_arrays=True)
    except dbus.exceptions.DBusException as error:
        kind = (error.get_dbus_name() or '').rsplit('.', 1)[-1]
        text = error.get_dbus_message() or str(error)

        if kind == 'ServiceUnknown':
            raise Failure('the instance has exited')
        if kind == 'NoReply':
            raise Failure('TurboJedi has not answered in %d seconds' % TIMEOUT)

        raise Failure('%s: %s' % (kind, text) if kind else text)


def missing():
    '''Why no instance is seen'''
    policy = [path for path in POLICIES if os.path.exists(path)]

    if not policy:
        return ('no instance of TurboJedi is on the system bus: the D-Bus policy is not installed, neither %s; '
                'TurboJedi takes no name without it' % ' nor '.join(POLICIES))

    return 'no instance of TurboJedi is on the system bus: TurboJedi is not running on this machine'


def instance(bus, arguments):
    '''The name of the instance the arguments name by pid, the only one there is without it'''
    found = names(bus)
    pid = arguments.get('pid')

    if pid is not None:
        if pid not in found:
            raise Failure('there is no instance of PID %s, list_instances tells the ones there are' % pid)
        return found[pid]

    if not found:
        raise Failure(missing())

    if len(found) > 1:
        raise Failure('there are %d instances, PIDs %s: give pid, list_instances tells which is which' %
                      (len(found), ', '.join(str(key) for key in sorted(found))))

    return list(found.values())[0]


# Arguments

def number(arguments, key, default=None, minimum=0):
    value = arguments.get(key, default)

    if value is None:
        return None
    if isinstance(value, bool) or not isinstance(value, int) or value < minimum:
        raise Failure('%s is a whole number of %d or more' % (key, minimum))

    return value


def text(arguments, key, default=None):
    value = arguments.get(key, default)

    if (value is not None) and not isinstance(value, str):
        raise Failure('%s is a string' % key)

    return value


def strings(arguments, key):
    value = arguments.get(key, [])

    if isinstance(value, str):
        value = [value]
    if not (isinstance(value, list) and all(isinstance(item, str) for item in value)):
        raise Failure('%s is a list of strings' % key)

    return value


def moment(arguments, key):
    '''Microseconds of a time: a number of them, or ISO 8601, a time without a zone in the zone of the machine'''
    value = arguments.get(key)

    if isinstance(value, int) and not isinstance(value, bool) and (value >= 0):
        return value

    if isinstance(value, str):
        try:
            stamp = datetime.datetime.fromisoformat(value.strip().replace('Z', '+00:00').replace('z', '+00:00'))
            if stamp.tzinfo is None:
                stamp = stamp.astimezone()
            return int(stamp.timestamp()) * 1000000 + stamp.microsecond
        except ValueError:
            pass

    raise Failure('%s is microseconds since the epoch or a time as 2026-11-16T03:12:00, with a zone or in the one of '
                  'the machine' % key)


def options(arguments, query):
    '''The options of Query() and OpenWindow()'''
    result = dbus.Dictionary({}, signature='sv')

    if arguments.get('window_id') is not None:
        result['id'] = dbus.UInt32(number(arguments, 'window_id'))
    if arguments.get('matches'):
        result['matches'] = dbus.Array(strings(arguments, 'matches'), signature='s')

    for key in ('text', 'priority', 'since', 'until'):
        if arguments.get(key) is not None:
            result[key] = dbus.String(text(arguments, key))

    for key in ('regex', 'exclude', 'case'):
        if arguments.get(key) is not None:
            if not isinstance(arguments[key], bool):
                raise Failure('%s is true or false' % key)
            result[key] = dbus.Boolean(arguments[key])

    if query:
        for key in ('before', 'after'):
            if arguments.get(key) is not None:
                result[key] = dbus.String(text(arguments, key))
        for key in ('limit', 'scan'):
            if arguments.get(key) is not None:
                result[key] = dbus.UInt32(number(arguments, key, minimum=1))

    return result


def pick(entries, arguments):
    '''Only the fields asked for, the cursor and the time always'''
    fields = strings(arguments, 'fields') if arguments.get('fields') else None

    if fields is None:
        return entries

    keep = set(fields) | {'__CURSOR', '__REALTIME_TIMESTAMP'}
    return [dict((key, value) for key, value in entry.items() if key in keep) for entry in entries]


def entry(value):
    return value if value else None


# Tools

def list_instances(arguments):
    bus = connect()
    found = names(bus)
    result = []
    others = 0

    if not found:
        raise Failure(missing())

    for pid in sorted(found):
        try:
            item = plain(call(bus, found[pid], 'Describe'))
            item['windows'] = [{'window_id': window['id'], 'window': window['window'], 'title': window['title']}
                               for window in plain(call(bus, found[pid], 'ListWindows'))]
            result.append(item)
        except Failure as failure:
            if 'Denied' in str(failure):
                others += 1
            else:
                result.append({'pid': pid, 'error': str(failure)})

    answer = {'instances': result}

    if others:
        # The agent comes as another user: tell it why it does not see them
        answer['others'] = others
        answer['note'] = ('%d instance%s of other users do%s not answer this one: an instance answers its own user and '
                          'root, and the user of sudo of an instance of root' %
                          (others, '' if others == 1 else 's', 'es' if others == 1 else ''))

    return answer


def describe(arguments):
    bus = connect()
    return plain(call(bus, instance(bus, arguments), 'Describe'))


def list_windows(arguments):
    bus = connect()
    windows = plain(call(bus, instance(bus, arguments), 'ListWindows'))

    for window in windows:
        # The name the other tools take
        window['window_id'] = window.pop('id')

    return {'windows': windows}


def get_cursor(arguments):
    bus = connect()
    return {'entry': entry(plain(call(bus, instance(bus, arguments), 'GetCursor', dbus.UInt32(number(arguments, 'window_id')))))}


def get_marked(arguments):
    bus = connect()
    entries, total = plain(call(bus, instance(bus, arguments), 'GetMarked', dbus.UInt32(number(arguments, 'window_id')),
                                dbus.UInt32(number(arguments, 'limit', 0))))
    return {'entries': pick(entries, arguments), 'total': total}


def get_entry(arguments):
    bus = connect()
    return {'entry': entry(plain(call(bus, instance(bus, arguments), 'GetEntry', dbus.UInt32(number(arguments, 'window_id')),
                                      dbus.String(text(arguments, 'cursor', '')))))}


def query_journal(arguments):
    bus = connect()
    name = instance(bus, arguments)
    entries, stopped = plain(call(bus, name, 'Query', options(arguments, True)))
    return {'entries': pick(entries, arguments), 'stopped': stopped}


def list_actions(arguments):
    bus = connect()
    return {'actions': plain(call(bus, instance(bus, arguments), 'ListActions', dbus.UInt32(number(arguments, 'window_id')),
                                  dbus.String(text(arguments, 'cursor', ''))))}


def list_watches(arguments):
    bus = connect()
    watches = plain(call(bus, instance(bus, arguments), 'ListWatches'))

    for watch in watches:
        watch['watch_id'] = watch.pop('id')

    return {'watches': watches}


def get_watch_lines(arguments):
    bus = connect()
    lines = plain(call(bus, instance(bus, arguments), 'GetWatchLines', dbus.UInt32(number(arguments, 'watch_id'))))
    limit = number(arguments, 'limit', 100, minimum=1)
    return {'lines': lines[:limit], 'total': len(lines)}


def mark_entries(arguments):
    bus = connect()
    marked, stopped = plain(call(bus, instance(bus, arguments), 'Mark', dbus.UInt32(number(arguments, 'window_id')),
                                 dbus.Array(strings(arguments, 'cursors'), signature='s'),
                                 dbus.Boolean(bool(arguments.get('clear', False)))))
    return {'marked': marked, 'stopped': stopped}


def go_to_entry(arguments):
    bus = connect()
    found, stopped = plain(call(bus, instance(bus, arguments), 'GoTo', dbus.UInt32(number(arguments, 'window_id')),
                                dbus.String(text(arguments, 'cursor', ''))))
    return {'found': found, 'stopped': stopped}


def seek_window(arguments):
    bus = connect()
    reached, stopped = plain(call(bus, instance(bus, arguments), 'Seek', dbus.UInt32(number(arguments, 'window_id')),
                                  dbus.UInt64(moment(arguments, 'time'))))
    return {'reached': reached, 'stopped': stopped}


def open_window(arguments):
    bus = connect()
    return {'window_id': plain(call(bus, instance(bus, arguments), 'OpenWindow', options(arguments, False)))}


# Schemas

PID = {'type': 'integer', 'description': 'PID of the instance, from list_instances; may be left out when only one runs'}
WINDOW = {'type': 'integer', 'description': 'window_id of the window, from list_windows: not the number on the screen'}
CURSOR = {'type': 'string', 'description': '__CURSOR of the entry'}
FIELDS = {'type': 'array', 'items': {'type': 'string'},
          'description': 'only these fields of each entry, such as MESSAGE, PRIORITY, _SYSTEMD_UNIT; __CURSOR and '
                         '__REALTIME_TIMESTAMP always. All fields without it'}

FILTER = {
    'window_id': {'type': 'integer', 'description': 'the journal of this window, its directory or merge of the journals; '
                                                    'the journal of the machine without it'},
    'matches': {'type': 'array', 'items': {'type': 'string'},
                'description': 'FIELD=VALUE as journalctl takes them, such as _SYSTEMD_UNIT=nginx.service or '
                               'SYSLOG_IDENTIFIER=sshd; the same field twice is OR, different fields are AND'},
    'text': {'type': 'string', 'description': 'text matched against "identifier[pid]: message"'},
    'regex': {'type': 'boolean', 'description': 'text is a regular expression of PCRE2'},
    'exclude': {'type': 'boolean', 'description': 'entries that match the text are left out'},
    'case': {'type': 'boolean', 'description': 'the case of the text matters'},
    'priority': {'type': 'string', 'description': 'up to this level: emerg, alert, crit, err, warning, notice, info, '
                                                  'debug, or 0 to 7'},
    'since': {'type': 'string', 'description': 'from this time, as journalctl --since takes it: "2026-11-16 03:00", '
                                               '"-1h", "today"'},
    'until': {'type': 'string', 'description': 'up to this time, the same way'},
}

QUERY = dict(FILTER)
QUERY.update({
    'before': {'type': 'string', 'description': 'a __CURSOR: entries older than its entry, going back'},
    'after': {'type': 'string', 'description': 'a __CURSOR: entries newer than its entry, going on'},
    'limit': {'type': 'integer', 'minimum': 1, 'description': 'entries kept, 100 by default'},
    'scan': {'type': 'integer', 'minimum': 1, 'description': 'entries looked at, 100000 by default'},
    'fields': FIELDS,
})


def schema(properties, required=()):
    result = {'type': 'object', 'properties': dict(properties), 'additionalProperties': False}
    result['properties']['pid'] = PID
    if required:
        result['required'] = list(required)
    return result


SCREEN = ' The user sees it on their screen and in the status line of TurboJedi, as "API: ...". It waits while the user ' \
         'has a dialog open, up to 30 seconds.'

TOOLS = [
    ('list_instances', list_instances, True,
     'Instances of TurboJedi running on this machine that answer this user: PID, user, host, version, start time and '
     'their windows. Start here.',
     {'type': 'object', 'properties': {}, 'additionalProperties': False}),

    ('describe', describe, True,
     'An instance of TurboJedi: pid, uid, user, host, version, started (microseconds), api, and sudo, the user an '
     'instance of root started by sudo answers as well.',
     schema({})),

    ('list_windows', list_windows, True,
     'The windows of the journal of an instance, the front one first: window_id, the number on the screen, the title, '
     'the filter (text, levels as EACRWNID with - for hidden ones, matches, the fields of Source, since, until), '
     'whether the window follows the end of the journal, and how many entries are marked or selected in it.',
     schema({})),

    ('get_cursor', get_cursor, True,
     'The entry under the cursor of a window, every field of it: what the user is looking at. null without one.',
     schema({'window_id': WINDOW}, ('window_id',))),

    ('get_marked', get_marked, True,
     'The entries the user has marked or selected in a window, the oldest first, and how many there are in all.',
     schema({'window_id': WINDOW, 'limit': {'type': 'integer', 'minimum': 0, 'description': 'entries, 1000 by default'},
             'fields': FIELDS}, ('window_id',))),

    ('get_entry', get_entry, True,
     'An entry by its __CURSOR in the journal of a window, every field of it: of a line of a watch, of an answer of a '
     'query. null when it is not there.',
     schema({'window_id': WINDOW, 'cursor': CURSOR}, ('window_id', 'cursor'))),

    ('query_journal', query_journal, True,
     'Entries of the journal, the newest first unless after is given; the journal of a window or of the machine. '
     'Every field of an entry unless fields are given; binary values as {"base64": ...}. stopped tells why the query '
     'has ended before the journal or the range: limit, scan or time (30 seconds). Up to 4 queries at once.',
     schema(QUERY)),

    ('list_actions', list_actions, True,
     'The items of the Actions menu of TurboJedi for an entry of a window, title and plugin: what the user can do with '
     'it, such as the status of its unit or a ban of its address. They are not run from here: name the one that helps '
     'and the user runs it with Alt-F10 or A on the entry.',
     schema({'window_id': WINDOW, 'cursor': CURSOR}, ('window_id', 'cursor'))),

    ('list_watches', list_watches, True,
     'Watches the plugins keep of the journal while TurboJedi runs, such as SSH attacks or alerts: watch_id, plugin, '
     'key, title, state (active, stopped, failed), the status the plugin tells, gaps, unread lines, the time of the '
     'last line.',
     schema({})),

    ('get_watch_lines', get_watch_lines, True,
     'The lines of a watch, the newest first: number, moment, text, unread, and the __CURSOR and time of its entry when '
     'it has one, for get_entry and go_to_entry.',
     schema({'watch_id': {'type': 'integer', 'description': 'watch_id from list_watches'},
             'limit': {'type': 'integer', 'minimum': 1, 'description': 'lines, 100 by default'}}, ('watch_id',))),

    ('mark_entries', mark_entries, False,
     'Marks the entries of the cursors in a window, as the user does with M, to show them what has been found. Older '
     'entries come into the window as needed. Returns how many are marked, and stopped "time" when the history has not '
     'come in to the end.' + SCREEN,
     schema({'window_id': WINDOW, 'cursors': {'type': 'array', 'items': {'type': 'string'}, 'description': '__CURSOR of the entries'},
             'clear': {'type': 'boolean', 'description': 'take the other marks off first'}}, ('window_id', 'cursors'))),

    ('go_to_entry', go_to_entry, False,
     'Puts the cursor of a window on an entry and brings the window to the front. found is false when the entry is not '
     'in the journal of the window, the cursor is near its time then.' + SCREEN,
     schema({'window_id': WINDOW, 'cursor': CURSOR}, ('window_id', 'cursor'))),

    ('seek_window', seek_window, False,
     'Takes a window to a moment, as Ctrl-G does; the window stops following the end of the journal, going back to it '
     'is up to the user. To set several windows to the same moment, seek each.' + SCREEN,
     schema({'window_id': WINDOW, 'time': {'type': ['integer', 'string'],
             'description': 'microseconds since the epoch, or ISO 8601 such as 2026-11-16T03:12:00, without a zone in '
                            'the one of the machine'}}, ('window_id', 'time'))),

    ('open_window', open_window, False,
     'Opens a new window of the journal with a filter of its own, in front, and returns its window_id. The way to show '
     'the user a selection: the filters of their windows are not changed.' + SCREEN,
     schema(FILTER)),
]

HANDLERS = dict((tool[0], tool[1]) for tool in TOOLS)


def describe_tools():
    result = []

    for name, handler, reading, description, input in TOOLS:
        result.append({
            'name': name,
            'description': description,
            'inputSchema': input,
            'annotations': {'readOnlyHint': reading, 'destructiveHint': False, 'idempotentHint': reading,
                            'openWorldHint': False},
        })

    return result


# Protocol

class Session:
    def __init__(self):
        self.protocol = PROTOCOLS[0]

    def initialize(self, params):
        asked = params.get('protocolVersion')
        self.protocol = asked if asked in PROTOCOLS else PROTOCOLS[0]
        return {
            'protocolVersion': self.protocol,
            'capabilities': {'tools': {}},
            'serverInfo': {'name': 'turbojedi', 'title': 'TurboJedi', 'version': VERSION},
            'instructions': INSTRUCTIONS,
        }

    def call(self, params):
        name = params.get('name')
        arguments = params.get('arguments') or {}

        if name not in HANDLERS:
            raise Problem(-32602, 'unknown tool %s' % name)

        try:
            if not isinstance(arguments, dict):
                raise Failure('the arguments are an object')

            if (arguments.get('pid') is not None) and (isinstance(arguments['pid'], bool) or not isinstance(arguments['pid'], int)):
                raise Failure('pid is a whole number')

            result = HANDLERS[name](arguments)
            answer = {'content': [{'type': 'text', 'text': json.dumps(result, ensure_ascii=False)}], 'isError': False}

            if self.protocol in STRUCTURED:
                answer['structuredContent'] = result

            return answer

        except Failure as failure:
            return {'content': [{'type': 'text', 'text': str(failure)}], 'isError': True}

    def handle(self, message):
        '''The answer to a message, None for a notification'''
        if not isinstance(message, dict) or (message.get('jsonrpc') != '2.0') or not isinstance(message.get('method'), str):
            return error(message.get('id') if isinstance(message, dict) else None, -32600, 'not a request of JSON-RPC 2.0')

        method = message['method']
        params = message.get('params') or {}

        if 'id' not in message:
            # Notifications: initialized, cancelled, nothing to answer
            return None

        try:
            if not isinstance(params, dict):
                raise Problem(-32602, 'params are an object')
            if method == 'initialize':
                result = self.initialize(params)
            elif method == 'ping':
                result = {}
            elif method == 'tools/list':
                result = {'tools': describe_tools()}
            elif method == 'tools/call':
                result = self.call(params)
            else:
                raise Problem(-32601, 'unknown method %s' % method)
        except Problem as problem:
            return error(message['id'], problem.code, problem.text)
        except Exception as failure:
            traceback.print_exc(file=sys.stderr)
            return error(message['id'], -32603, 'internal error: %s' % failure)

        return {'jsonrpc': '2.0', 'id': message['id'], 'result': result}


class Problem(Exception):
    def __init__(self, code, text):
        Exception.__init__(self, text)
        self.code = code
        self.text = text


def error(identity, code, text):
    return {'jsonrpc': '2.0', 'id': identity, 'error': {'code': code, 'message': text}}


def send(message):
    sys.stdout.buffer.write(json.dumps(message, ensure_ascii=False).encode('utf-8') + b'\n')
    sys.stdout.buffer.flush()


def main():
    if '--version' in sys.argv[1:]:
        print('turbojedi-mcp.py %s' % VERSION)
        return

    if sys.argv[1:]:
        sys.stderr.write('turbojedi-mcp.py is a server of the Model Context Protocol over stdio, started by an AI agent; '
                         'see MCP.md\n')
        sys.exit(2)

    session = Session()

    for line in sys.stdin.buffer:
        if not line.strip():
            continue

        try:
            message = json.loads(line.decode('utf-8'))
        except ValueError:
            send(error(None, -32700, 'not JSON'))
            continue

        answer = session.handle(message)

        if answer is not None:
            send(answer)


if __name__ == '__main__':
    main()
