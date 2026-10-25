# lvgl-console

Small LVGL 9.6.0 console for the Raspberry Pi 4 Linux framebuffer. Uses the
fbdev/evdev setup from lvgl-video and the logo/tab conventions from lvgl-com.
The first tab, PTT, opens at startup and shows link.png centered in its content
area. Settings contains keyboard entry and a display sleep test.

The UI uses black backgrounds and neon green (`#39FF14`) text, borders,
scrollbars, and logo. Active tab titles and pressed buttons use a dim green
fill (`#103D08`). Active tabs have a thicker border, pressed buttons glow,
and keyboard focus keeps a green outline. The shared palette and styling live
in `src/ui.c`; the logo is recolored at runtime.

LVGL 9.6.0 is vendored in third_party/lvgl. The application uses pthreads,
libpng and zlib; it needs no compositor or multimedia libraries.

## Buildroot

Keep lvgl-console beside rpi-extree. Enable BR2_PACKAGE_LVGL_CONSOLE in the
external package menu, then build:

```sh
make -C buildroot menuconfig
make -C buildroot lvgl-console
```

For local development, disable `BR2_PACKAGE_LVGL_CONSOLE` and enable
`BR2_PACKAGE_LVGL_CONSOLE_LOCAL` under **User interface** in the external
package menu. The local package uses the `lvgl-console` directory beside
`rpi-extree`, including uncommitted edits and the vendored LVGL source:

```sh
make -C buildroot lvgl-console-local
# After editing source files (resyncs the local source and rebuilds):
make -C buildroot lvgl-console-local-rebuild
# After changing CMake options or build configuration:
make -C buildroot lvgl-console-local-reconfigure
```

Both variants install the same binaries and assets, so enable only one.
The local package needs no GitHub push or `buildroot/local.mk` override.
After publishing to GitHub, update the pinned commit in
`rpi-extree/package/lvgl-console/lvgl-console.mk`, disable the local package,
and enable `BR2_PACKAGE_LVGL_CONSOLE` again. Use a clean Buildroot build when
switching variants for a release image; disabling a package does not remove
its previously installed files from the target directory.
Run the normal Buildroot build to regenerate the filesystem image.

The package installs /usr/bin/lvgl-console, /usr/bin/lvgl-console-launcher,
and /usr/share/lvgl-console/link.png. It does not install an autostart service.
Stop any existing framebuffer UI (for example lvgl-com) before starting it:

```sh
LV_LINUX_FBDEV_DEVICE=/dev/fb0 \
LV_LINUX_EVDEV_DEVICE=/dev/input/touchscreen \
    /usr/bin/lvgl-console-launcher
```

Those device paths are the defaults. Override the input path with the proper
/dev/input/eventN if there is no touchscreen symlink. The process needs access
to both devices. Missing input is reported but allows display-only operation;
framebuffer or logo initialization failure exits with an error. SIGINT and
SIGTERM cleanly stop the application.

## Native build and headless check

Requires CMake, C/C++ compilers, pthreads, and libpng development files.

```sh
cmake -S lvgl-console -B /tmp/lvgl-console-native \
    -DCMAKE_BUILD_TYPE=Release \
    -DLVGL_CONSOLE_ASSET_DIR="$PWD/lvgl-console/assets" \
    -DLVGL_CONSOLE_TESTS=ON
cmake --build /tmp/lvgl-console-native -j4
ctest --test-dir /tmp/lvgl-console-native --output-on-failure
```

The smoke test decodes the logo header and renders each tab through an
in-memory display at 640x480. Actual framebuffer output and touch input still
need to be checked on the Pi. For an installed build, omit the asset override;
the default follows CMAKE_INSTALL_PREFIX/share/lvgl-console.
# lvgl-console
# lvgl-console

## External keyboard demo

A persistent 36-pixel-high bottom status bar reserves four 104×28 boxes for
Gateway, Internet, VPS, and WireGuard (in that order), followed by a wider speech
box. Each box contains only its centered value, without a title. Ping reports come
from `/tmp/dpinger/{gateway,internet,vps,wireguard}.status` and display loss and
integer milliseconds as `0%/11ms`; deviation is not displayed. Missing, malformed,
future-dated, or older-than-five-second reports show `--/--`. Four-digit latency
and even 100% loss fit without resizing; latency above 9999 ms shows `>9999ms`.
The dim-red alarm background is selected when loss exceeds
`APP_PING_LOSS_LIMIT_PERCENT` (0) or latency exceeds `APP_PING_LATENCY_LIMIT_MS`
(100), defined in `src/ping_status.h`. Layout and alarm color constants are in
`src/ui.c`.

Ping files are checked once per second on every tab; speech state is checked
four times per second. Unchanged labels and alarm colors are not redrawn, and
polling pauses during the display sleep test. Files are reopened on each read
for atomic replacement. Enable PocketTerm `dpinger.service` to supply reports.

The speech box displays `PTT idle`, `TX <txid>`, or `RX <talker>` (both when
applicable); long text scrolls within the box. Its background is dim red during
TX, dim blue during RX, and black when idle or unavailable. TX takes priority
when both are active. `/tmp/udpptt.sock` is a command-only
socket, so the UI checks it with a nonblocking connect without sending commands
and reads activity from `/run/udpptt/state`. Start the client with
`--ptt-socket /tmp/udpptt.sock --state-file /run/udpptt/state` as in the PocketTerm
service. Missing/unreachable socket shows `PTT offline`; missing or invalid state
shows `PTT unavailable`. The state file changes on activity transitions, not as
a periodic heartbeat, so idle state does not expire. Socket availability does
not prove server connectivity or client responsiveness.

The Settings button **Sleep screen for 5 seconds** blanks the selected framebuffer
using `FBIOBLANK`, then automatically unblanks it and forces a full LVGL redraw,
including overlay layers. The LVGL event loop continues running during the test.
The process needs permission to control `LV_LINUX_FBDEV_DEVICE` (default
`/dev/fb0`). Failures appear below the button and in stderr. Normal application
shutdown also attempts to unblank an ongoing test; SIGKILL cannot perform cleanup.
Hardware input may wake the console early, and HDMI audio may stop while blanked.

Settings contains a labeled single-line text field (128 characters maximum).
Tap it and type on an external keyboard. No on-screen keyboard is created or
shown on focus. Text is for demonstration only and is not saved.

At startup the first `/dev/input/by-id/*-event-kbd` device is opened, falling
back to `/dev/input/keyboard`. Override this with
`LV_LINUX_KEYBOARD_DEVICE=/dev/input/eventN`. Connect the keyboard before
starting the app. The demo uses a US layout with Shift, Caps Lock, Backspace,
Delete, Home and End; international layouts and hotplug are not
implemented. Touch input continues to use `LV_LINUX_EVDEV_DEVICE`.

Left/Right switches tabs globally, including while the Settings text field is
focused. Navigation wraps at either end (PTT / Status / Settings), once per
press; holding an arrow does not cycle repeatedly. The selected title keeps
its dim green fill. Use Home/End to move the text cursor.

For the Pico keyboard shown as `/dev/input/event1`, start with:

```sh
LV_LINUX_KEYBOARD_DEVICE=/dev/input/event1 /usr/bin/lvgl-console-launcher
```

Event numbers can change across boots; prefer the automatic by-id discovery
or set a stable `/dev/input/by-id/*-event-kbd` path when available.

## Console launcher

Run `lvgl-console-launcher` as root (or with the required console privileges)
on the Pi. It resolves `/dev/tty0` to the currently active numbered VT, saves
its display and keyboard modes, disables console keyboard processing, and
switches to graphics mode before starting `/usr/bin/lvgl-console`. The UI still
reads the external keyboard directly through evdev. On exit, the launcher
restores the saved display and keyboard modes, allowing the console to redraw.
It does not stop or restart getty, change termios, or erase console contents.

```sh
/usr/bin/lvgl-console-launcher
# Explicit VT: this terminal must already be active and in text mode.
/usr/bin/lvgl-console-launcher --tty /dev/tty1
# Run a development binary instead of the installed application:
/tmp/lvgl-console-native/lvgl-console-launcher -- /tmp/lvgl-console-native/lvgl-console
```

The launcher refuses an inactive VT, an existing graphics session, or a VT
already locked by another launcher. Stop other framebuffer applications first;
applications that do not use graphics mode cannot be detected by this check.
Keep the selected VT active while the UI runs. The default assumes that this
VT's framebuffer is the same one selected by `LV_LINUX_FBDEV_DEVICE`.

SIGINT, SIGTERM, SIGHUP and SIGQUIT sent to the launcher are forwarded to the
UI process group. If the UI has not exited after five seconds, the launcher
sends SIGKILL and then restores the console. It also restores after UI crashes,
UI SIGKILL, failed exec, and failures during setup. It returns the child's exit
status, or 128 plus the terminating signal number. Restoration failures are
reported and return failure.

Console keyboard processing is disabled while LVGL runs, so local Ctrl+C and
VT-switch shortcuts do not act on the underlying terminal. To stop the demo,
use a second session (for example SSH):

```sh
killall -TERM lvgl-console-launcher
```

For systemd, use `ExecStart=/usr/bin/lvgl-console-launcher`,
`KillMode=mixed`, and `TimeoutStopSec=10s` so the launcher gets the stop signal
and has time to restore the console. Send stdout/stderr to the journal.
No service is enabled automatically.

Restoration requires the launcher itself to remain alive: SIGKILL against the
launcher, a launcher crash, or power loss cannot run its cleanup. Stop it with
SIGTERM; SIGKILL against the UI child is handled. Actual VT redraw and physical
keyboard restoration need verification on the Pi. Automated launcher tests
simulate only the VT operations and exercise real child processes and signals,
without touching the host console.
