#include "volume.h"
#include <alsa/asoundlib.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static const char *expected_card;
static unsigned attached;
int __wrap_snd_mixer_attach(snd_mixer_t *mixer, const char *name)
{
    (void)mixer;
    if (strcmp(name, expected_card)) abort();
    ++attached;
    return -ENODEV; /* Exercise routing without requiring physical hardware. */
}
static void check(const char *device, const char *expected)
{
    expected_card = expected;
    if (app_volume_select_device(device)) abort();
    unsigned before = attached;
    int percent;
    if (app_volume_get(true, &percent) != -ENODEV ||
        app_volume_get(false, &percent) != -ENODEV ||
        app_volume_set(true, 50) != -ENODEV ||
        app_volume_set(false, 75) != -ENODEV || attached != before + 4) abort();
}
int main(void)
{
    setenv("LVGL_ALSA_CARD", "hw:stale-override", 1);
    check("plughw:CARD=Audio,DEV=0", "hw:Audio");
    check("plughw:CARD=UACDemoV10,DEV=0", "hw:UACDemoV10");
    check("plughw:CARD=Audio,DEV=0", "hw:Audio");
    check("hw:1,0", "hw:1");
    check("", "hw:stale-override");
    unsetenv("LVGL_ALSA_CARD");
    check(NULL, "hw:Audio");
    unsigned before = attached;
    int percent;
    if (app_volume_select_device("default") != -EINVAL ||
        app_volume_get(true, &percent) != -EINVAL || attached != before) abort();
    puts("Volume routing, profile switching, overrides and invalid devices passed");
    return 0;
}
