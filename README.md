# lvgl-console

Small LVGL 9.6.0 console for the Raspberry Pi 4 Linux framebuffer. Uses the
fbdev/evdev setup from lvgl-video and the logo/tab conventions from lvgl-com.
The Comm tab shows link.png and large Connect and Disconnect buttons. Both
buttons are placeholders; Status and Settings contain placeholder text.

LVGL 9.6.0 is vendored in third_party/lvgl. The application uses pthreads,
libpng and zlib; it needs no compositor or multimedia libraries.

## Buildroot

Keep lvgl-console beside rpi-extree. Enable BR2_PACKAGE_LVGL_CONSOLE in the
external package menu, then build:

```sh
make -C buildroot menuconfig
make -C buildroot lvgl-console
```

The package fetches a pinned Git commit. To test local changes before publishing,
set `LVGL_CONSOLE_OVERRIDE_SRCDIR = /absolute/path/to/lvgl-console` in
`buildroot/local.mk`, then run `make -C buildroot lvgl-console-rebuild`.
For release builds, update the package commit after publishing the changes.
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
in-memory display at 800x480. Actual framebuffer output and touch input still
need to be checked on the Pi. For an installed build, omit the asset override;
the default follows CMAKE_INSTALL_PREFIX/share/lvgl-console.
# lvgl-console
# lvgl-console

## External keyboard demo

Settings contains a labeled single-line text field (128 characters maximum).
Tap it and type on an external keyboard. No on-screen keyboard is created or
shown on focus. Text is for demonstration only and is not saved.

At startup the first `/dev/input/by-id/*-event-kbd` device is opened, falling
back to `/dev/input/keyboard`. Override this with
`LV_LINUX_KEYBOARD_DEVICE=/dev/input/eventN`. Connect the keyboard before
starting the app. The demo uses a US layout with Shift, Caps Lock, Backspace,
Delete, Left/Right, Home and End; international layouts and hotplug are not
implemented. Touch input continues to use `LV_LINUX_EVDEV_DEVICE`.

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
