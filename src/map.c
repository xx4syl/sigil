#include "map.h"

#include <string.h>

#include "gco.h"

#define EDGE 3.0 / 4.0

#define TOMB NUM(0xDEAD)

#define GROW(old) ((old) < 0x10 ? 0x10 : (old) * 2)

typedef struct pair {
    Value k, v;
} Pair;

void sil__initmap(Map *m) {
    m->pairs = NULL;
    m->cap = m->len = m->tombs = 0;
}

void sil__markmap(Map *m) {
    for (int i = 0; i < m->cap; i++) {
        if (!ISUNDEFINED(m->pairs[i].k)) {
            sil__markvalue(m->pairs[i].k);
            sil__markvalue(m->pairs[i].v);
        }
    }
}

void sil__freemap(Sil *sil, Map *m) {
    FREEARRAY(m->pairs, Pair, m->cap);
}

static Pair *allocpairs(Sil *sil, int cap) {
    Pair *pairs = ALLOCARRAY(Pair, cap);
    for (int i = 0; i < cap; i++) pairs[i].k = pairs[i].v = UNDEFINED;
    return pairs;
}

static Pair *findpair(Pair *pairs, int cap, Value k) {
    uint32_t hash = ISSTR(k) ? ASSTR(k)->hash : VALHASH(k);

    Pair *tomb = NULL;
    for (int i = hash & (cap - 1);; i = (i + 1) & (cap - 1)) {
        Pair *pair = &pairs[i];
        if (ISUNDEFINED(pair->k)) {
            if (ISUNDEFINED(pair->v)) return tomb != NULL ? tomb : pair;
            else if (tomb == NULL) tomb = pair;
        } else if (VALEQ(pair->k, k)) {
            return pair;
        }
    }
}

static void growpairs(Sil *sil, Map *m) {
    int newcap = GROW(m->cap);
    Pair *newpairs = allocpairs(sil, newcap);

    for (int i = 0; i < m->cap; i++) {
        if (!ISUNDEFINED(m->pairs[i].k)) {
            Pair *pair = findpair(newpairs, newcap, m->pairs[i].k);
            pair->k = m->pairs[i].k;
            pair->v = m->pairs[i].v;
        }
    }

    FREEARRAY(m->pairs, Pair, m->cap);
    m->pairs = newpairs;
    m->cap = newcap;
    m->tombs = 0;
}

bool sil__mapstore(Sil *sil, Map *m, Value k, Value v) {
    if (m->len + m->tombs >= EDGE * m->cap) growpairs(sil, m);

    Pair *pair = findpair(m->pairs, m->cap, k);
    bool isnew = ISUNDEFINED(pair->k);
    if (isnew) {
        pair->k = k;
        m->len++;
        if (!ISUNDEFINED(pair->v)) m->tombs--;
    }
    pair->v = v;
    return !isnew;
}

bool sil__mapload(Map *m, Value k, Value *v) {
    if (m->len == 0) return false;

    Pair *p = findpair(m->pairs, m->cap, k);
    if (ISUNDEFINED(p->k)) return false;

    *v = p->v;
    return true;
}

bool sil__mapdelete(Map *m, Value k) {
    if (m->len == 0) return false;

    Pair *pair = findpair(m->pairs, m->cap, k);
    if (ISUNDEFINED(pair->k)) return false;

    pair->k = UNDEFINED;
    pair->v = TOMB;
    m->len--;
    m->tombs++;
    return true;
}

Str *sil__maploadstr(Map *m, const char *chars, int len, uint32_t hash) {
    if (m->len == 0) return NULL;

    int cap = m->cap;
    for (int i = hash & (cap - 1);; i = (i + 1) & (cap - 1)) {
        Pair *pair = &m->pairs[i];
        if (ISUNDEFINED(pair->k)) {
            if (ISUNDEFINED(pair->v)) return NULL;
        } else {
            Str *str = ASSTR(pair->k);
            if (str->len == len && str->hash == hash &&
                memcmp(str->chars, chars, len) == 0) {
                return str;
            }
        }
    }
}

bool sil__mapnext(Map *m, int *iter, Value *k, Value *v) {
    int i = *iter;
    if (i < 0) return false;
    while (i < m->cap) {
        Pair *pair = &m->pairs[i++];
        if (!ISUNDEFINED(pair->k)) {
            if (k != NULL) *k = pair->k;
            if (v != NULL) *v = pair->v;
            *iter = i;
            return true;
        }
    }
    *iter = -1;
    return false;
}
