#ifndef sil_core_h
#define sil_core_h
#include "cmn.h"
//

#include <setjmp.h>

#include "cpl.h"
#include "gco.h"

#define TEMPSIZE 4
#define MAXFRAMES 1000
#define CATCHPOOLSIZE 0x20

typedef struct frame {
    union {
        struct {
            Code *ip;
        } native;
        struct {
            int slots;
        } c;
    } as;
    Func *fn;
    Value *slots;
    int ret;
} Frame;

struct sil_core {
    Compiler compiler;

    Frame frarray[MAXFRAMES];
    Frame *frame;

    struct {
        Value *start, *end;
    } array;

    Nonlocal *opennls;

    Map glob, strpool;

    struct {
        Doc *str;
    } klss;

    struct {
        SilHandle *handles;
        Value tmp[TEMPSIZE];
        Value *tmpcur;
        Gco *gcos;
        size_t allocsize, trigger;
        SilRealloc realloc;
    } gc;

    jmp_buf errjmpbuf;
};

void sil__clrtmp(Sil *sil);
void sil__pushtmp(Sil *sil, Value value);
Value sil__poptmp(Sil *sil);
Value sil__peektmp(Sil *sil, int i);

#endif  // sil_core_h
