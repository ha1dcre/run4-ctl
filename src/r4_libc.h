/* SPDX-License-Identifier: GPL-3.0-only
 * The C library RUN4 CTL's device code uses, in one place. The host builds (the simulator and the browser emulator:
 * PLAT_HOST; the native tests: R4_NATIVE) have the system's. The FM-1 has none: fm1-base gives memset, memcpy and
 * memcmp (bsp/chip/libc.c, in the unity build before the app), and this file the rest the firmware calls: memmove,
 * strlen, strcmp, strncmp, strcpy. Each is a plain loop with no state, so any context may call it (the audio
 * interrupt as the UI). None is PLAT_RAM_CODE: no RAM code calls them (base/include/plat.h). What only the simulator
 * runs (app_host_cmd / app_host_query: atoi, strtoul, strchr, strstr) stays under PLAT_HOST and uses the system's. */
#pragma once
#include <stddef.h>
#if defined PLAT_HOST || defined R4_NATIVE
#include <string.h>
#include <stdlib.h>
#else
/* fm1-base's (bsp/chip/libc.c), declared as defined there: for a unit outside the unity build */
void *memset(void *d, int c, unsigned n);
void *memcpy(void *d, const void *s, unsigned n);
int memcmp(const void *a, const void *b, unsigned n);

static void *r4_memmove(void *d, const void *s, size_t n)
{
    unsigned char *p = d;
    const unsigned char *q = s;
    if (p == q || !n)
        return d;
    if (p < q) {                         /* forward: each byte is read before the copy reaches it */
        while (n--)
            *p++ = *q++;
    } else {                             /* the destination after the source: from the end */
        p += n;
        q += n;
        while (n--)
            *--p = *--q;
    }
    return d;
}
static size_t r4_strlen(const char *s)
{
    const char *e = s;
    while (*e)
        e++;
    return (size_t)(e - s);
}
static int r4_strcmp(const char *a, const char *b)
{
    const unsigned char *p = (const unsigned char *)a, *q = (const unsigned char *)b;
    while (*p && *p == *q)
        p++, q++;
    return (int)*p - (int)*q;
}
static int r4_strncmp(const char *a, const char *b, size_t n)
{
    const unsigned char *p = (const unsigned char *)a, *q = (const unsigned char *)b;
    for (; n; n--, p++, q++)
        if (*p != *q || !*p)
            return (int)*p - (int)*q;
    return 0;
}
static char *r4_strcpy(char *d, const char *s)
{
    char *p = d;
    while ((*p++ = *s++) != 0)
        ;
    return d;
}
#define memmove r4_memmove
#define strlen r4_strlen
#define strcmp r4_strcmp
#define strncmp r4_strncmp
#define strcpy r4_strcpy
#endif
