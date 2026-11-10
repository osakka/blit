#!/usr/bin/env python3
"""ADR 0228 phase 1, milestone B: GET /v1/input/keymap.

    keymap_gate.py <xkb_keymap_check> <blit-server>

Blit, on the mock display with two fabricated input nodes (a keyboard and
a pointer, BLIT_INPUT_ROOT sidecars), serves its keyboard as an xkb keymap.
The text must compile with libxkbcommon (tools/tests/xkb_keymap_check.c)
and type what Blit types: for the US default, KEY_A is 'a' and with Shift
'A', and the main block, Return, the modifiers and CapsLock behave; after a
PATCH of the keyboard's `keymap` and `modifiers` (docs/api.md, input
devices) the overridden key and the remapped modifier appear, and the ETag
changes. If-None-Match is answered 304; an unknown device 404.

Repeat (milestone C): every answer, 200 and 304, carries the keyboard's
repeat as X-Blit-Repeat-Rate and X-Blit-Repeat-Delay (the fabricated
keyboard's 250 ms and 33 ms period: 30 a second), 0 and 0 for the pointer;
a PATCH of repeat changes them and not the ETag. Which keyboard: a second
fabricated keyboard; the default is the first enabled one until a key comes
from another (POST /v1/input/inject with its device_id), then that one,
and back.
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
PORT = (int(os.environ.get('BLIT_GATE_PORT', '19824')) +
        int(os.environ.get('BLIT_GATE_PORT_OFFSET', '0')))
BASE = 'http://127.0.0.1:%d' % PORT

# evdev codes
KEY_1, KEY_Q, KEY_A, KEY_SEMICOLON, KEY_SPACE, KEY_ENTER = 2, 16, 30, 39, 57, 28
KEY_LEFTSHIFT, KEY_LEFTCTRL, KEY_RIGHTALT, KEY_CAPSLOCK, KEY_LEFTMETA = 42, 29, 100, 58, 125
KEY_ESC, KEY_F1, KEY_UP, KEY_102ND = 1, 59, 103, 86


def fail(why):
    print('KEYMAP_GATE_FAIL %s' % why, file=sys.stderr)
    sys.exit(1)


def check(ok, why):
    if not ok:
        fail(why)


def get(path, headers=None):
    request = urllib.request.Request(BASE + path, headers=headers or {})
    try:
        with urllib.request.urlopen(request, timeout=10) as response:
            return response.status, dict(response.headers), response.read()
    except urllib.error.HTTPError as error:
        return error.code, dict(error.headers), error.read()


def patch(path, body):
    request = urllib.request.Request(BASE + path, method='PATCH', data=json.dumps(body).encode(),
                                     headers={'Content-Type': 'application/json'})
    with urllib.request.urlopen(request, timeout=10) as response:
        return json.loads(response.read())


def post(path, body):
    request = urllib.request.Request(BASE + path, method='POST', data=json.dumps(body).encode(),
                                     headers={'Content-Type': 'application/json'})
    with urllib.request.urlopen(request, timeout=10) as response:
        return response.read()


def compile_check(checker, text, work, cases):
    path = os.path.join(work, 'keymap.xkb')
    with open(path, 'wb') as out:
        out.write(text)
    argv = [checker, path]
    for code, held, expect in cases:
        argv += [str(code), held, expect]
    result = subprocess.run(argv, capture_output=True, text=True)
    check(result.returncode == 0, 'libxkbcommon:\n%s%s\n--- keymap\n%s'
          % (result.stdout, result.stderr, text.decode(errors='replace')))
    return result.stdout.split()[-1]


def main():
    checker, server = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
    work = tempfile.mkdtemp(prefix='blit-keymap-gate-')
    nodes = os.path.join(work, 'nodes')
    os.mkdir(nodes)
    for name, identity in (('event0', 'name=Fabricated Pointer\nkinds=pointer\n'
                                      'capabilities=rel-x,rel-y,buttons\n'),
                           ('event1', 'name=Fabricated Keyboard\nkinds=keyboard\n'
                                      'capabilities=keys,repeat\nrepeat=250,33\n'),
                           ('event2', 'name=Second Keyboard\nkinds=keyboard\n'
                                      'capabilities=keys,repeat\nrepeat=500,50\n')):
        open(os.path.join(nodes, name), 'w').close()
        with open(os.path.join(nodes, name + '.identity'), 'w') as out:
            out.write('bustype=3\nvendor=05ac\nproduct=0256\nversion=0100\n'
                      'phys=usb-0000:00:14.0-4/input0\n' + identity)
    env = dict(os.environ, BLIT_PSF_FONT=os.path.join(ROOT, 'assets/fonts/Blit-Terminus16.psf'),
               BLIT_SETTINGS_PATH=os.path.join(work, 'settings.json'), BLIT_LOGIN_AUTOSTART='0',
               BLIT_FONT_ROOT=os.path.join(ROOT, 'assets/fonts'), BLIT_INPUT_ROOT=nodes)
    blit = subprocess.Popen([server, str(PORT)], env=env, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    try:
        deadline = time.time() + 15
        while True:
            try:
                if get('/v1/version')[0] == 200:
                    break
            except OSError:
                pass
            check(time.time() < deadline, 'the server did not start')
            time.sleep(0.1)
        devices = json.loads(get('/v1/input/devices')[2])['devices']
        named = dict((d['name'], d['id']) for d in devices)
        check('Fabricated Keyboard' in named and 'Fabricated Pointer' in named,
              'no fabricated keyboard and pointer: %s' % devices)
        keyboard, pointer_id = named['Fabricated Keyboard'], named['Fabricated Pointer']
        second = named['Second Keyboard']

        status, headers, text = get('/v1/input/keymap')
        check(status == 200, 'GET /v1/input/keymap -> %d %r' % (status, text[:200]))
        check(headers.get('Content-Type', '').startswith('text/plain'),
              'Content-Type %r' % headers.get('Content-Type'))
        check(headers.get('X-Blit-Keymap-Device') == keyboard,
              'the keymap is not the keyboard\'s: %r' % headers.get('X-Blit-Keymap-Device'))
        etag = headers.get('ETag', '')
        check(len(etag) == 66 and etag.startswith('"'), 'ETag %r' % etag)
        check(text.startswith(b'xkb_keymap {'), 'not an xkb keymap: %r' % text[:40])
        keys = compile_check(checker, text, work, [
            (KEY_A, '-', 'a'), (KEY_A, str(KEY_LEFTSHIFT), 'A'), (KEY_Q, '-', 'q'),
            (KEY_1, '-', '1'), (KEY_1, str(KEY_LEFTSHIFT), '!'),
            (KEY_SEMICOLON, '-', ';'), (KEY_SEMICOLON, str(KEY_LEFTSHIFT), ':'),
            (KEY_SPACE, '-', ' '), (KEY_ENTER, '-', 'sym:Return'), (KEY_ESC, '-', 'sym:Escape'),
            (KEY_F1, '-', 'sym:F1'), (KEY_UP, '-', 'sym:Up'),
            (KEY_LEFTCTRL, '-', 'mod:Control'), (KEY_LEFTSHIFT, '-', 'mod:Shift'),
            (KEY_RIGHTALT, '-', 'mod:Mod5'), (KEY_LEFTMETA, '-', 'mod:Mod4'),
            # CapsLock inverts Shift for letters only, as in Blit.
            (KEY_A, str(KEY_CAPSLOCK), 'A'), (KEY_1, str(KEY_CAPSLOCK), '1'),
            (KEY_A, '%d,%d' % (KEY_CAPSLOCK, KEY_LEFTSHIFT), 'a'),
            # The US table has no third level: AltGr types the plain key.
            (KEY_Q, str(KEY_RIGHTALT), 'q'),
            # No character and no role in Blit: no key.
            (KEY_102ND, '-', 'sym:NoSymbol'),
        ])
        status, h304, body = get('/v1/input/keymap', {'If-None-Match': etag})
        check(status == 304 and body == b'', 'If-None-Match -> %d' % status)
        repeat = lambda h: (h.get('X-Blit-Repeat-Rate'), h.get('X-Blit-Repeat-Delay'))
        check(repeat(headers) == ('30', '250') and repeat(h304) == ('30', '250'),
              'the repeat headers: 200 %s, 304 %s' % (repeat(headers), repeat(h304)))
        # A repeat PATCH: the headers change, the keymap's ETag does not.
        patch('/v1/input/devices/%s' % keyboard, {'configuration': {'repeat_delay_ms': 400,
                                                                    'repeat_rate_hz': 20}})
        status, h304, _ = get('/v1/input/keymap', {'If-None-Match': etag})
        check(status == 304 and repeat(h304) == ('20', '400'),
              'after a repeat PATCH: %d %s' % (status, repeat(h304)))
        check(repeat(get('/v1/input/keymap?device=%s' % pointer_id)[1]) == ('0', '0'),
              'the pointer has a repeat')
        # Which keyboard: the first enabled, until a key comes from another.
        default = lambda: get('/v1/input/keymap')[1].get('X-Blit-Keymap-Device')
        check(default() == keyboard, 'the default before any key: %s' % default())
        for device, rate in ((second, ('20', '500')), (keyboard, ('20', '400'))):
            for value in (1, 0):
                post('/v1/input/inject', {'kind': 'key', 'code': KEY_A, 'value': value, 'device_id': device})
            status, h, _ = get('/v1/input/keymap')
            check(h.get('X-Blit-Keymap-Device') == device and repeat(h) == rate,
                  'after a key from %s: %s %s' % (device, h.get('X-Blit-Keymap-Device'), repeat(h)))
        post('/v1/input/inject', {'kind': 'key', 'code': KEY_A, 'value': 1, 'device_id': second})
        post('/v1/input/inject', {'kind': 'key', 'code': KEY_A, 'value': 0, 'device_id': second})
        patch('/v1/input/devices/%s' % second, {'configuration': {'enabled': False}})
        check(default() == keyboard, 'the last key\'s keyboard disabled: %s' % default())
        patch('/v1/input/devices/%s' % second, {'configuration': {'enabled': True}})
        check(default() == second, 'the last key\'s keyboard enabled again: %s' % default())
        post('/v1/input/inject', {'kind': 'key', 'code': KEY_A, 'value': 1, 'device_id': keyboard})
        post('/v1/input/inject', {'kind': 'key', 'code': KEY_A, 'value': 0, 'device_id': keyboard})
        check(default() == keyboard, 'the first keyboard again: %s' % default())

        # The keyboard settings, Blit's own: Q types a-umlaut, A-umlaut with
        # Shift, '@' with AltGr; CapsLock is Control; the 102nd key types
        # '<' and '>'.
        patch('/v1/input/devices/%s' % keyboard, {'configuration': {
            'keymap': {'base': 'us', 'keys': {str(KEY_Q): ['ä', 'Ä', '@', ''],
                                              str(KEY_102ND): ['<', '>', '', '']}},
            'modifiers': {str(KEY_CAPSLOCK): 'control'}}})
        status, headers, remapped = get('/v1/input/keymap')
        check(status == 200 and headers.get('ETag') != etag,
              'the ETag did not change with the keymap: %r' % headers.get('ETag'))
        compile_check(checker, remapped, work, [
            (KEY_Q, '-', 'ä'), (KEY_Q, str(KEY_LEFTSHIFT), 'Ä'),
            (KEY_Q, str(KEY_RIGHTALT), '@'),
            # Blit: AltGr with Shift and no fourth level falls back to Shift.
            (KEY_Q, '%d,%d' % (KEY_RIGHTALT, KEY_LEFTSHIFT), 'Ä'),
            (KEY_102ND, '-', '<'), (KEY_102ND, str(KEY_LEFTSHIFT), '>'),
            (KEY_CAPSLOCK, '-', 'mod:Control'), (KEY_CAPSLOCK, '-', 'sym:Control_L'),
            (KEY_A, '-', 'a'), (KEY_A, str(KEY_LEFTSHIFT), 'A'),
        ])
        check(get('/v1/input/keymap?device=%s' % keyboard)[2] == remapped,
              'the keyboard named is not the default')
        status, _, pointer = get('/v1/input/keymap?device=%s' % pointer_id)
        check(status == 200 and pointer != remapped, 'the pointer\'s keymap: %d' % status)
        check(get('/v1/input/keymap?device=nope')[0] == 404, 'an unknown device is not 404')
        print('KEYMAP_GATE_OK xkb_v1 compiles keys=%s us:a/A,1/!,caps,altgr remap:q->ä/Ä/@,'
              '102nd-></>,caps->control etag=changes,304 unknown=404 repeat=30/250,304-too,'
              'patch->20/400,pointer=0/0 default=first,last-key(second,first),disabled->first,enabled->last-key'
              % keys.split('=')[1])
    finally:
        blit.terminate()
        try:
            blit.wait(5)
        except subprocess.TimeoutExpired:
            blit.kill()
        shutil.rmtree(work, ignore_errors=True)


if __name__ == '__main__':
    main()
