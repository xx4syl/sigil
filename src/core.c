#include "core.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifdef DEBUG_GC
    #include <time.h>
#endif

#define INITARRAY 0x100
#define INITTRIGGER 0x10000
#define TRIGGERFACTOR 3.14

struct sil_handle {
    Value v;
    SilHandle *prev, *next;
};

static SilHandle *newhandle(Sil *sil, Value v) {
    SilHandle *hndl = ALLOC(sizeof(SilHandle));

    hndl->v = v;
    hndl->next = sil->gc.handles;
    hndl->prev = NULL;

    if (sil->gc.handles != NULL) sil->gc.handles->prev = hndl;
    sil->gc.handles = hndl;

    return hndl;
}

static void freehandle(Sil *sil, SilHandle *hndl) {
    if (hndl->prev != NULL) hndl->prev->next = hndl->next;
    else sil->gc.handles = hndl->next;

    if (hndl->next != NULL) hndl->next->prev = hndl->prev;

    FREE(hndl, sizeof(SilHandle));
}

static Frame *nextframe(Sil *sil) {
    if (sil->frarray + MAXFRAMES - 1 == sil->frame) return NULL;
    return ++sil->frame;
}

static void dropframe(Sil *sil) {
    sil->frame--;
}

static int codeidx(Frame *fr) {
    return fr->as.native.ip - 1 - fr->fn->proto.native->code;
}

static Value *gettop(Sil *sil) {
    ASSERT(sil->frame != NULL, "no frame");

    Frame *fr = sil->frame;
    switch (fr->fn->type) {
        case FUNC_C:
            return fr->slots + fr->as.c.slots;

        case FUNC_NATIVE: {
            int slots = fr->fn->proto.native->slots[codeidx(fr)];
            return fr->slots + slots;
        }

        default:
            UNREACHABLE;
    }
}

static int funcline(Frame *fr) {
    if (fr->fn->type == FUNC_NATIVE)
        return fr->fn->proto.native->lines[codeidx(fr)];
    return -1;
}

static void ensurearray(Sil *sil, Value *fo) {
    if (sil->array.end > fo) return;

    int oldcap = sil->array.end - sil->array.start;

    int atleast = fo - sil->array.start;
    int newcap = oldcap;
    do newcap *= 2;
    while (newcap < atleast);

    Value *newarray = REALLOCARRAY(sil->array.start, Value, oldcap, newcap);

    if (sil->array.start != newarray) {
        for (Frame *f = sil->frarray; f <= sil->frame; f++) {
            int offset = f->slots - sil->array.start;
            f->slots = newarray + offset;
        }

        for (Nonlocal *nl = sil->opennls; nl != NULL; nl = nl->next) {
            int offset = nl->value - sil->array.start;
            nl->value = newarray + offset;
        }

        sil->array.start = newarray;
    }

    sil->array.end = sil->array.start + newcap;
}

static inline void sts(Sil *sil, int slot, Value value) {
    sil->frame->slots[slot] = value;
}

static inline Value lds(Sil *sil, int slot) {
    return sil->frame->slots[slot];
}

void sil__clrtmp(Sil *sil) {
    sil->gc.tmpcur = sil->gc.tmp;
}

void sil__pushtmp(Sil *sil, Value value) {
    ASSERT(sil->gc.tmpcur < sil->gc.tmp + TEMPSIZE, "temp overflow");
    *sil->gc.tmpcur++ = value;
}

Value sil__poptmp(Sil *sil) {
    ASSERT(sil->gc.tmpcur > sil->gc.tmp, "temp underflow");
    return *--sil->gc.tmpcur;
}

Value sil__peektmp(Sil *sil, int offs) {
    offs++;
    ASSERT(sil->gc.tmpcur - offs >= sil->gc.tmp && offs > 0, "out of bounds");
    return sil->gc.tmpcur[-offs];
}

static void markarray(Sil *sil) {
    if (sil->array.start == NULL) return;

    Value *top = gettop(sil);
    for (Value *v = sil->array.start; v < top; v++) sil__markvalue(*v);
}

static void markframes(Sil *sil) {
    Frame *last = sil->frame;
    for (Frame *fr = sil->frarray + 1; fr <= last; fr++)
        sil__markgco((Gco *)fr->fn);
}

static void marktmp(Sil *sil) {
    for (Value *v = sil->gc.tmp; v < sil->gc.tmpcur; v++) sil__markvalue(*v);
}

static void markklasses(Sil *sil) {
    if (sil->klss.str != NULL) sil__markgco((Gco *)sil->klss.str);
}

static void markhandles(Sil *sil) {
    for (SilHandle *hndl = sil->gc.handles; hndl != NULL; hndl = hndl->next)
        sil__markvalue(hndl->v);
}

static void mark(Sil *sil) {
    marktmp(sil);
    markklasses(sil);
    markarray(sil);
    markframes(sil);
    markhandles(sil);
    sil__markmap(&sil->glob);
    sil__markcompiler(&sil->compiler);
}

static void sweep(Sil *sil) {
    Gco **o = &sil->gc.gcos;
    while (*o != NULL) {
        if (!(*o)->marked) {
            Gco *tofree = *o;
            *o = tofree->next;
            sil__freegco(sil, tofree);
        } else {
            (*o)->marked = false;
            o = &(*o)->next;
        }
    }
}

static Nonlocal *opennl(Sil *sil, Value *local) {
    Nonlocal *prev = NULL;
    Nonlocal *nl = sil->opennls;
    while (nl != NULL && nl->value > local) {
        prev = nl;
        nl = nl->next;
    }

    if (nl != NULL && nl->value == local) return nl;

    Nonlocal *newnl = sil__newnonlocal(sil, local);
    newnl->next = nl;

    if (prev == NULL) sil->opennls = newnl;
    else prev->next = newnl;

    return newnl;
}

static void closenls(Sil *sil, int count) {
    for (int i = 0; i < count; i++) {
        Nonlocal *nl = sil->opennls;
        sil__closenonlocal(nl);
        sil->opennls = nl->next;
    }
}

static enum sil_result callfunc(
    Sil *sil,
    Func *fn,
    int ret,
    int argc,
    Value *argv
) {
    Frame *frame = nextframe(sil);
    if (frame == NULL) {
        argv[ret] = GCO(STRLIT("stack overflow"));
        return SIL_UNCAUGHT;
    }

    frame->slots = argv;
    frame->ret = ret;
    frame->fn = fn;

    switch (fn->type) {
        case FUNC_C: {
            frame->as.c.slots = argc;

            argv[ret] = VOID;
            enum sil_result res = fn->proto.c(sil);
            dropframe(sil);
            return res;
        }

        case FUNC_NATIVE: {
            frame->as.native.ip = fn->proto.native->code + 1;

            ensurearray(sil, argv + fn->proto.native->maxslots);
            int paramc = fn->proto.native->paramc;
            int append = paramc - argc;
            for (int i = 0; i < append; i++) frame->slots[argc + i] = VOID;

            if (fn->proto.native->vararg) {
                Doc *doc = sil__newdoc(sil, NULL);
                sil__pushtmp(sil, GCO(doc));
                int store = -append;
                for (int i = 0; i < store; i++)
                    sil__docstore(sil, doc, NUM(i), frame->slots[paramc + i]);

                frame->slots[paramc] = GCO(STRLIT("__len"));
                double len = store > 0 ? store : .0;
                sil__docstore(sil, doc, frame->slots[paramc], NUM(len));

                frame->slots[paramc] = GCO(doc);
                sil__clrtmp(sil);
            }

            frame->as.native.ip--;
            return SIL_OK;
        }

        default:
            UNREACHABLE;
    }
}

static Str *fmt(Sil *sil, const char *fmt, ...) {
#define LIM 0x10

    char buf[0x1000];
    char *cur = buf;

    va_list args;
    va_start(args, fmt);

    for (int i = 0; fmt[i] != '\0'; i++) {
        switch (fmt[i]) {
            case '@': {
                Str *str = sil__tostr(sil, va_arg(args, Value));
                for (int i = 0; i < str->len && i < LIM; i++)
                    *cur++ = str->chars[i];
            } break;
            case '$': {
                char *str = va_arg(args, char *);
                for (int i = 0; str[i] != '\0' && i < LIM; i++) *cur++ = str[i];
            } break;
            case '#': {
                int num = va_arg(args, int);
                cur += sprintf(cur, "%d", num);
            } break;
            default: {
                *cur++ = fmt[i];
            } break;
        }
    }

    va_end(args);

    return sil__newstrl(sil, buf, cur - buf);

#undef LIM
}

static Doc *getklass(Sil *sil, Value v) {
    Doc *klass;
    switch (VALTYPE(v)) {
        case VAL_VOID:
        case VAL_BOOL:
        case VAL_NUM:
            return NULL;
        case VAL_GCO: {
            switch (GCOTYPE(ASGCO(v))) {
                case GCO_STR: {
                    return sil->klss.str;
                } break;
                case GCO_DOC: {
                    return ASDOC(v)->klass;
                } break;
                case GCO_FUNC:
                    return NULL;
                default:
                    UNREACHABLE;
            }
        } break;
        default:
            UNREACHABLE;
    }
}

static Value getklassmethod(Sil *sil, Value v, char *name, int namelen) {
    Doc *klass = getklass(sil, v);
    if (klass == NULL) return VOID;

    char buf[0x10] = "__";
    memcpy(buf + 2, name, namelen);
    Value k = GCO(sil__newstrl(sil, buf, 2 + namelen));

    return sil__docload(klass, k);
}

static const char *funcname(Frame *fr) {
    const char *name = fr->fn->name;
    if (name != NULL) return name;
    return "unnamed";
}

static enum sil_result callvalue(
    Sil *sil,
    Value cle,
    int ret,
    int argc,
    Value *argv
) {
    if (ISFUNC(cle)) return callfunc(sil, ASFUNC(cle), ret, argc, argv);

    Value t = sil__typeof(sil, cle);
    sil__pushtmp(sil, t);
    argv[ret] = GCO(
        fmt(sil, "attempt to call @ in func $ at line #", t,
            funcname(sil->frame), funcline(sil->frame))
    );
    sil__clrtmp(sil);
    return SIL_UNCAUGHT;
}

static void closenlstil(Sil *sil, Value *til) {
    Nonlocal *nl = sil->opennls;
    int i = 0;
    while (nl != NULL && nl->value > til) {
        i++;
        nl = nl->next;
    }
    closenls(sil, i);
}

static void createclosure(Sil *sil, Def *def, Value *slot) {
    Func *fn = sil__newnativefunc(sil, def);
    Frame *fr = sil->frame;
    *slot = GCO(fn);
    sil__pushtmp(sil, GCO(fn));

    for (int i = 0; i < def->nlilen; i++) {
        NonlocalInfo nli = def->nli[i];
        Nonlocal *nl;
        if (nli.local) nl = opennl(sil, fr->slots + nli.index);
        else nl = fr->fn->nls[nli.index];
        fn->nls[fn->nlc++] = nl;
    }

    sil__clrtmp(sil);
}

static enum sil_result str__add(Sil *sil) {
    if (!ASBOOL(lds(sil, 2))) sil_concat(sil, 0, 0, 1);
    else sil_concat(sil, 0, 1, 0);

    return sil_return(sil, 0);
}

static enum sil_result str__pos(Sil *sil) {
    sil_to_num(sil, 0, 0);
    return sil_return(sil, 0);
}

static enum sil_result str__neg(Sil *sil) {
    sil_to_num(sil, 0, 0);
    sil_num(sil, -sil_as_double(sil, 0), 0);
    return sil_return(sil, 0);
}

static enum sil_result str__rev(Sil *sil) {
    int len;
    const char *chars = sil_as_chars(sil, &len, 0);
    char *buf = ALLOCARRAY(char, len + 1);
    memcpy(buf, chars, sizeof(char) * len + 1);
    for (int i = 0; i < len / 2; i++) {
        char tmp = buf[i];
        buf[i] = buf[len - i - 1];
        buf[len - i - 1] = tmp;
    }
    sil_str(sil, buf, 0);
    FREEARRAY(buf, char, len + 1);
    return sil_return(sil, 0);
}

static void initklasses(Sil *sil) {
    sil->klss.str = sil__newdoc(sil, NULL);

    Value k, v;

#define STORE(klass, name)                                                     \
    do {                                                                       \
        k = GCO(STRLIT(#name));                                                \
        sil__pushtmp(sil, k);                                                  \
        v = GCO(sil__newcfunc(sil, klass##name, #name, 0));                    \
        sil__pushtmp(sil, v);                                                  \
        sil__docstore(sil, sil->klss.klass, k, v);                             \
        sil__clrtmp(sil);                                                      \
    } while (false)

    STORE(str, __add);
    STORE(str, __pos);
    STORE(str, __neg);
    STORE(str, __rev);

#undef STORE
}

static bool unwind(Sil *sil, Value v) {
    for (;;) {
        switch (sil->frame->fn->type) {
            case FUNC_NATIVE: {
                Def *def = sil->frame->fn->proto.native;
                int idx = codeidx(sil->frame);
                for (int i = 0; i < def->ctchlen; i++) {
                    CatchInfo ci = def->ctch[i];
                    if (ci.begin <= idx && idx < ci.end) {
                        Value *slots = sil->frame->slots;
                        closenlstil(sil, slots + ci.slot - 1);
                        slots[ci.slot] = v;
                        sil->frame->as.native.ip = def->code + ci.recover;
                        return true;
                    }
                }
                dropframe(sil);
                continue;
            }
            case FUNC_C: {
                Frame *prev = sil->frame + 1;
                closenlstil(sil, prev->slots);
                prev->slots[prev->ret] = v;
                return false;
            }
            default:
                UNREACHABLE;
        }
    }
}

static enum sil_result execute(Sil *sil) {
    Frame *frame;
    Value *slots;
    register Code code;

#define UPDCTX()                                                               \
    do {                                                                       \
        frame = sil->frame;                                                    \
        slots = frame->slots;                                                  \
    } while (false)

#define OP (code.op)
#define A (code.a)
#define B (code.b)
#define C (code.c)
#define LB ((B << 8) | C)
#define SA (slots[A])
#define SB (slots[B])
#define SC (slots[C])

#define DEF (frame->fn->proto.native)
#define READK(i) (DEF->k[i])

#define ATTEMPTBC(to)                                                          \
    do {                                                                       \
        Value btype = sil__typeof(sil, SB);                                    \
        sil__pushtmp(sil, btype);                                              \
        Value ctype = sil__typeof(sil, SC);                                    \
        sil__pushtmp(sil, ctype);                                              \
        UNWINDFMTLN("attempt to " to " @ and @", btype, ctype);                \
    } while (false)

#define ATTEMPTB(to)                                                           \
    do {                                                                       \
        sil__pushtmp(sil, sil__typeof(sil, SB));                               \
        Value btype = sil__peektmp(sil, 0);                                    \
        UNWINDFMTLN("attempt to " to " @", btype);                             \
    } while (false)

#define KLASSBINOP(ifnum, opname, method)                                      \
    do {                                                                       \
        if (ISNUM(SB) && ISNUM(SC)) ifnum else {                               \
                bool rev = false;                                              \
                int slot = C + 1;                                              \
                Value m = getklassmethod(sil, SB, method, LITLEN(method));     \
                if (ISVOID(m)) {                                               \
                    rev = true;                                                \
                    m = getklassmethod(sil, SC, method, LITLEN(method));       \
                    if (ISVOID(m)) ATTEMPTBC(opname);                          \
                }                                                              \
                ensurearray(sil, slots + slot + 2);                            \
                UPDCTX();                                                      \
                slots[slot + 2] = BOOL(rev);                                   \
                if (!rev) {                                                    \
                    slots[slot + 1] = SC;                                      \
                    slots[slot] = SB;                                          \
                } else {                                                       \
                    slots[slot + 1] = SB;                                      \
                    slots[slot] = SC;                                          \
                }                                                              \
                if (callvalue(sil, m, A - slot, 3, slots + slot) != SIL_OK)    \
                    UNWIND(SA);                                                \
                UPDCTX();                                                      \
            }                                                                  \
    } while (false)

#define KLASSUNOP(ifnum, opname, method)                                       \
    do {                                                                       \
        if (ISNUM(SB)) ifnum else {                                            \
                int slot = B + 1;                                              \
                Value m = getklassmethod(sil, SB, method, LITLEN(method));     \
                ensurearray(sil, slots + slot);                                \
                UPDCTX();                                                      \
                slots[slot] = SB;                                              \
                if (ISVOID(m)) ATTEMPTB(opname);                               \
                if (callvalue(sil, m, A - slot, 1, slots + slot) != SIL_OK)    \
                    UNWIND(SA);                                                \
                UPDCTX();                                                      \
            }                                                                  \
    } while (false)

#define UNWIND(thro)                                                           \
    do {                                                                       \
        sil__clrtmp(sil);                                                      \
        if (!unwind(sil, thro)) return SIL_UNCAUGHT;                           \
        UPDCTX();                                                              \
        DISPATCH;                                                              \
    } while (false)

#define UNWINDLIT(literal) UNWIND(GCO(STRLIT(sil, literal)))
#define UNWINDFMT(format, ...) UNWIND(GCO(fmt(sil, format, __VA_ARGS__)))
#define UNWINDFMTLN(format, ...)                                               \
    UNWIND(GCO(                                                                \
        fmt(sil, format " in func $ at line #", __VA_ARGS__, funcname(frame),  \
            funcline(frame))                                                   \
    ))

    UPDCTX();

    if (frame->fn->type == FUNC_C) return SIL_OK;

#ifdef DEBUG_EXECUTE
    printf("[[debug Execute]]\n");

    #define SWITCH                                                             \
        code = *frame->as.native.ip++;                                         \
        Value *top = gettop(sil);                                              \
        printf("|   [ ");                                                      \
        for (Value *v = sil->array.start; v < top; v++) {                      \
            if (v == slots) printf("$ ");                                      \
            printf("[");                                                       \
            sil__logvalue(sil, *v);                                            \
            printf("] ");                                                      \
        }                                                                      \
        printf("]\n");                                                         \
        sil__logcode(sil, frame->fn->proto.native, codeidx(frame));            \
        switch (code.op)
#else
    #define SWITCH switch ((code = *frame->as.native.ip++).op)
#endif

#ifdef SWITCHEDGOTO
    #define START DISPATCH;
    #define END

    #define DISPATCH                                                           \
        do {                                                                   \
            SWITCH {                                                           \
                OPS_X                                                          \
            }                                                                  \
        } while (false)

    #define CASE(op, body) case_##op : body DISPATCH;

    #define X(op)                                                              \
        case BC_##op:                                                          \
            goto case_##op;
#else
    #define START                                                              \
    dispatch:                                                                  \
        SWITCH {
    #define END }

    #define DISPATCH goto dispatch

    #define CASE(op, body)                                                     \
        case BC_##op:                                                          \
            body DISPATCH;

    #define X(op) @
#endif

    START

    CASE(NIL, { SA = VOID; })
    CASE(TRUE, { SA = BOOL(true); })
    CASE(FLS, { SA = BOOL(false); })
    CASE(LDK, { SA = READK(LB); })
    CASE(FUNC, { createclosure(sil, (Def *)ASGCO(READK(LB)), &SA); })
    CASE(DOC, {
        Doc *klass;

        if (ISVOID(SB)) klass = NULL;
        else if (ISDOC(SB)) klass = ASDOC(SB);
        else {
            Value t = sil__typeof(sil, SB);
            sil__pushtmp(sil, t);
            UNWINDFMTLN("attempt to use @ as class", t);
        }

        SA = GCO(sil__newdoc(sil, klass));
    })
    CASE(DEFG, {
        Value k = READK(LB);
        Value v = SA;
        sil__mapstore(sil, &sil->glob, k, v);
    })
    CASE(STG, {
        Value k = READK(LB);
        Value v = SA;
        if (!sil__mapstore(sil, &sil->glob, k, v))
            UNWINDFMTLN("undefined variable '@'", k);
    })
    CASE(LDG, {
        Value k = READK(LB);
        if (!sil__mapload(&sil->glob, k, &SA))
            UNWINDFMTLN("undefined variable '@'", k);
    })
    CASE(CLNL, { closenls(sil, LB); })
    CASE(STNL, { *frame->fn->nls[LB]->value = SA; })
    CASE(LDNL, { SA = *frame->fn->nls[LB]->value; })
    CASE(STI, {
        Value doc = SA;
        if (!ISDOC(doc)) {
            Value t = sil__typeof(sil, doc);
            sil__pushtmp(sil, t);
            UNWINDFMTLN("attempt to store to @", t);
        }
        sil__docstore(sil, ASDOC(doc), SB, SC);
    })
    CASE(LDI, {
        Value doc = SB;
        if (!ISDOC(doc)) {
            Value t = sil__typeof(sil, doc);
            sil__pushtmp(sil, t);
            UNWINDFMTLN("attempt to load from @", t);
        }
        SA = sil__docload(ASDOC(doc), SC);
    })
    CASE(DEL, {
        Value doc = SB;
        if (!ISDOC(doc)) {
            Value t = sil__typeof(sil, doc);
            sil__pushtmp(sil, t);
            UNWINDFMTLN("attempt to delete from @", t);
        }
        SA = BOOL(sil__docdelete(ASDOC(doc), SC));
    })
    CASE(COPY, { SA = SB; })
    CASE(SWAP, {
        Value tmp = SA;
        SA = SB;
        SB = tmp;
    })
    CASE(ADD, { KLASSBINOP(SA = NUM(ASNUM(SB) + ASNUM(SC));, "add", "add"); })
    CASE(SUB, {
        KLASSBINOP(SA = NUM(ASNUM(SB) - ASNUM(SC));, "subtract", "sub");
    })
    CASE(MUL, {
        KLASSBINOP(SA = NUM(ASNUM(SB) * ASNUM(SC));, "multiply", "mul");
    })
    CASE(POW, {
        KLASSBINOP(SA = NUM(pow(ASNUM(SB), ASNUM(SC)));, "power", "pow");
    })
    CASE(FDIV, {
        KLASSBINOP(SA = NUM(ASNUM(SB) / ASNUM(SC));, "float division", "fdiv");
    })
    CASE(IDIV, {
        KLASSBINOP(
            SA = NUM(floor(ASNUM(SB) / ASNUM(SC)));, "integer division", "idiv"
        );
    })
    CASE(MOD, {
        KLASSBINOP(
            SA = NUM(fmod(ASNUM(SB), ASNUM(SC)));, "modulo division", "mod"
        );
    })
    CASE(AND, {
        KLASSBINOP(
            {
                int32_t b = ASNUM(SB);
                int32_t c = ASNUM(SC);
                SA = NUM(b & c);
            },
            "bitwise and", "and"
        );
    })
    CASE(XOR, {
        KLASSBINOP(
            {
                int32_t b = ASNUM(SB);
                int32_t c = ASNUM(SC);
                SA = NUM(b ^ c);
            },
            "bitwise xor", "xor"
        );
    })
    CASE(OR, {
        KLASSBINOP(
            {
                int32_t b = ASNUM(SB);
                int32_t c = ASNUM(SC);
                SA = NUM(b | c);
            },
            "bitwise or", "or"
        );
    })
    CASE(LSH, {
        KLASSBINOP(
            {
                int32_t b = ASNUM(SB);
                int32_t c = ASNUM(SC);
                SA = NUM(b << c);
            },
            "left shift", "lsh"
        );
    })
    CASE(RSH, {
        KLASSBINOP(
            {
                int32_t b = ASNUM(SB);
                int32_t c = ASNUM(SC);
                SA = NUM(b >> c);
            },
            "right shift", "rsh"
        );
    })
    CASE(EQ, { SA = BOOL(VALEQ(SB, SC)); })
    CASE(LT, {
        KLASSBINOP(SA = BOOL((ASNUM(SB) < ASNUM(SC)));, "less than", "lt");
    })
    CASE(LE, {
        KLASSBINOP(
            SA = BOOL((ASNUM(SB) <= ASNUM(SC)));, "less than or equal", "le"
        );
    })
    CASE(NOT, { SA = BOOL(!sil__tobool(SB)); })
    CASE(REV, { KLASSUNOP(SA = NUM(~(int32_t)ASNUM(SB));, "reverse", "rev"); })
    CASE(NEG, { KLASSUNOP(SA = NUM(-ASNUM(SB));, "negate", "neg"); })
    CASE(POS, { KLASSUNOP(SA = SB;, "posite", "pos"); })
    CASE(TPOF, { SA = sil__typeof(sil, SB); })
    CASE(IS, {
        Value l = SB;
        for (;;) {
            Doc *klass = getklass(sil, l);
            if (klass == NULL) {
                SA = BOOL(false);
                break;
            }
            l = GCO(klass);
            if (VALEQ(l, SC)) {
                SA = BOOL(true);
                break;
            }
        }
    })
    CASE(JMP, { frame->as.native.ip += LB; })
    CASE(JMPF, { frame->as.native.ip += !sil__tobool(SA) * LB; })
    CASE(BJMP, { frame->as.native.ip -= LB; })
    CASE(CALL, {
        int argc = C & 0x7f;

        if (C >> 7) {
            int offset = B + argc;
            Value tospr = slots[offset + 1];
            if (!ISDOC(tospr)) {
                Value t = sil__typeof(sil, tospr);
                sil__pushtmp(sil, t);
                UNWINDFMTLN("attempt to spread @", t);
            }
            sil__pushtmp(sil, tospr);
            Doc *spr = ASDOC(tospr);
            Value lenval = sil__docload(spr, GCO(STRLIT("__len")));
            double lendouble = sil__tonum(sil, lenval);
            int count = sil__dtoi(isinf(lendouble) ? 0 : lendouble);
            if (count > 1) {
                ensurearray(sil, slots + offset + count);
                UPDCTX();
            }
            for (int i = 0; i < count; i++)
                slots[offset + i + 1] = sil__docload(spr, NUM(i));
            argc += count;
            sil__clrtmp(sil);
        }

        int ret = A - B - 1;
        Value *argv = slots + B + 1;
        if (callvalue(sil, SB, ret, argc, argv) != SIL_OK) UNWIND(SA);
        UPDCTX();
    })
    CASE(RET, {
        slots[frame->ret] = SA;
        dropframe(sil);
        if (sil->frame->fn->type == FUNC_C) return SIL_OK;
        UPDCTX();
    })
    CASE(THRW, { UNWIND(SA); })

    END UNREACHABLE;

#undef X
#undef CASE
#undef DISPATCH
#undef END
#undef START
#undef SWITCH
#undef UNWINDFMTLN
#undef UNWINDFMT
#undef UNWINDLIT
#undef UNWIND
#undef KLASSUNOP
#undef KLASSBINOP
#undef ATTEMPTB
#undef ATTEMPTBC
#undef READK
#undef DEF
#undef SC
#undef SB
#undef SA
#undef LB
#undef C
#undef B
#undef A
#undef OP
#undef UPDCTX
}

static Func mainfunc = {
    .proto.c = NULL,
    .name = "@main",
    .nlc = 0,
    .type = FUNC_C,
    .o = {
        .type = GCO_FUNC,
        .marked = false,
        .next = NULL,
    },
};

/* ==== api ================================================================= */

Sil *sil_new(SilRealloc realloc) {
    Sil *sil = realloc(NULL, sizeof(Sil));

    sil->gc.handles = NULL;
    sil->gc.tmpcur = sil->gc.tmp;
    sil->gc.gcos = NULL;
    sil->gc.allocsize = sizeof(Sil);
    sil->gc.trigger = sizeof(Sil) + INITTRIGGER;
    sil->gc.realloc = realloc;

    sil->frame = sil->frarray - 1;

    sil__initcompiler(&sil->compiler);
    sil__initmap(&sil->glob);
    sil__initmap(&sil->strpool);

    sil->klss.str = NULL;
    sil->array.start = NULL;

    initklasses(sil);

    sil->array.start = ALLOCARRAY(Value, INITARRAY);
    sil->array.end = sil->array.start + INITARRAY;

    sil->opennls = NULL;

    Frame *mainframe = nextframe(sil);
    mainframe->fn = &mainfunc;
    mainframe->slots = sil->array.start;
    mainframe->as.c.slots = 0;

    return sil;
}

void sil_free(Sil *sil) {
#define FREELINKED(type, list, next, free)                                     \
    do {                                                                       \
        type *prev;                                                            \
        for (type *i = list; i != NULL; i = prev) {                            \
            prev = next;                                                       \
            free;                                                              \
        }                                                                      \
    } while (false);

    sil__freemap(sil, &sil->glob);

    FREELINKED(Gco, sil->gc.gcos, i->next, sil__freegco(sil, i));

    FREELINKED(SilHandle, sil->gc.handles, i->next, freehandle(sil, i));

    sil__freemap(sil, &sil->strpool);

    FREEARRAY(sil->array.start, Value, sil->array.end - sil->array.start);

    ASSERT(sil->gc.allocsize == sizeof(Sil), "invalid cleanup");

    sil->gc.realloc(sil, 0);

#undef FREELINKED
}

void sil_gc(Sil *sil) {
#ifdef DEBUG_GC
    printf("[[debug GC begin]]\n");
    clock_t start = clock();
    size_t oldallocsize = sil->gc.allocsize;
#endif

    mark(sil);
    sweep(sil);

    sil->gc.trigger = sil->gc.allocsize * TRIGGERFACTOR;

#ifdef DEBUG_GC
    printf("{\n\told: %zu,\n", oldallocsize);
    printf("\tnew: %zu,\n", sil->gc.allocsize);
    printf("\ttime: %g,\n", (double)(clock() - start) / CLOCKS_PER_SEC);
    printf("\tfree: %zu,\n", oldallocsize - sil->gc.allocsize);
    printf("\ttrigger: %zu,\n}\n", sil->gc.trigger);
    printf("[[debug GC end]]\n");
#endif
}

enum sil_result sil_interpret(Sil *sil, const char *script, int slot) {
    enum sil_result code = sil__compile(&sil->compiler, script);
    if (code != SIL_OK) {
        sts(sil, slot, sil__poptmp(sil));
        return code;
    }

    Def *def = (Def *)ASGCO(sil__peektmp(sil, 0));
    Func *func = sil__newnativefunc(sil, def);
    sil__clrtmp(sil);

    sts(sil, slot, GCO(func));
    return sil_call(sil, 0, slot);
}

enum sil_result sil_call(Sil *sil, int argc, int slot) {
    Value cle = lds(sil, slot);
    Value *argv = sil->frame->slots + slot + 1;
    enum sil_result code = callvalue(sil, cle, -1, argc, argv);
    if (code != SIL_OK) return code;
    return execute(sil);
}

int sil_slots(Sil *sil) {
    return sil->frame->as.c.slots;
}

void sil_use_slots(Sil *sil, int use) {
    ensurearray(sil, sil->frame->slots + use);
    for (int i = sil_slots(sil); i < use; i++) sts(sil, i, VOID);
    sil->frame->as.c.slots = use;
}

enum sil_type sil_type(Sil *sil, int slot) {
    Value v = lds(sil, slot);
    switch (VALTYPE(v)) {
        case VAL_VOID:
            return SIL_VOID;
        case VAL_BOOL:
            return SIL_BOOL;
        case VAL_NUM:
            return SIL_NUM;
        case VAL_GCO: {
            switch (GCOTYPE(ASGCO(v))) {
                case GCO_STR:
                    return SIL_STR;
                case GCO_FUNC:
                    return SIL_FUNC;
                case GCO_DOC:
                    return SIL_DOC;
                default:
                    UNREACHABLE;
            }
        }
        default:
            UNREACHABLE;
    }
}

bool sil_as_bool(Sil *sil, int slot) {
    return ASBOOL(lds(sil, slot));
}

int sil_as_int(Sil *sil, int slot) {
    return sil__dtoi(ASNUM(lds(sil, slot)));
}

double sil_as_double(Sil *sil, int slot) {
    return ASNUM(lds(sil, slot));
}

const char *sil_as_chars(Sil *sil, int *len, int slot) {
    if (len != NULL) *len = ASSTR(sil->frame->slots[slot])->len;
    return ASCHARS(sil->frame->slots[slot]);
}

void sil_to_bool(Sil *sil, int dst, int slot) {
    sts(sil, dst, BOOL(sil__tobool(lds(sil, slot))));
}

void sil_to_num(Sil *sil, int dst, int slot) {
    sts(sil, dst, NUM(sil__tonum(sil, lds(sil, slot))));
}

void sil_to_str(Sil *sil, int dst, int slot) {
    Str *str = sil__tostr(sil, lds(sil, slot));
    sts(sil, dst, GCO(str));
}

void sil_handle(Sil *sil, SilHandle *hndl, int slot) {
    sts(sil, slot, hndl->v);
}

bool sil_equal(Sil *sil, int slota, int slotb) {
    return VALEQ(lds(sil, slota), lds(sil, slotb));
}

void sil_concat(Sil *sil, int dst, int arg1, int arg2) {
    Str *str1 = sil__tostr(sil, lds(sil, arg1));
    sil__pushtmp(sil, GCO(str1));

    Str *str2 = sil__tostr(sil, lds(sil, arg2));
    sil__pushtmp(sil, GCO(str2));

    sts(sil, dst, GCO(sil__strconcat(sil, str1, str2)));

    sil__clrtmp(sil);
}

enum sil_result sil_return(Sil *sil, int slot) {
    sts(sil, sil->frame->ret, lds(sil, slot));
    return SIL_OK;
}

enum sil_result sil_throw(Sil *sil, int slot) {
    sil_return(sil, slot);
    return SIL_UNCAUGHT;
}

void sil_void(Sil *sil, int slot) {
    sts(sil, slot, VOID);
}

void sil_bool(Sil *sil, bool v, int slot) {
    sts(sil, slot, BOOL(v));
}

void sil_num(Sil *sil, double v, int slot) {
    sts(sil, slot, NUM(v));
}

void sil_str(Sil *sil, const char *chars, int slot) {
    sts(sil, slot, GCO(sil__newstrl(sil, chars, strlen(chars))));
}

void sil_strl(Sil *sil, const char *chars, int len, int slot) {
    sts(sil, slot, GCO(sil__newstrl(sil, chars, len)));
}

void sil_func(Sil *sil, SilFunc v, const char *name, int slot) {
    sts(sil, slot, GCO(sil__newcfunc(sil, v, name, 0)));
}

void sil_closure(Sil *sil, SilFunc v, const char *name, int nlc, int slot) {
    Func *func = sil__newcfunc(sil, v, name, nlc);
    sil__pushtmp(sil, GCO(func));

    for (int i = 0; i < nlc; i++) {
        Nonlocal *nl = sil__newnonlocal(sil, sil->frame->slots + slot + i);
        sil__closenonlocal(nl);
        func->nls[func->nlc++] = nl;
    }

    sts(sil, slot, GCO(func));

    sil__clrtmp(sil);
}

void sil_get_nonlocal(Sil *sil, int index, int slot) {
    sts(sil, slot, *sil->frame->fn->nls[index]->value);
}

void sil_set_nonlocal(Sil *sil, int index, int slot) {
    *sil->frame->fn->nls[index]->value = lds(sil, slot);
}

void sil_define(Sil *sil, const char *name, int slot) {
    Value k = GCO(sil__newstrl(sil, name, strlen(name)));
    sil__pushtmp(sil, k);
    sil__mapstore(sil, &sil->glob, k, lds(sil, slot));
    sil__clrtmp(sil);
}

bool sil_variable(Sil *sil, const char *name, int dst) {
    Value k = GCO(sil__newstrl(sil, name, strlen(name)));
    bool has = sil__mapload(&sil->glob, k, &k);
    if (has) sts(sil, dst, k);
    return has;
}

void sil_copy(Sil *sil, int dst, int slot) {
    sts(sil, dst, lds(sil, slot));
}

void sil_doc(Sil *sil, int slot) {
    sts(sil, slot, GCO(sil__newdoc(sil, NULL)));
}

int sil_doc_size(Sil *sil, int slot) {
    return ASDOC(lds(sil, slot))->map.len;
}

void sil_doc_store(Sil *sil, int d, int k, int v) {
    sil__docstore(sil, ASDOC(lds(sil, d)), lds(sil, k), lds(sil, v));
}

void sil_doc_load(Sil *sil, int dst, int d, int k) {
    sts(sil, dst, sil__docload(ASDOC(lds(sil, d)), lds(sil, k)));
}

bool sil_doc_delete(Sil *sil, int d, int k) {
    return sil__docdelete(ASDOC(lds(sil, d)), lds(sil, k));
}

int sil_line(Sil *sil) {
    int ln;
    for (int i = 0;; i++) {
        const char *frname = sil_trace(sil, &ln, i);
        if (ln > 0) return ln;
        if (frname == NULL) return -1;
    }
    return ln;
}

const char *sil_trace(Sil *sil, int *line, int lv) {
    Frame *fr = sil->frame - lv;
    if (fr < sil->frarray) return NULL;
    *line = funcline(fr);
    return funcname(fr);
}

SilHandle *sil_new_handle(Sil *sil, int slot) {
    return newhandle(sil, lds(sil, slot));
}

void sil_free_handle(Sil *sil, SilHandle *hndl) {
    freehandle(sil, hndl);
}
