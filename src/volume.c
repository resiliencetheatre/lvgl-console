#include "volume.h"
#include <alsa/asoundlib.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static bool usable(snd_mixer_elem_t *e, bool capture)
{
    return snd_mixer_selem_is_active(e) && (capture ? snd_mixer_selem_has_capture_volume(e) : snd_mixer_selem_has_playback_volume(e));
}
static int open_control(bool capture, snd_mixer_t **mixer, snd_mixer_elem_t **element)
{
    const char *card = getenv("LVGL_ALSA_CARD");
    if (!card || !*card) card = "hw:Audio";
    int rc = snd_mixer_open(mixer, 0);
    if (rc < 0) return rc;
    if ((rc = snd_mixer_attach(*mixer, card)) < 0 ||
        (rc = snd_mixer_selem_register(*mixer, NULL, NULL)) < 0 ||
        (rc = snd_mixer_load(*mixer)) < 0) goto fail;
    const char *requested = getenv(capture ? "LVGL_ALSA_CAPTURE_CONTROL" : "LVGL_ALSA_PLAYBACK_CONTROL");
    static const char *capture_names[] = {"Headset", "Mic", "Capture"};
    static const char *playback_names[] = {"Headset", "Speaker", "Headphone", "PCM", "Master"};
    const char **names = capture ? capture_names : playback_names;
    unsigned count = capture ? sizeof(capture_names) / sizeof(capture_names[0]) :
                               sizeof(playback_names) / sizeof(playback_names[0]);
    for (unsigned i = 0; i < (requested && *requested ? 1 : count); ++i) {
        const char *name = requested && *requested ? requested : names[i];
        for (snd_mixer_elem_t *e = snd_mixer_first_elem(*mixer); e; e = snd_mixer_elem_next(e)) {
            if (usable(e, capture) && !strcmp(snd_mixer_selem_get_name(e), name) && snd_mixer_selem_get_index(e) == 0) {
                *element = e;
                return 0;
            }
        }
    }
    rc = -ENOENT;
fail:
    snd_mixer_close(*mixer);
    *mixer = NULL;
    return rc;
}
static int access_volume(bool capture, int *percent, bool write)
{
    snd_mixer_t *mixer = NULL;
    snd_mixer_elem_t *element = NULL;
    int rc = open_control(capture, &mixer, &element);
    if (rc < 0) return rc;
    long min, max, value;
    rc = capture ? snd_mixer_selem_get_capture_volume_range(element, &min, &max) :
                   snd_mixer_selem_get_playback_volume_range(element, &min, &max);
    if (rc < 0) goto done;
    if (max <= min) { rc = -ERANGE; goto done; }
    if (write) {
        if (*percent < 0 || *percent > 100) { rc = -EINVAL; goto done; }
        value = min + (long)((max-min) * (double)*percent / 100.0 + 0.5);
        rc = capture ? snd_mixer_selem_set_capture_volume_all(element, value) :
                       snd_mixer_selem_set_playback_volume_all(element, value);
    } else {
        rc = -ENOENT;
        for (int channel = 0; channel <= SND_MIXER_SCHN_LAST; ++channel) {
            if (!(capture ? snd_mixer_selem_has_capture_channel(element, channel) :
                            snd_mixer_selem_has_playback_channel(element, channel))) continue;
            rc = capture ? snd_mixer_selem_get_capture_volume(element, channel, &value) :
                           snd_mixer_selem_get_playback_volume(element, channel, &value);
            if (!rc) *percent = (int)((value-min) * 100.0 / (max-min) + 0.5);
            break;
        }
    }
done:
    snd_mixer_close(mixer);
    return rc;
}
int app_volume_get(bool capture, int *percent) { return access_volume(capture, percent, false); }
int app_volume_set(bool capture, int percent) { return access_volume(capture, &percent, true); }
