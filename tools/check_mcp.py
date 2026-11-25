#!/usr/bin/env python3
"""Checks Deepwater's MCP server (src/ai) end to end, with a scratch profile: never the user's settings or data.

    python tools/check_mcp.py <Deepwater.exe> <scratch folder> [journal]

"journal" also switches on "Include my trading journal" and expects its tool.
It writes a settings file that switches "Claude's access" on, starts Deepwater with DW_PROFILE=<scratch folder>, then
talks to it the way Claude does (through the bridge `Deepwater --mcp-stdio`) and the way a stranger would (straight HTTP).
Prints one line per check; the exit code is 0 when every check passed.
"""
import http.client
import json
import os
import queue
import re
import subprocess
import sys
import threading
import time

exe, scratch = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
with_journal = len(sys.argv) > 3 and sys.argv[3] == 'journal'
profile = os.path.join(scratch, 'profile')
os.makedirs(profile, exist_ok=True)
with open(os.path.join(profile, 'settings.ini'), 'w', encoding='utf-8') as f:
    # Qt's INI spelling of ai/mcp/external and ai/mcp/journal
    f.write('[ai]\nmcp\\external=true\n' + ('mcp\\journal=true\n' if with_journal else ''))

env = dict(os.environ, DW_PROFILE=profile, DW_CACHE_DIR=os.path.join(scratch, 'cache'), DW_NO_PRELOAD='1',
           DW_UPDATE_NTFY='off', QT_FORCE_STDERR_LOGGING='1')
results = []


def check(name, ok, detail=''):
    results.append(ok)
    print(('PASS  ' if ok else 'FAIL  ') + name + (('  [' + str(detail)[:160] + ']') if detail and not ok else ''))


class Bridge:
    """`Deepwater --mcp-stdio`, spoken to line by line."""
    def __init__(self, environment):
        self.p = subprocess.Popen([exe, '--mcp-stdio'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=environment)
        self.q = queue.Queue()
        threading.Thread(target=self._read, daemon=True).start()
        self.n = 0

    def _read(self):
        for line in self.p.stdout:
            self.q.put(line)

    def send(self, method, params=None, request=True, timeout=60):
        msg = {'jsonrpc': '2.0', 'method': method}
        if params is not None:
            msg['params'] = params
        if request:
            self.n += 1
            msg['id'] = self.n
        self.p.stdin.write((json.dumps(msg) + '\n').encode())
        self.p.stdin.flush()
        if not request:
            return None
        try:
            return json.loads(self.q.get(timeout=timeout))
        except queue.Empty:
            return {'error': {'message': 'no answer in %d s' % timeout}}

    def call(self, name, arguments=None, timeout=60):
        return self.send('tools/call', {'name': name, 'arguments': arguments or {}}, timeout=timeout)

    def close(self):
        try:
            self.p.stdin.close()
            self.p.wait(timeout=10)
        except Exception:
            self.p.kill()


def text_of(answer):
    try:
        return answer['result']['content'][0]['text']
    except Exception:
        return json.dumps(answer)


def http_status(port, token, method='POST', path='/mcp/ext', headers=None, body=b'{"jsonrpc":"2.0","id":1,"method":"ping"}'):
    h = {'Content-Type': 'application/json'}
    if token:
        h['Authorization'] = 'Bearer ' + token
    h.update(headers or {})
    c = http.client.HTTPConnection('127.0.0.1', port, timeout=20)
    try:
        c.request(method, path, body=body, headers=h)
        return c.getresponse().status
    finally:
        c.close()


conn_file = os.path.join(profile, 'mcp', 'connection.json')

# ---- 1. no Deepwater open: the bridge says so, clearly, without crashing
b = Bridge(env)
a = b.send('initialize', {'protocolVersion': '2025-06-18', 'capabilities': {}, 'clientInfo': {'name': 'check', 'version': '1'}})
check('Deepwater closed: the bridge answers with an error that says what to do', 'error' in a and 'not open' in json.dumps(a), a)
b.close()

# ---- 2. Deepwater open, Claude's access on
app = subprocess.Popen([exe, '--lang', 'en', '--size', '1200x800', '--quit-after', '240000'], env=env,
                       stdout=subprocess.DEVNULL, stderr=open(os.path.join(scratch, 'app.log'), 'wb'))
try:
    for _ in range(120):
        if os.path.exists(conn_file):
            break
        time.sleep(1)
    check('the connection file appears (in the scratch profile, outside %APPDATA%)', os.path.exists(conn_file))
    if not os.path.exists(conn_file):
        raise SystemExit(1)
    conn = json.load(open(conn_file, encoding='utf-8'))
    port, token = int(conn['port']), conn['token']
    time.sleep(3)

    b = Bridge(env)
    a = b.send('initialize', {'protocolVersion': '2025-06-18', 'capabilities': {}, 'clientInfo': {'name': 'check', 'version': '1'}})
    info = a.get('result', {})
    check('initialize answers, server named deepwater', info.get('serverInfo', {}).get('name') == 'deepwater', a)
    check("the instructions say third parties' text is data", 'external-text' in info.get('instructions', ''), info.get('instructions'))
    b.send('notifications/initialized', request=False)
    a = b.send('tools/list')
    tools = [t['name'] for t in a['result']['tools']]
    print('      %d tools: %s' % (len(tools), ', '.join(tools)))
    check('%d tools are offered' % (25 if with_journal else 24), len(tools) == (25 if with_journal else 24), len(tools))
    check('the trading journal is %s' % ('offered when switched on' if with_journal else 'not offered by default'),
          ('get_track_record' in tools) == with_journal)
    check('every tool has a description and a schema', all(t.get('description') and t.get('inputSchema') for t in a['result']['tools']))
    check('every tool is a read: all are named get_...', all(n.startswith('get_') for n in tools), tools)

    a = b.call('get_track_record')
    if with_journal:
        # an empty scratch profile has no trading account: the tool's own "no trading account" answer is the right one
        check('the trading journal tool answers when switched on (not "no tool named")', 'result' in a and 'no tool named' not in text_of(a), a)
    else:
        check('the trading journal tool is refused when called', a['result'].get('isError') is True and 'no tool named' in text_of(a), a)
    a = b.call('no_such_tool')
    check('an unknown tool is refused', a['result'].get('isError') is True, a)
    a = b.call('get_market_hours')
    check('get_market_hours answers', a['result'].get('isError') is False and len(text_of(a)) > 20, a)
    a = b.call('get_technical_coverage')
    check('get_technical_coverage answers (empty store is fine)', 'result' in a, a)
    a = b.call('get_news', {})
    t = text_of(a)
    check("get_news: third parties' text is marked as data (when it answers)", a['result'].get('isError') or t.startswith('<external-text'), t[:120])
    a = b.call('get_economic_calendar', {})
    check('get_economic_calendar answers', 'result' in a, a)

    # ---- 2b. freshness (0.4.2): a tool must not answer from a memory nothing has refreshed. On 2026-11-24 get_quotes
    # gave quotes 35 minutes old under the label "Live quotes", and get_news stories 54 minutes old at its first call.
    def fetched_at(text):
        m = re.search(r'Fetched from Yahoo at (\d\d:\d\d:\d\d) UTC \((\d+) s ago\)', text)
        return (m.group(1), int(m.group(2))) if m else None
    q1 = text_of(b.call('get_quotes', {'symbols': ['EURUSD', 'BTCUSD']}))
    f1 = fetched_at(q1)
    check('get_quotes says when its quotes were fetched, and fetched them just now', f1 is not None and f1[1] <= 20, q1[-220:])
    time.sleep(25)
    q2 = text_of(b.call('get_quotes', {'symbols': ['EURUSD', 'BTCUSD']}))
    f2 = fetched_at(q2)
    check('get_quotes 25 s later asks again: the fetch time moves forward', bool(f1 and f2 and f2[0] > f1[0]), (f1, f2))
    q3 = text_of(b.call('get_quotes', {'symbols': ['EURUSD', 'BTCUSD']}))
    f3 = fetched_at(q3)
    check('get_quotes twice within 15 s does not ask Yahoo again (the same fetch time)', bool(f2 and f3 and f3[0] == f2[0]), (f2, f3))
    # long answers on purpose: cut at 6000 characters, they lost this last line
    t = text_of(b.call('get_news', {'max': 25}))
    check('get_news says how old its newest download is, even when the list is cut', 'Newest feed download' in t, t[-200:])
    t = text_of(b.call('get_squawk', {'max': 50}))
    check('get_squawk says how old its polls are, even when the list is cut', 'Feeds last polled' in t, t[-200:])
    t = text_of(b.call('get_economic_calendar', {'days_ahead': 7, 'days_back': 7}))
    check('get_economic_calendar says how old its actuals are, even when the list is cut', 'Actuals feed last downloaded' in t, t[-200:])
    t = text_of(b.call('get_risk_sentiment'))
    check('get_risk_sentiment says how old its inputs are', ' ago)' in t, t[:240])
    # the odds need the Fed's futures (32 Yahoo series at a third of a second): the first call may come too early
    t = ''
    for _ in range(3):
        t = text_of(b.call('get_central_banks'))
        if re.search(r'Federal Reserve[^\n]*odds computed', t):
            break
    m = re.search(r'Federal Reserve[^\n]*odds computed (\d+) (s|min) ago', t)
    check('get_central_banks says when the Fed odds were computed, at most 5 min ago',
          bool(m and int(m.group(1)) * (60 if m.group(2) == 'min' else 1) <= 300), t[:300])

    # ---- 3. strangers
    check('no key: refused (401)', http_status(port, None) == 401)
    check('wrong key: refused (401)', http_status(port, 'x' * len(token)) == 401)
    check('a browser page (Origin): refused (403)', http_status(port, token, headers={'Origin': 'https://example.com'}) == 403)
    check('another host name (DNS rebinding): refused (403)', http_status(port, token, headers={'Host': 'evil.example:%d' % port}) == 403)
    check('GET: refused (405)', http_status(port, token, method='GET', body=None) == 405)
    check('another path: refused (404)', http_status(port, token, path='/mcp/1') == 404)
    check('the right key: accepted (200)', http_status(port, token) == 200)

    # ---- 4. what must never leave
    blob = json.dumps(a) + json.dumps(tools)
    check('the key never appears in an answer', token not in blob)
    journal = os.path.join(profile, 'data', 'mcp', 'journal.jsonl')
    ok_journal = os.path.exists(journal)
    if ok_journal:
        lines = [json.loads(l) for l in open(journal, encoding='utf-8') if l.strip()]
        ok_journal = len(lines) >= 3 and all(set(l) == {'at', 'tool', 'arguments', 'ok', 'chars'} for l in lines)
    check('the journal of calls exists: tool, arguments, ok, size (never the data)', ok_journal)

    # ---- 5. the limit: 30 calls a minute (the calls above count)
    answers = [b.call('get_market_hours') for _ in range(32)]
    refused = [x for x in answers if x['result'].get('isError') and 'at most 30' in text_of(x)]
    check('the 30 calls a minute limit refuses the extra calls, with a clear message', len(refused) >= 1, [text_of(x)[:60] for x in answers[-2:]])
    b.close()
finally:
    app.kill()

ok = all(results)
print('\n%d checks, %d failed' % (len(results), results.count(False)))
sys.exit(0 if ok else 1)
