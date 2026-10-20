"""Exercise real fork/exec/signal supervision with simulated VT ioctls."""
import os
from pathlib import Path
import resource
import signal
import subprocess
import sys
import tempfile
import time

launcher = sys.argv[1]
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
expected = ['keyboard 4', 'display 1', 'display 0', 'keyboard 1']

with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    vt = root / 'tty'
    vt.touch()
    log = root / 'modes'
    marker = root / 'ready'
    environment = dict(os.environ, TEST_VT_FILE=str(vt), TEST_VT_LOG=str(log))

    def reset():
        log.write_text('')
        marker.unlink(missing_ok=True)

    def modes():
        return log.read_text().splitlines()

    def run(command, code, extra=None, tty=None, expected_modes=expected):
        reset()
        args = [launcher] + (['--tty', tty] if tty else []) + ['--'] + command
        result = subprocess.run(args, env=dict(environment, **(extra or {})),
                                capture_output=True, timeout=10)
        assert result.returncode == code, (command, result.returncode, result.stderr)
        assert modes() == expected_modes, modes()

    run(['/bin/true'], 0)
    run(['/bin/sh', '-c', 'exit 23'], 23)
    run(['/no/such/lvgl-console'], 127)
    run(['/bin/sh', '-c', 'kill -SEGV $$'], 128 + signal.SIGSEGV)
    run(['/bin/true'], 1, {'TEST_GRAPHICS_FAIL': '1'},
        expected_modes=['keyboard 4', 'display 1', 'keyboard 1'])
    run(['/bin/true'], 1, {'TEST_FORK_FAIL': '1'})
    run(['/bin/true'], 1, {'TEST_ALREADY_GRAPHICS': '1'}, expected_modes=[])
    run(['/bin/true'], 1, tty='/dev/tty2', expected_modes=[])

    def supervise(action, ignore_term=False):
        reset()
        code = ('import os,signal,time; from pathlib import Path; '
                + ('signal.signal(signal.SIGTERM, signal.SIG_IGN); ' if ignore_term else '')
                + f'Path({str(marker)!r}).write_text(str(os.getpid())); time.sleep(30)')
        process = subprocess.Popen([launcher, '--', sys.executable, '-c', code],
                                   env=environment, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        child = None
        try:
            deadline = time.monotonic() + 5
            while not marker.exists() or not marker.read_text():
                assert process.poll() is None, process.communicate()
                assert time.monotonic() < deadline, 'Child did not start'
                time.sleep(0.01)
            child = int(marker.read_text())
            action(process, child)
            process.communicate(timeout=9)
            assert modes() == expected, modes()
            return process.returncode
        finally:
            if process.poll() is None:
                if child:
                    os.kill(child, signal.SIGKILL)
                process.kill()
                process.communicate()

    assert supervise(lambda p, child: os.kill(child, signal.SIGKILL)) == 137
    assert supervise(lambda p, child: p.terminate()) == 143
    assert supervise(lambda p, child: p.send_signal(signal.SIGINT)) == 130
    assert supervise(lambda p, child: p.terminate(), ignore_term=True) == 137

print('Launcher: normal exit, exec/fork/setup failure, crash, SIGKILL, forwarding and timeout passed')
