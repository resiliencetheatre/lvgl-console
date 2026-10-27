# lvgl-console

Small LVGL 9.6.0 console for the Raspberry Pi 4 Linux framebuffer. Uses the
fbdev/evdev setup from lvgl-video and the logo/tab conventions from lvgl-com.
The first tab, PTT, opens at startup and shows link.png horizontally centered in
the lower third above the status bar, over a dim-green world map excluding
Antarctica. The map is drawn as LVGL lines in a flat equirectangular projection,
without a new image decoder. `WORLD_MAP_COLOR` in `src/ui.c` controls its color.
Decorative bright-red dots pulse in connected pairs across the US, Europe,
Japan, Australia, Africa, South America, Canada, Russia, and South Africa.
A shared endpoint remains bright while the old endpoint and red connecting line
fade down and the next endpoint/link fade up over four seconds. A faint preview
keeps the previous/current/next locations visible. Each pair holds for a randomly
chosen two to six seconds before transitioning. New destinations are random,
excluding the current chain's three locations. These markers do not represent
live network activity. Timing constants are at the top of `src/ui.c`.
The 100 ms animation timer invalidates the affected link/marker areas and pauses
while another tab is selected or the screen is blanked.
The compiled outline data in `src/world_map_data.h` is simplified from Natural
Earth's public-domain 1:110m country dataset. `tools/generate_world_map.py`
documents the source and regenerates the header from its GeoJSON.
Messaging connects to the utxtrelay console connector; Settings edits PTT identities and
screen-saver preferences.

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

Requires CMake, C/C++ compilers, pthreads, and libpng/ALSA development files.

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
polling pauses while the screen saver has blanked the display. Files are reopened on each read
for atomic replacement. Enable PocketTerm `dpinger.service` to supply reports.

The speech box displays `PTT idle`, `TX <txid>`, or `RX <talker>` (both when
applicable); long text scrolls within the box. Its background is bright red during
TX, bright blue during RX, and black when idle or unavailable. TX takes priority
when both are active. `/tmp/udpptt.sock` is a command-only
socket, so the UI checks it with a nonblocking connect without sending commands
and reads activity from `/run/udpptt/state`. Start the client with
`--ptt-socket /tmp/udpptt.sock --state-file /run/udpptt/state` as in the PocketTerm
service. Missing/unreachable socket shows `PTT offline`; missing or invalid state
shows `PTT unavailable`. The state file changes on activity transitions, not as
a periodic heartbeat, so idle state does not expire. Socket availability does
not prove server connectivity or client responsiveness.

Settings places the Screen saver checkbox below PTT Operation mode under a
System settings heading. Audio device below Screen saver uses the same radio-style
selection as PTT Operation mode: **USB HF** or **USB Monofone**. Selection immediately
copies `/etc/default/udpptt.usb-hf` or `/etc/default/udpptt.usb-monofone` over
`/etc/default/udpptt` and restarts `udpptt.service` asynchronously. USB HF selects
`plughw:CARD=Audio,DEV=0`; USB Monofone selects `plughw:CARD=UACDemoV10,DEV=0`
and enables `--usbptt`. Configure the server, credentials and IDs in **both profiles**
before switching: the entire profile replaces the active configuration, including
screen saver state, and unsaved field edits are discarded. Save edits only the active
configuration; update the profile files too if those edits should survive switching.
Copy/restart errors appear in the Settings feedback.
The application install includes `/usr/libexec/udpptt-audio-select` (with a `/usr`
install prefix). When updating an existing device, deploy that helper as well as
the binary, both profile files, and the updated PocketTerm `udpptt.service`; run
`systemctl daemon-reload` after updating the unit. The application install also includes both profile templates in `/etc/default`;
configure their server and credentials for the device. The updated service unit
comes from the PocketTerm root filesystem overlay, which requires a full image
build to refresh.

Network status below it lists all interfaces (including
loopback), with one line per assigned IPv4 or IPv6 address and an UP/DOWN status.
Interfaces without addresses show `No IP`; UP requires both enabled and running
interface flags. The list refreshes every two seconds, pausing during screen
saver blanking. Save is at the bottom, with feedback below it. The
input/output volume rollers appear side by side above PTT Operation mode.
Each roller shows three rows with
options from 0% through 100% in 5% steps, without wrapping. Selecting a value
applies it immediately. The initial hardware level is displayed at the nearest
5% option without changing the volume until selection. Scroll chaining and
gesture bubbling are disabled on the rollers to reduce accidental tab changes.
Capture and playback use separate ALSA controls, with all channels
set to the selected level. Mute switches are left unchanged. These are live
mixer settings, not saved by the PTT identity Save button. Existing ALSA state
restoration determines whether mixer levels survive reboot.

The mixer follows `UDPPTT_ALSA_DEVICE`: USB HF uses `hw:Audio`, and USB Monofone
uses `hw:UACDemoV10`. Both rollers reload hardware levels after profile switching;
changing a roller immediately writes to the selected card. While switching,
volume controls are disabled. Missing controls display N/A rather than 0%.
An explicit PTT device takes precedence over `LVGL_ALSA_CARD`, including old
launcher overrides. Only when no PTT device is configured does the mixer fall
back to `LVGL_ALSA_CARD`, then `hw:Audio`. Set
`LVGL_ALSA_CAPTURE_CONTROL` and `LVGL_ALSA_PLAYBACK_CONTROL` to exact simple
control names when necessary (index 0). Auto-selection tries Headset/Mic/Capture
for input and Headset/Speaker/Headphone/PCM/Master for output, using the first compatible
control. Missing devices/controls show N/A with disabled rollers; switching
profiles or restarting the application retries discovery. Run
`amixer -c Audio scontents` on the target to inspect names, ranges, and switches.
The application now links alsa-lib, including its mixer API.

Settings scrolls vertically. Beneath the narrow Input/Output rollers, the PTT
Operation mode section offers mutually exclusive WAN, DARK FIBER, DATA DIODE
(TX), and DATA DIODE (RX) choices. This selection is a UI placeholder only: it
starts at WAN each launch and does not change client flags or persist a mode.
The WAN address field reads `UDPPTT_SERVER` from `/etc/default/udpptt`; Save
updates it together with the identity settings. IP addresses and hostnames are
supported, and a blank value remains allowed for an unconfigured device. Restart
`udpptt.service` to apply a changed server address.

Settings loads `/etc/default/udpptt`. The three fields edit `UDPPTT_TXID`
(1–31 characters), `UDPPTT_DPI_ID` (1–63 characters), and `UDPPTT_PREAMBLE_ID`
(exactly five uppercase A–Z letters). IDs accept letters, digits, dots,
underscores, and hyphens. Touch a field to focus it and use the physical
keyboard; Tab advances through the focus group. No virtual keyboard is opened.

**Poweroff**, to the left of Save, requests an orderly system shutdown with
`/bin/systemctl --no-block poweroff`. It does not save pending settings. The
button is disabled after submission; failures are shown below the buttons and
allow retry. The command runs asynchronously so the UI stays responsive, and
systemd stops the console launcher during shutdown to restore the VT. The
PocketTerm service runs as root, which permits the poweroff request.

**Save** validates the fields and atomically updates these settings while
preserving other entries and comments, including the audio device and key.
The existing file's permissions and (when running as root) ownership are
preserved. Errors appear below Save. The file must already exist and the process
must have permission to replace it in `/etc/default`. No shell commands are run
to load or save configuration. Restart `udpptt.service` to apply identity changes;
saving does not interrupt an active call or regenerate DPI keys. Install the
updated PocketTerm service, which reads all three identity environment variables.

**Screen saver** is saved as `UDPPTT_SCREENSAVER=0` or `1` in the
same file. After Save, this preference takes effect immediately. When enabled,
60 seconds without LVGL input blanks the framebuffer. Touch or keyboard activity
unblanks it and forces a full screen/overlay redraw. The wake input is also
delivered to the UI. `SCREENSAVER_IDLE_MS` in `src/ui.c` sets the timeout. HDMI
audio may be interrupted while the display is blanked. The default is off.

Messaging contains a vertically scrollable history and a fixed single-line
entry field backed by `utxtrelay-console-client`. Enter submits a room message;
`/msg NAME text` submits a direct message, `/who` refreshes participants,
`/secret STRING` enables shared-secret encryption, `/secret off` clears it,
and `/status` (or `/secret` alone) requests state. Secret entry is masked and
cleared on submission; secrets are never added to chat history. The entry waits
for a command result before accepting another submission. Failed ordinary
messages stay in the entry; unknown outcomes are reported without automatic retry.

The connector defaults to `/run/utxtrelay/console.sock`; override with
`UTXTRELAY_SOCKET`. Participants appear below the entry. Connection diagnostics
are hidden during normal use; `/status` or `/secret` displays room, nickname,
relay status, encryption mode, and byte limit in the chat history. Send errors
also appear in the history. Socket polling is nonblocking and continues on other
tabs and while the display is blanked. On reconnection the connector replays up
to 100 retained events with IDs to avoid duplicate display. Restarting the
connector loses its history and encryption key; restarting the UI alone does not.

Input is limited to 512 characters; the connector additionally enforces the
900-byte UTF-8 clear-message limit and a smaller encrypted limit. The UI keeps
100 history rows in memory, with wrapped text. A local message marked submitted
means the connector wrote it to UDP, not that any recipient received it.
No on-screen keyboard is created and no chat history is stored on disk.

The PocketTerm overlay enables `utxtrelay-console-client.service` before
`lvgl-console.service`, which runs the console launcher. The former
`lvgl-com.service` name is an alias of this service. Configure the server, port,
unique nickname and room in `/etc/default/utxtrelay`, then restart the connector.
The default server `127.0.0.1` is a placeholder, with no relay server autostart.
The UI package alone still installs no service; these units are overlay-specific.

By default all `/dev/input/event*` devices with ordinary typing keys are opened.
The internal Pico keyboard and an external USB keyboard can type into the same
focused field simultaneously. Each keyboard has independent Shift/Caps Lock
state. Mouse and media-only interfaces are excluded by their key capabilities;
mixed keyboard/control devices such as the Pico are supported.

Devices are rescanned once per second, so USB keyboards can be connected,
unplugged, and reconnected without restarting the app. Unplugging clears held
keys. Set `LV_LINUX_KEYBOARD_DEVICE` to a single event device or stable by-id
symlink to restrict input to that keyboard; that path is also retried after
removal. Leave the variable unset to use both keyboards.

The demo uses a US layout with Shift, Caps Lock, Backspace, Delete, Home and End;
international layouts are not implemented. Touch input continues to use
`LV_LINUX_EVDEV_DEVICE`.

Left/Right moves the cursor in the text entry; Home/End moves to the start/end.
Arrow keys no longer switch tabs. Select PTT / Messaging / Settings by touch.
The focused entry does not automatically scroll away from PTT at startup.

For the Pico keyboard shown as `/dev/input/event1`, start with:

```sh
LV_LINUX_KEYBOARD_DEVICE=/dev/input/event1 /usr/bin/lvgl-console-launcher
```

Event numbers can change across boots; prefer automatic capability discovery
or, for a single keyboard override, set a stable `/dev/input/by-id/*-event-kbd` path when available.

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

The **Audio PTT preamble** checkbox under PTT Operation mode saves
`UDPPTT_PREAMBLE_ENABLED=0` or `1` and immediately restarts `udpptt.service`.
No Save click is needed. Enabled passes `--preamble-id` with the saved preamble
ID; disabled omits both the option and its value. Older configurations default
to enabled. Audio profile switching reloads this setting from the profile.
Deploy the updated `udpptt-prepare` and `udpptt.service` together with the UI;
`make update_pocketterm` from the workspace does this and reloads systemd.

Network status snapshots are sorted so interface/address enumeration order does
not trigger redraws. Refreshes are deferred while Settings is scrolling and for
500 ms afterward, and while the address list is off-screen. Transient read
failures retain the last snapshot instead of collapsing and expanding the list.
