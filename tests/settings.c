#include "settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main(void)
{
    char path[] = "/tmp/lvgl-settings-XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) return 1;
    FILE *f = fdopen(fd, "w");
    if (!f) return 1;
    fputs("# preserve this comment\nUDPPTT_SERVER = \"192.0.2.1\"\nUDPPTT_KEY='test key'\nUDPPTT_TXID=old\nUDPPTT_TXID=\"latest\"\nEXTRA=value\n", f);
    fclose(f);
    app_settings s;
    if (app_settings_load(path, &s) || strcmp(s.txid, "latest") || strcmp(s.preamble, "POCKT") ||
        strcmp(s.server, "192.0.2.1")) return 1;
    strcpy(s.server, "198.51.100.10");
    strcpy(s.txid, "field-unit"); strcpy(s.dpi_id, "unit-2"); strcpy(s.preamble, "FIELD"); s.screensaver = true;
    if (app_settings_save(path, &s)) return 1;
    app_settings readback;
    if (app_settings_load(path, &readback) || strcmp(readback.txid, s.txid) ||
        strcmp(readback.dpi_id, s.dpi_id) || strcmp(readback.preamble, s.preamble) ||
        strcmp(readback.server, s.server) || !readback.screensaver) return 1;
    f = fopen(path, "r");
    char content[2048] = {0};
    fread(content, 1, sizeof(content)-1, f); fclose(f);
    if (!strstr(content, "# preserve this comment\n") || !strstr(content, "UDPPTT_KEY='test key'\n") ||
        !strstr(content, "EXTRA=value\n") || !strstr(content, "UDPPTT_SERVER=198.51.100.10\n") ||
        strstr(content, "UDPPTT_SERVER=192.0.2.1") || strstr(content, "UDPPTT_TXID=old")) return 1;
    struct stat info;
    if (stat(path, &info) || (info.st_mode & 0777) != 0600) return 1;
    strcpy(s.preamble, "bad");
    if (app_settings_save(path, &s) == 0) return 1;
    strcpy(s.preamble, "FIELD"); strcpy(s.dpi_id, "bad id");
    if (app_settings_valid(&s)) return 1;
    unlink(path);
    if (app_settings_save(path, &readback) == 0) return 1;
    return 0;
}
