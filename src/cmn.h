#ifndef sil_cmn_h
#define sil_cmn_h

#include <stdint.h>

#include "../incl/sigil.h"

// #define AUTOSEMI
// #define ALTKEYWORDS
// #define LOOPELSE
// #define NANBOXING
// #define SWITCHEDGOTO
// #define DEBUG

// #define DEBUG_GC
// #define DEBUG_LEX
// #define DEBUG_COMPILE
// #define DEBUG_EXECUTE
// #define DEBUG_ASSERT

// #define GC_STRESS

#ifdef DEBUG
    #define DEBUG_GC
    #define DEBUG_LEX
    #define DEBUG_COMPILE
    #define DEBUG_EXECUTE
    #define DEBUG_ASSERT
#endif

#ifdef DEBUG_ASSERT
    #include <stdio.h>
    #include <stdlib.h>

    #define ASSERT(cond, msg)                                                  \
        do {                                                                   \
            if (!(cond)) {                                                     \
                fprintf(                                                       \
                    stderr, "assertion failed at func %s(%s:%d): %s\n",        \
                    __func__, __FILE__, __LINE__, msg                          \
                );                                                             \
                abort();                                                       \
            }                                                                  \
        } while (false)

    #define UNREACHABLE ASSERT(false, "reached unreachable")
#else
    #define ASSERT(cond, msg)                                                  \
        do {                                                                   \
        } while (false)

    #if defined(__GNUC__) || defined(__clang__)
        #define UNREACHABLE __builtin_unreachable()
    #elif defined(_MSC_VER)
        #define UNREACHABLE __assume(0)
    #else
        #include <stdlib.h>

        #define UNREACHABLE abort()
    #endif
#endif

#define TODO ASSERT(false, "todo")

#define REALLOC(mem, oldsize, newsize) sil__realloc(sil, mem, oldsize, newsize)
#define ALLOC(size) REALLOC(NULL, 0, size)
#define FREE(mem, size) REALLOC(mem, size, 0)

#define REALLOCARRAY(mem, type, oldlen, newlen)                                \
    REALLOC(mem, sizeof(type) * (oldlen), sizeof(type) * (newlen))
#define ALLOCARRAY(type, len) ALLOC(sizeof(type) * (len))
#define FREEARRAY(mem, type, len) FREE(mem, sizeof(type) * (len))

#define FLEXSIZE(type, flextype, flexlen)                                      \
    (sizeof(type) + sizeof(flextype) * (flexlen))

#define REALLOCFLEX(mem, type, flextype, oldlen, newlen)                       \
    REALLOC(                                                                   \
        mem, FLEXSIZE(type, flextype, oldlen),                                 \
        FLEXSIZE(type, flextype, newlen)                                       \
    )
#define ALLOCFLEX(type, flextype, flexlen)                                     \
    ALLOC(FLEXSIZE(type, flextype, flexlen))
#define FREEFLEX(mem, type, flextype, flexlen)                                 \
    FREE(mem, FLEXSIZE(type, flextype, flexlen))

#define LITLEN(lit) (sizeof(lit) - 1)

#define NUMSEP '\''

typedef unsigned char byte;

void *sil__realloc(Sil *sil, void *mem, size_t oldsize, size_t newsize);
uint32_t sil__fnv1a(const void *data, size_t size);
char *sil__strdupl(Sil *sil, const char *chars, int len);
void *sil__dupl(Sil *sil, const void *data, size_t size);
int sil__ctoi(char c, int base);
bool sil__isalpha(char c);
bool sil__isdigit(char c);
int sil__readnum(const char *src, int *skip);
double sil__atofl(Sil *sil, const char *chars, int len);

#endif  // sil_cmn_h
