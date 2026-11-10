"""The environment of every Blit process a gate starts (blit-server,
blit-compositor, a Blit helper). One rule, the same in all three gate
languages: tools/tests/gate_env.rb (Ruby), tools/tests/gate_env.py (Python)
and tools/tests/gate_env.sh (sh).

  1. Start from the gate's own environment minus everything a Blit session,
     Hibr or systemd puts there: every BLIT_*, HIBR_* and LISTEN_* variable,
     WAYLAND_DISPLAY, WAYLAND_SOCKET and NOTIFY_SOCKET. PATH, HOME, LANG,
     TMPDIR, XDG_RUNTIME_DIR, ASAN_OPTIONS and the rest stay.
  2. BLIT_INPUT_ROOT is an empty directory of this process's own, so the
     server opens no real /dev/input device.
  3. The NAME=value words of BLIT_GATE_SERVER_ENV, the one sanctioned way for
     an operator (or a wrapper script: the shell wrappers hand the mock
     display over this way) to give gate servers a variable.
  4. What the gate sets itself, which wins over all of the above.

    env = gate_env.server_env(BLIT_PSF_FONT=font)
    subprocess.Popen([binary, str(port)], env=env, ...)
"""

import atexit
import os
import re
import shutil
import tempfile

FOREIGN = re.compile(r'(?:BLIT_|HIBR_|LISTEN_)|(?:WAYLAND_DISPLAY|WAYLAND_SOCKET|NOTIFY_SOCKET)$')

_no_input = []


def foreign(name):
    return FOREIGN.match(name) is not None


def no_input():
    """Empty, private to this process, removed when it exits."""
    if not _no_input:
        directory = tempfile.mkdtemp(prefix='blit-gate-no-input')
        owner = os.getpid()
        atexit.register(lambda: os.getpid() == owner and shutil.rmtree(directory, ignore_errors=True))
        _no_input.append(directory)
    return _no_input[0]


def operator():
    words = os.environ.get('BLIT_GATE_SERVER_ENV', '').split()
    return dict(word.partition('=')[::2] for word in words)


def _complete(env, extra, more):
    env.update(extra or {})
    env.update(more)
    return {k: str(v) for k, v in env.items() if v is not None}


def server_env(extra=None, **more):
    """blit-server's environment: steps 1 to 4."""
    env = {k: v for k, v in os.environ.items() if not foreign(k)}
    env['BLIT_INPUT_ROOT'] = no_input()
    env.update(operator())
    return _complete(env, extra, more)


def process_env(extra=None, **more):
    """Any other Blit process a gate starts (blit-compositor, a Blit helper):
    steps 1 and 4. BLIT_GATE_SERVER_ENV is the server's -- it carries the mock
    display's LD_PRELOAD, which a sanitized compositor must not get."""
    return _complete({k: v for k, v in os.environ.items() if not foreign(k)}, extra, more)
