"""Run the iwd mock on a private bus without touching host networking."""
import os
import subprocess
import sys

bus = subprocess.Popen(
    [sys.argv[1], "--session", "--nofork", "--print-address=1"],
    stdout=subprocess.PIPE, text=True,
)
try:
    address = bus.stdout.readline().strip()
    if not address:
        raise RuntimeError("Private D-Bus did not start")
    subprocess.run([sys.argv[2]], check=True, timeout=35,
                   env={**os.environ, "DBUS_SYSTEM_BUS_ADDRESS": address})
finally:
    bus.terminate()
    bus.wait(timeout=5)
