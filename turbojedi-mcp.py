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
import re
import sys
import traceback

VERSION = 'unknown'  # Of TurboJedi, set when installed
API = 1              # Of the D-Bus interface this proxy speaks: within it fields and methods are only added

PROTOCOLS = ('2025-11-25', '2025-06-18', '2025-03-26', '2024-11-05')  # The newest first
STRUCTURED = ('2025-11-25', '2025-06-18')                             # Versions that know structuredContent

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

Plugins may offer methods for an agent, list_methods tells them: searches of what a plugin knows, such as the sessions
of a subscriber in BrandMeister, and a watch of a context for the user to follow. invoke calls one. Only the methods
that read or change the screen are there; what a plugin does to the system stays with the user.

Start with list_instances. describe tells what the user has in front: a window of the journal, a timeline, the list
of watches or the lines of one. Windows are named by window_id from list_windows, watches by watch_id from
list_watches; entries by their __CURSOR. Times are microseconds since the epoch.'''


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
    '''Names of the instances by their PID. The policy lets anyone take a name of the prefix: a name counts only when
    the bus tells that its owner has the UID and the PID the name says'''
    found = {}
    daemon = dbus.Interface(bus.get_object('org.freedesktop.DBus', '/org/freedesktop/DBus'), 'org.freedesktop.DBus')

    for name in bus.list_names():
        match = re.match(r'^me\.burnaway\.TurboJedi\.U(\d+)\.P(\d+)$', name)

        if not match:
            continue

        try:
            uid = int(daemon.GetConnectionUnixUser(name))
            pid = int(daemon.GetConnectionUnixProcessID(name))
        except dbus.exceptions.DBusException:
            # Gone meanwhile
            continue

        if (uid, pid) != (int(match.group(1)), int(match.group(2))):
            sys.stderr.write('turbojedi-mcp.py: %s belongs to UID %d, PID %d, not to the ones it names\n' % (name, uid, pid))
            continue

        found[pid] = str(name)

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
        if kind == 'UnknownMethod':
            # An instance started before TurboJedi was updated: the interface only grows within its version
            raise Failure(older('has no %s() yet' % method))

        raise Failure('%s: %s' % (kind, text) if kind else text)


def older(what, instead=None):
    '''What an instance older than this proxy cannot do, and what to do about it'''
    return 'this TurboJedi %s: it is older than turbojedi-mcp.py, restart it after the update%s' % \
           (what, ', or %s' % instead if instead else '')


def missing():
    '''Why no instance is seen'''
    policy = [path for path in POLICIES if os.path.exists(path)]

    if not policy:
        return ('no instance of TurboJedi is on the system bus: the D-Bus policy is not installed, neither %s; '
                'TurboJedi takes no name without it' % ' nor '.join(POLICIES))

    return 'no instance of TurboJedi is on the system bus: TurboJedi is not running on this machine'


def release(text):
    '''A version of git describe as numbers to compare, v0.14.1-34-g86f7b2e as (0, 14, 1, 34); None for another one'''
    match = re.match(r'^v?(\d+)\.(\d+)(?:\.(\d+))?(?:-(\d+)-g[0-9a-f]+)?(?:-dirty)?$', text or '')
    return tuple(int(part or 0) for part in match.groups()) if match else None


def annotate(item):
    '''An instance as Describe() tells it, with a note when it and this proxy do not match'''
    api = item.get('api', 0)
    version = item.get('version')

    if api > API:
        item['unsupported'] = True
        item['note'] = ('TurboJedi %s speaks version %d of its D-Bus interface, this turbojedi-mcp.py knows up to %d: '
                        'update turbojedi-mcp.py, the package of TurboJedi has it' % (version, api, API))
    elif (VERSION != 'unknown') and version and (version != VERSION):
        own = release(VERSION)
        theirs = release(version)

        if own and theirs and (theirs < own):
            # Started before the package was updated: the fields and the tools of the newer one are not there
            item['outdated'] = True
            item['note'] = ('this instance runs %s, older than turbojedi-mcp.py %s: it has been started before '
                            'TurboJedi was updated, some fields and tools work only once TurboJedi is restarted' %
                            (version, VERSION))
        else:
            item['note'] = 'this instance runs %s, turbojedi-mcp.py is %s' % (version, VERSION)

    return item


checked = {}  # The versions of the interface of the instances by their names, asked once


def supported(bus, name):
    '''The name of an instance this proxy can speak to'''
    if name not in checked:
        checked[name] = plain(call(bus, name, 'Describe')).get('api', 0)

    if checked[name] > API:
        raise Failure('this TurboJedi speaks version %d of its D-Bus interface, this turbojedi-mcp.py knows up to %d: '
                      'update turbojedi-mcp.py, the package of TurboJedi has it' % (checked[name], API))

    return name


def instance(bus, arguments):
    '''The name of the instance the arguments name by pid, the only one there is without it'''
    return supported(bus, choose(bus, arguments))


def choose(bus, arguments):
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


FRONTS = {'timeline': 'a timeline', 'watches': 'the list of watches', 'watch': 'the lines of a watch'}


def window(bus, name, arguments):
    '''The window of the arguments, the window of the journal the user has in front without one'''
    if arguments.get('window_id') is not None:
        return number(arguments, 'window_id')

    for item in plain(call(bus, name, 'ListWindows')):
        if item.get('active'):
            return item['id']

    front = plain(call(bus, name, 'Describe'))

    if 'front' not in front:
        raise Failure(older('does not tell which window the user has in front', 'give window_id from list_windows'))

    raise Failure('the user has %s in front, not a window of the journal: give window_id from list_windows' %
                  FRONTS.get(front['front'], 'no window'))


# Tools

def list_instances(arguments):
    bus = connect()
    found = names(bus)
    result = []
    others = 0

    if not found:
        # Nothing runs: an answer, not a failure, with the reason the agent may tell
        return {'instances': [], 'note': missing()}

    for pid in sorted(found):
        try:
            item = annotate(plain(call(bus, found[pid], 'Describe')))
            if not item.get('unsupported'):
                item['windows'] = [{'window_id': window['id'], 'window': window['window'], 'title': window['title']}
                                   for window in plain(call(bus, found[pid], 'ListWindows'))]
                item.update(overview(bus, found[pid]))
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
    return annotate(plain(call(bus, choose(bus, arguments), 'Describe')))


def list_windows(arguments):
    bus = connect()
    windows = plain(call(bus, instance(bus, arguments), 'ListWindows'))

    for window in windows:
        # The name the other tools take
        window['window_id'] = window.pop('id')

    return {'windows': windows}


def get_cursor(arguments):
    bus = connect()
    name = instance(bus, arguments)
    identity = window(bus, name, arguments)
    return {'window_id': identity, 'entry': entry(plain(call(bus, name, 'GetCursor', dbus.UInt32(identity))))}


def get_marked(arguments):
    bus = connect()
    name = instance(bus, arguments)
    identity = window(bus, name, arguments)
    entries, total = plain(call(bus, name, 'GetMarked', dbus.UInt32(identity), dbus.UInt32(number(arguments, 'limit', 0))))
    return {'window_id': identity, 'entries': pick(entries, arguments), 'total': total}


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
    name = instance(bus, arguments)
    identity = window(bus, name, arguments)
    cursor = text(arguments, 'cursor')

    if cursor is None:
        # The entry under the cursor of the window
        cursor = plain(call(bus, name, 'GetCursor', dbus.UInt32(identity))).get('__CURSOR')
        if cursor is None:
            raise Failure('the window %d has no entry under its cursor' % identity)

    return {'window_id': identity, 'cursor': cursor,
            'actions': plain(call(bus, name, 'ListActions', dbus.UInt32(identity), dbus.String(cursor)))}


def list_watches(arguments):
    bus = connect()
    watches = plain(call(bus, instance(bus, arguments), 'ListWatches'))

    for watch in watches:
        watch['watch_id'] = watch.pop('id')

    return {'watches': watches}


def get_watch_lines(arguments):
    bus = connect()
    name = instance(bus, arguments)
    limit = number(arguments, 'limit', 100, minimum=1)
    front = plain(call(bus, name, 'Describe'))
    identity = number(arguments, 'watch_id')

    if identity is None:
        # The watch the user has in front: the one under the cursor of the list, or the one whose lines are shown
        if 'front' not in front:
            raise Failure(older('does not tell which watch the user has in front', 'give watch_id from list_watches'))

        if (front.get('front') not in ('watches', 'watch')) or not front.get('watch'):
            raise Failure('the user has %s in front, not a watch: give watch_id from list_watches' %
                          ('no watch under the cursor of the list' if front.get('front') == 'watches' else
                           FRONTS.get(front.get('front'), 'a window of the journal' if front.get('front') else 'no window')))
        identity = front['watch']

    lines = plain(call(bus, name, 'GetWatchLines', dbus.UInt32(identity)))
    answer = {'watch_id': identity, 'lines': lines[:limit], 'total': len(lines)}

    if (front.get('front') == 'watch') and (front.get('watch') == identity) and front.get('line'):
        # The line under the cursor of the user, wherever it is among the lines
        for line in lines:
            if line['number'] == front['line']:
                answer['line'] = line

    return answer


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


# Methods of the plugins

EFFECTS = ('none', 'screen')  # What an agent may call: a method that reads, or one that changes what the user sees
LIMITS = {'u': (0, 0xffffffff), 'i': (-0x80000000, 0x7fffffff), 't': (0, 0xffffffffffffffff),
          'x': (-0x8000000000000000, 0x7fffffffffffffff)}
JSON = {'s': {'type': 'string'}, 'b': {'type': 'boolean'}, 'd': {'type': 'number'},
        'as': {'type': 'array', 'items': {'type': 'string'}}}


def offered(method):
    '''A method an agent is given: one that tells what it does, and does not change the system by its own word'''
    return bool(method.get('description')) and (method.get('effect') in EFFECTS)


def methods(bus, name):
    return plain(call(bus, name, 'ListMethods'))


def values_schema(method):
    '''The JSON schema of the values of a method by its arguments, any values for one that has not told them'''
    if not method.get('declared'):
        return {'type': 'object', 'description': 'the method has not told its arguments'}

    properties = {}

    for argument in method.get('arguments', []):
        item = dict(JSON.get(argument['type'], {'type': 'integer'}))
        if argument['type'] in LIMITS:
            item['minimum'], item['maximum'] = LIMITS[argument['type']]
        if argument.get('description'):
            item['description'] = argument['description']
        properties[argument['name']] = item

    result = {'type': 'object', 'properties': properties, 'additionalProperties': False}
    required = [argument['name'] for argument in method.get('arguments', []) if argument.get('required')]

    if required:
        result['required'] = required

    return result


def convert(name, kind, value):
    '''A value of the agent as the argument of its type, checked: the core checks it again'''
    integer = isinstance(value, int) and not isinstance(value, bool)

    if kind == 's' and isinstance(value, str):
        return dbus.String(value)
    if kind == 'b' and isinstance(value, bool):
        return dbus.Boolean(value)
    if kind == 'd' and (integer or isinstance(value, float)):
        return dbus.Double(value)
    if kind == 'as' and isinstance(value, list) and all(isinstance(item, str) for item in value):
        return dbus.Array(value, signature='s')
    if kind in LIMITS and integer and (LIMITS[kind][0] <= value <= LIMITS[kind][1]):
        return {'u': dbus.UInt32, 'i': dbus.Int32, 't': dbus.UInt64, 'x': dbus.Int64}[kind](value)

    raise Failure('argument %s is %s, the value given is not' % (name, {'s': 'a string', 'b': 'true or false',
                  'd': 'a number', 'as': 'a list of strings', 'u': 'a whole number of 0 to 4294967295',
                  'i': 'a whole number of 32 bits', 't': 'a whole number of 0 or more', 'x': 'a whole number of 64 bits'}[kind]))


def guess(name, value):
    '''A value for a method that has not told its arguments, of the type JSON gives it'''
    if isinstance(value, bool):
        return dbus.Boolean(value)
    if isinstance(value, int):
        return dbus.Int64(value)
    if isinstance(value, float):
        return dbus.Double(value)
    if isinstance(value, str):
        return dbus.String(value)
    if isinstance(value, list) and all(isinstance(item, str) for item in value):
        return dbus.Array(value, signature='s')

    raise Failure('argument %s is a string, a number, true or false, or a list of strings' % name)


def overview(bus, name):
    '''What the plugins of an instance offer an agent, without the schemas: nothing for an instance older than the
    methods, or one whose list fails, the rest of the instance is told all the same'''
    try:
        found = methods(bus, name)
    except Failure:
        return {}

    return {'methods': [{'plugin': method['plugin'], 'method': method['method'], 'effect': method['effect'],
                         'description': method['description']} for method in found if offered(method)]}


def list_methods(arguments):
    bus = connect()
    result = []

    for method in methods(bus, instance(bus, arguments)):
        if offered(method):
            result.append({'plugin': method['plugin'], 'method': method['method'], 'description': method['description'],
                           'effect': method['effect'], 'values': values_schema(method)})

    return {'methods': result}


def invoke(arguments):
    bus = connect()
    name = instance(bus, arguments)
    plugin = text(arguments, 'plugin')
    called = text(arguments, 'method')
    given = arguments.get('values', {})

    if given is None:
        given = {}

    if not plugin or not called:
        raise Failure('plugin and method are needed, list_methods tells the ones there are')
    if not isinstance(given, dict):
        raise Failure('values is an object of the arguments of the method')

    # Asked again for every call: the plugins may have been loaded again since list_methods
    found = [method for method in methods(bus, name) if (method['plugin'] == plugin) and (method['method'] == called)]

    if not found:
        raise Failure('plugin %s has no method %s, list_methods tells the ones there are' % (plugin, called))
    if not offered(found[0]):
        raise Failure('method %s of plugin %s is not for an agent: it %s' % (called, plugin, 'tells nothing of what '
                      'it does' if not found[0].get('description') else 'changes the system, the user runs it'))

    method = found[0]
    kinds = dict((argument['name'], argument['type']) for argument in method.get('arguments', []))
    values = dbus.Dictionary({}, signature='sv')

    for key, value in given.items():
        if key.startswith('_'):
            raise Failure('keys beginning with _ are of TurboJedi: the window goes by window_id')
        if method.get('declared') and (key not in kinds):
            raise Failure('method %s of plugin %s takes no argument %s' % (called, plugin, key))
        values[key] = convert(key, kinds[key], value) if method.get('declared') else guess(key, value)

    for argument in method.get('arguments', []):
        if argument.get('required') and (argument['name'] not in given):
            raise Failure('method %s of plugin %s needs argument %s' % (called, plugin, argument['name']))

    try:
        # The window in front unless another is named, the journal of the machine when the user has none in front
        identity = window(bus, name, arguments)
        values['_window'] = dbus.UInt32(identity)
    except Failure:
        if arguments.get('window_id') is not None:
            raise
        identity = None

    answer = plain(call(bus, name, 'Invoke', dbus.String(plugin), dbus.String(called), values))
    return {'window_id': identity, 'answer': answer}


# Schemas

PID = {'type': 'integer', 'description': 'PID of the instance, from list_instances; may be left out when only one runs'}
WINDOW = {'type': 'integer', 'description': 'window_id of the window, from list_windows: not the number on the screen'}
ACTIVE = {'type': 'integer', 'description': 'window_id of the window, from list_windows; the window of the journal the user '
                                           'has in front without it'}
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
     'Instances of TurboJedi running on this machine that answer this user: PID, user, host, version, start time, '
     'their windows, and methods, what their plugins offer beyond a query of the journal, such as the sessions of an ID '
     'in BrandMeister or TetraPack; list_methods gives their schemas, invoke calls them. Start here. An empty list comes with a note why: TurboJedi is not running, or its D-Bus policy is '
     'not installed. outdated marks an instance started before TurboJedi was updated, it lacks some fields until it is '
     'restarted; unsupported, one this proxy cannot speak to. Tell the user the note of either.',
     {'type': 'object', 'properties': {}, 'additionalProperties': False}),

    ('describe', describe, True,
     'An instance of TurboJedi and what the user has in front: pid, uid, user, host, version, started (microseconds), '
     'api; sudo, the user an instance of root started by sudo answers as well; front, the window in front: journal '
     'with window (its window_id) and title, timeline with title, its filter, step (microseconds of an interval), '
     'counted_since and counted_until, the range counted, interval_since and interval_until, the interval under the '
     'cursor or the selection, with intervals, selection, errors, warnings and other, the counts of it (the times are '
     'local, or of UTC with UTC at the end, query takes them as since and until with the filter to read the entries of the bars), watches (the list of watches) with watch, the one '
     'under the cursor, or watch (the lines of a watch) with watch and line, the number of the line under the cursor; '
     'dialog, whether a dialog or a menu is open over it, the screen tools wait then; descriptors and descriptor_limit, '
     'the files the process has open and its limit, as every window and watch keeps the files of its journal open.',
     schema({})),

    ('list_windows', list_windows, True,
     'The windows of the journal of an instance, the front one first: window_id, the number on the screen, the title, '
     'the filter (text, levels as EACRWNID with - for hidden ones, matches, the fields of Source, since, until), '
     'whether the window follows the end of the journal, how many entries are marked or selected in it, and active, '
     'the one the user has in front. No window is active while a timeline or a list of watches is in front.',
     schema({})),

    ('get_cursor', get_cursor, True,
     'The entry under the cursor of a window, every field of it: what the user is looking at. null without one. '
     'Without window_id, the window the user has in front.',
     schema({'window_id': ACTIVE})),

    ('get_marked', get_marked, True,
     'The entries the user has marked or selected in a window, the oldest first, and how many there are in all. '
     'Without window_id, the window the user has in front.',
     schema({'window_id': ACTIVE, 'limit': {'type': 'integer', 'minimum': 0, 'description': 'entries, 1000 by default'},
             'fields': FIELDS})),

    ('get_entry', get_entry, True,
     'An entry by its __CURSOR in the journal of a window, every field of it: of a line of a watch, of an answer of a '
     'query. null when it is not there.',
     schema({'window_id': WINDOW, 'cursor': CURSOR}, ('window_id', 'cursor'))),

    ('query_journal', query_journal, True,
     'Entries of the journal, the newest first unless after is given; the journal of a window or of the machine. '
     'Every field of an entry unless fields are given; binary values as {"base64": ...}. stopped tells why the query '
     'has ended before the journal or the range: limit, scan or time (30 seconds). Up to 4 queries at once. For what '
     'a service means by its messages, its sessions, calls or contexts, look at methods of list_instances first: a '
     'plugin that knows the service finds it better than a text does.',
     schema(QUERY)),

    ('list_actions', list_actions, True,
     'The items of the Actions menu of TurboJedi for an entry of a window, title and plugin: what the user can do with '
     'it, such as the status of its unit or a ban of its address. An item of a submenu has path, the titles of the '
     'submenus it is in, outer first: the user goes there by them. The handlers of the plugins make the menu as opening '
     'it does, the items are not run from here: name the one that helps and the user runs it with Alt-F10 or A on the '
     'entry. Without window_id, the window the user has in front; without cursor, the entry under its cursor.',
     schema({'window_id': ACTIVE, 'cursor': CURSOR})),

    ('list_watches', list_watches, True,
     'Watches the plugins keep of the journal while TurboJedi runs, such as SSH attacks or alerts: watch_id, plugin, '
     'key, title, state (active, stopped, failed), the status the plugin tells, gaps, unread lines, the time of the '
     'last line.',
     schema({})),

    ('get_watch_lines', get_watch_lines, True,
     'The lines of a watch, the newest first: number, moment, text, unread, and the __CURSOR and time of its entry when '
     'it has one, for get_entry and go_to_entry. Without watch_id, the watch the user has in front: the one under the '
     'cursor of the list of watches, or the one whose lines are open. While its lines are open, line is the one under '
     'the cursor of the user.',
     schema({'watch_id': {'type': 'integer', 'description': 'watch_id from list_watches; the watch in front without it'},
             'limit': {'type': 'integer', 'minimum': 1, 'description': 'lines, 100 by default'}})),

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

    ('list_methods', list_methods, True,
     'Methods plugins offer for an agent: what each does, its effect (none: it only reads; screen: it changes what the '
     'user sees, such as a watch set) and the JSON schema of its values. They know what the plugin knows: the searches '
     'of BrandMeister or TetraPack in the journal and in their cores, a watch of a context. A method that changes the '
     'system is never offered, nor one that does not tell what it does.',
     schema({})),

    ('invoke', invoke, False,
     'Calls a method of a plugin from list_methods with its values, in a window: queries of the method read the journal '
     'of the window and a watch it sets follows it. Without window_id, the window the user has in front, the journal of '
     'the machine when no window of the journal is in front. Returns the answer of the method. A method of effect '
     'screen changes what the user sees.' + SCREEN,
     schema({'plugin': {'type': 'string', 'description': 'the plugin, as list_methods names it'},
             'method': {'type': 'string', 'description': 'the method, as list_methods names it'},
             'values': {'type': 'object', 'description': 'the arguments of the method, as its schema of list_methods tells'},
             'window_id': ACTIVE}, ('plugin', 'method'))),

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
