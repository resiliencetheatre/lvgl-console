#include "versions.h"
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>
#ifdef __GLIBC__
#include <gnu/libc-version.h>
#endif

void app_versions_read(char *text, size_t size)
{
    struct utsname kernel;
    const int have_kernel = uname(&kernel) == 0;
    char gst[64] = "unavailable", ssl[160] = "unavailable";
    /* Optional runtime libraries: report the installed versions without adding
     * mandatory GStreamer/OpenSSL dependencies or invoking external commands. */
    void *library = dlopen("libgstreamer-1.0.so.0", RTLD_LAZY | RTLD_LOCAL);
    if (library) {
        void (*version)(unsigned *, unsigned *, unsigned *, unsigned *) = NULL;
        void *symbol = dlsym(library, "gst_version");
        _Static_assert(sizeof(version) == sizeof(symbol), "POSIX function pointer size");
        memcpy(&version, &symbol, sizeof(version));
        if (version) {
            unsigned major, minor, micro, nano;
            version(&major, &minor, &micro, &nano);
            if (nano) snprintf(gst, sizeof(gst), "%u.%u.%u.%u", major, minor, micro, nano);
            else snprintf(gst, sizeof(gst), "%u.%u.%u", major, minor, micro);
        }
        dlclose(library);
    }
    const char *crypto[] = {"libcrypto.so.3", "libcrypto.so.1.1", "libcrypto.so"};
    for (unsigned i = 0; i < sizeof(crypto)/sizeof(crypto[0]); ++i) {
        library = dlopen(crypto[i], RTLD_LAZY | RTLD_LOCAL);
        if (!library) continue;
        const char *(*version)(int) = NULL;
        void *symbol = dlsym(library, "OpenSSL_version");
        _Static_assert(sizeof(version) == sizeof(symbol), "POSIX function pointer size");
        memcpy(&version, &symbol, sizeof(version));
        const char *value = version ? version(0) : NULL;
        if (value) snprintf(ssl, sizeof(ssl), "%s", value);
        dlclose(library);
        if (value) break;
    }
#ifdef __GLIBC__
    const char *libc = gnu_get_libc_version();
    const char *libc_name = "glibc";
#else
    const char *libc = "version unavailable";
    const char *libc_name = "libc";
#endif
    snprintf(text, size, "Kernel: %s\nKernel build: %s\n%s: %s\nGStreamer: %s\nOpenSSL: %s",
             have_kernel ? kernel.release : "unavailable",
             have_kernel ? kernel.version : "unavailable", libc_name, libc, gst,
             strncmp(ssl, "OpenSSL ", 8) == 0 ? ssl + 8 : ssl);
}
