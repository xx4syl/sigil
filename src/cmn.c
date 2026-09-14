#include "cmn.h"

#include <locale.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "core.h"

void *sil__realloc(Sil *sil, void *mem, size_t oldsize, size_t newsize) {
    if (newsize > oldsize) {
        sil->gc.allocsize += newsize - oldsize;
#ifdef GC_STRESS
        sil_gc(sil);
#else
        if (sil->gc.allocsize >= sil->gc.trigger) sil_gc(sil);
#endif
    } else {
        sil->gc.allocsize -= oldsize - newsize;
    }

    return sil->gc.realloc(mem, newsize);
}

uint32_t sil__fnv1a(const void *data, size_t size) {
    const byte *buf = data;
    uint32_t hval = 0x811c9dc5u;

    for (size_t i = 0; i < size; i++) {
        hval ^= (uint32_t)buf[i];
        hval *= 0x01000193u;
    }

    return hval;
}

char *sil__strdupl(Sil *sil, const char *chars, int len) {
    char *dup = ALLOCARRAY(char, len + 1);
    memcpy(dup, chars, len);
    dup[len] = '\0';
    return dup;
}

void *sil__dupl(Sil *sil, const void *data, size_t size) {
    void *dup = ALLOCARRAY(byte, size);
    memcpy(dup, data, size);
    return dup;
}

int sil__ctoi(char c, int base) {
    base--;
    if (base < 10) {
        if ('0' <= c && c <= '0' + base) return c - '0';
    } else {
        if ('0' <= c && c <= '9') return c - '0';
        if (('a' <= c && c <= 'a' + base)) return c - 'a' + 10;
        if (('A' <= c && c <= 'A' + base)) return c - 'A' + 10;
    }
    return -1;
}

bool sil__isalpha(char c) {
    return 'a' <= c && c <= 'z' || 'A' <= c && c <= 'Z' || c == '_';
}

bool sil__isdigit(char c) {
    return sil__ctoi(c, 10) >= 0;
}

static bool ishexdigit(char c) {
    return sil__ctoi(c, 0x10) >= 0;
}

static void readsepdigits(const char **cur, bool (*is)(char), bool allowsep) {
    const char *c = *cur;
    while (is(*c) || (allowsep && *c == NUMSEP)) {
        allowsep = !(*c == NUMSEP);
        c++;
    }
    *cur = c - !allowsep;
}

int sil__readnum(const char *src, int *skip) {
    const char *cur = src;

    int skipped = 0;
    while (!sil__isdigit(cur[skipped]) && cur[skipped] != '\0') skipped++;
    if (skip != NULL)
        *skip = skipped - (skipped != 0 && (cur[skipped - 1] == '+' ||
                                            cur[skipped - 1] == '-'));
    cur += skipped;

    bool ishex = *cur == '0' && (cur[1] == 'x' || cur[1] == 'X');
    bool (*is)(char) = ishex ? ishexdigit : sil__isdigit;

    if (ishex) cur += 2;

    if (!is(*cur)) return skipped;

    readsepdigits(&cur, is, !ishex);

    if (*cur == '.' && is(cur[1])) {
        cur += 2;
        readsepdigits(&cur, is, true);
    }

    char expchar = ishex ? 'p' : 'e';
    char expupchar = ishex ? 'P' : 'E';

    const char *rem = cur;
    if (*cur == expchar || *cur == expupchar) {
        cur++;
        if (*cur == '+' || *cur == '-') cur++;
        if (!sil__isdigit(*cur)) return rem - src;
        readsepdigits(&cur, sil__isdigit, false);
    }

    return cur - src;
}

double sil__atofl(Sil *sil, const char *chars, int len) {
    size_t bufsize = sizeof(char) * len + 1;
    char *buf = ALLOC(bufsize);
    char dp = *localeconv()->decimal_point;
    int cur = 0;
    for (int i = 0; i < len; i++) {
        if (chars[i] == '.') buf[cur++] = dp;
        else if (chars[i] != NUMSEP) buf[cur++] = chars[i];
    }
    buf[cur] = '\0';
    double num = atof(buf);
    FREE(buf, bufsize);
    return num;
}

int sil__dtoi(double d) {
    if (!isfinite(d)) return isnan(d) ? 0 : d < 0 ? INT_MIN : INT_MAX;
    return d > (double)INT_MAX   ? INT_MAX
           : d < (double)INT_MIN ? INT_MIN
                                 : (int)d;
}
