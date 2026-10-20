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

The package uses SITE_METHOD = local until the application moves to Git.
After editing local sources, run `make -C buildroot lvgl-console-dirclean`
and `make -C buildroot lvgl-console` to copy and rebuild them. Run the normal
Buildroot build to regenerate the filesystem image.

The package installs /usr/bin/lvgl-console and
/usr/share/lvgl-console/link.png. It does not install an autostart service.
Stop any existing framebuffer UI (for example lvgl-com) before starting it:

```sh
LV_LINUX_FBDEV_DEVICE=/dev/fb0 \
LV_LINUX_EVDEV_DEVICE=/dev/input/touchscreen \
    /usr/bin/lvgl-console
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
