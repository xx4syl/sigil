#include "gco.h"

#include <stdio.h>
#include <string.h>

#include "core.h"

#define GROW(old) ((old) < 0x100 ? (old) + 0x20 : (old) * 2)

int sil__defaddcode(Sil *sil, Def *def, Code code, int line, int slots) {
    if (def->codelen + 1 > def->codecap) {
        int newcap = GROW(def->codecap);
        def->lines = REALLOCARRAY(def->lines, int, def->codecap, newcap);
        def->slots = REALLOCARRAY(def->slots, int, def->codecap, newcap);
        def->code = REALLOCARRAY(def->code, Code, def->codecap, newcap);
        def->codecap = newcap;
    }

    def->lines[def->codelen] = line;
    def->slots[def->codelen] = slots;
    def->code[def->codelen++] = code;

    return def->codelen - 1;
}

int sil__defaddk(Sil *sil, Def *def, Value k) {
    for (int i = 0; i < def->klen; i++) {
        if (VALEQ(k, def->k[i])) return i;
    }

    if (def->klen + 1 > def->kcap) {
        int newcap = GROW(def->kcap);
        def->k = REALLOCARRAY(def->k, Value, def->kcap, newcap);
        def->kcap = newcap;
    }

    def->k[def->klen++] = k;

    return def->klen - 1;
}

int sil__defaddnli(Sil *sil, Def *def, bool local, int index) {
    if (def->nlilen + 1 > def->nlicap) {
        int newcap = GROW(def->nlicap);
        def->nli = REALLOCARRAY(def->nli, NonlocalInfo, def->nlicap, newcap);
        def->nlicap = newcap;
    }

    def->nli[def->nlilen++] = (NonlocalInfo){
        .local = local,
        .index = index,
    };

    return def->nlilen - 1;
}

int sil__defaddctch(Sil *sil, Def *def, int bgn, int end, int rcvr, int slot) {
    if (def->ctchlen + 1 > def->ctchcap) {
        int newcap = def->ctchcap + 2;
        def->ctch = REALLOCARRAY(def->ctch, CatchInfo, def->ctchcap, newcap);
        def->ctchcap = newcap;
    }

    def->ctch[def->ctchlen++] = (CatchInfo){
        .begin = bgn,
        .end = end,
        .recover = rcvr,
        .slot = slot,
    };

    return def->ctchlen - 1;
}

static void initgco(Gco *o, enum gcotype type) {
    o->marked = false;
    o->next = NULL;
    o->type = type;
}

static void attachgco(Sil *sil, Gco *o) {
    o->next = sil->gc.gcos;
    sil->gc.gcos = o;
}

void *sil__newgco(Sil *sil, size_t size, enum gcotype type) {
    Gco *o = ALLOC(size);

    initgco(o, type);
    attachgco(sil, o);

    return o;
}

Def *sil__newdef(Sil *sil, char *name) {
    Def *d = sil__newgco(sil, sizeof(Def), GCO_DEF);

    d->name = name;
    d->maxslots = 0;
    d->paramc = 0;
    d->vararg = false;

    d->code = NULL;
    d->lines = NULL;
    d->slots = NULL;
    d->codecap = d->codelen = 0;

    d->k = NULL;
    d->kcap = d->klen = 0;

    d->nli = NULL;
    d->nlicap = d->nlilen = 0;

    d->ctch = NULL;
    d->ctchcap = d->ctchlen = 0;

    return d;
}

static void markdef(Gco *o) {
    Def *def = (Def *)o;
    for (int i = 0; i < def->klen; i++) sil__markvalue(def->k[i]);
}

static void freedef(Sil *sil, Gco *o) {
    Def *def = (Def *)o;
    if (def->name != NULL) FREEARRAY(def->name, char, strlen(def->name) + 1);
    FREEARRAY(def->lines, int, def->codecap);
    FREEARRAY(def->slots, int, def->codecap);
    FREEARRAY(def->code, Code, def->codecap);
    FREEARRAY(def->k, Value, def->kcap);
    FREEARRAY(def->nli, NonlocalInfo, def->nlicap);
    FREEARRAY(def->ctch, CatchInfo, def->ctchcap);
    FREE(def, sizeof(Def));
}

Str *sil__newstrl(Sil *sil, const char *chars, int len) {
    uint32_t hash = sil__fnv1a(chars, len);

    Str *str = sil__maploadstr(&sil->strpool, chars, len, hash);
    if (str != NULL) return str;

    str = sil__newgco(sil, FLEXSIZE(Str, char, len + 1), GCO_STR);

    str->len = len;
    str->hash = hash;
    memcpy(str->chars, chars, len);
    str->chars[len] = '\0';

    sil__pushtmp(sil, GCO(str));
    sil__mapstore(sil, &sil->strpool, GCO(str), VOID);
    sil__poptmp(sil);

    return str;
}

static void markstr(Gco *o) {}

static void freestr(Sil *sil, Gco *o) {
    Str *str = (Str *)o;
    sil__mapdelete(&sil->strpool, GCO(str));
    FREEFLEX(str, Str, char, str->len + 1);
}

Func *sil__newnativefunc(Sil *sil, Def *def) {
    const size_t size = FLEXSIZE(Func, Nonlocal *, def->nlilen);
    Func *f = sil__newgco(sil, size, GCO_FUNC);

    f->type = FUNC_NATIVE;
    f->proto.native = def;
    f->name = def->name;

    f->nlc = 0;

    return f;
}

Func *sil__newcfunc(Sil *sil, SilFunc cfn, const char *name, int nlc) {
    char *dupname = name != NULL ? sil__strdupl(sil, name, strlen(name)) : NULL;

    const size_t size = FLEXSIZE(Func, Nonlocal *, nlc);
    Func *f = sil__newgco(sil, size, GCO_FUNC);

    f->type = FUNC_C;
    f->proto.c = cfn;
    f->name = dupname;

    f->nlc = 0;

    return f;
}

static void markfunc(Gco *o) {
    Func *f = (Func *)o;

    if (f->type == FUNC_NATIVE) sil__markgco((Gco *)f->proto.native);

    for (int i = 0; i < f->nlc; i++) sil__markgco((Gco *)f->nls[i]);
}

static void freefunc(Sil *sil, Gco *o) {
    Func *f = (Func *)o;

    if (f->type == FUNC_C && f->name != NULL)
        FREEARRAY(f->name, char, strlen(f->name) + 1);

    FREEFLEX(f, Func, Nonlocal *, f->nlc);
}

Doc *sil__newdoc(Sil *sil, Doc *klass) {
    Doc *doc = sil__newgco(sil, sizeof(Doc), GCO_DOC);

    doc->klass = klass;

    sil__initmap(&doc->map);

    doc->arr = NULL;
    doc->arrlen = doc->arrcap = 0;

    doc->destruct = NULL;
    doc->data = NULL;

    return doc;
}

void sil__docstore(Sil *sil, Doc *doc, Value k, Value v) {
    sil__mapstore(sil, &doc->map, k, v);
}

Value sil__docload(Doc *doc, Value k) {
    Value v = VOID;
    if (!sil__mapload(&doc->map, k, &v) && doc->klass != NULL)
        return sil__docload(doc->klass, k);
    return v;
}

bool sil__docdelete(Doc *doc, Value k) {
    return sil__mapdelete(&doc->map, k);
}

static void markdoc(Gco *o) {
    Doc *doc = (Doc *)o;
    sil__markmap(&doc->map);
    for (int i = 0; i < doc->arrlen; i++) sil__markvalue(doc->arr[i]);
    if (doc->klass != NULL) sil__markgco((Gco *)doc->klass);
}

static void freedoc(Sil *sil, Gco *o) {
    Doc *doc = (Doc *)o;
    sil__freemap(sil, &doc->map);
    FREEARRAY(doc->arr, Value, doc->arrcap);
    if (doc->destruct != NULL) doc->destruct(doc->data);
    FREE(doc, sizeof(Doc));
}

Str *sil__strconcat(Sil *sil, Str *a, Str *b) {
    char *buf = ALLOCARRAY(char, a->len + b->len);

    memcpy(buf, a->chars, a->len);
    memcpy(buf + a->len, b->chars, b->len);

    Str *str = sil__newstrl(sil, buf, a->len + b->len);

    FREEARRAY(buf, char, a->len + b->len);

    return str;
}

Nonlocal *sil__newnonlocal(Sil *sil, Value *local) {
    Nonlocal *nl = sil__newgco(sil, sizeof(Nonlocal), GCO_NONLOCAL);

    nl->closed = UNDEFINED;
    nl->value = local;

    return nl;
}

static void marknonlocal(Gco *o) {
    Nonlocal *nl = (Nonlocal *)o;
    sil__markvalue(*nl->value);
}

static void freenonlocal(Sil *sil, Gco *o) {
    Nonlocal *nl = (Nonlocal *)o;
    FREE(nl, sizeof(Nonlocal));
}

void sil__closenonlocal(Nonlocal *nl) {
    nl->closed = *nl->value;
    nl->value = &nl->closed;
}

void sil__logcode(Sil *sil, Def *d, int i) {
#define X(name) [BC_##name] = #name,
    static const char *names[] = {OPS_X};
#undef X

#define LB ((code.b << 8) | code.c)

#define BIN(op) printf("s%02x <- s%02x " op " s%02x", code.a, code.b, code.c)
#define UN(op) printf("s%02x <- " op "(s%02x)", code.a, code.b)

    if (i > 0 && d->lines[i] == d->lines[i - 1]) {
        printf("|   ");
    } else {
        printf("%-4d", d->lines[i]);
    }

    Code code = d->code[i];
    printf(
        "[%04d] %-4s %02x %02x %02x ", i, names[code.op], code.a, code.b, code.c
    );

    printf("(%02x slots) ", d->slots[i]);

    printf("| ");
    switch (code.op) {
        case BC_NIL: {
            printf("s%02x <- void", code.a);
        } break;
        case BC_TRUE: {
            printf("s%02x <- true", code.a);
        } break;
        case BC_FLS: {
            printf("s%02x <- false", code.a);
        } break;
        case BC_LDK: {
            printf("s%02x <- k%04x(", code.a, LB);
            sil__logvalue(sil, d->k[LB]);
            printf(")");
        } break;
        case BC_FUNC: {
            printf("s%02x <- func(k%04x(", code.a, LB);
            sil__logvalue(sil, d->k[LB]);
            printf("))");
        } break;
        case BC_DOC: {
            printf("s%02x <- doc(s%02x)", code.a, code.b);
        } break;
        case BC_DEFG: {
            printf("new g%04x(%s) <- s%02x", LB, ASCHARS(d->k[LB]), code.a);
        } break;
        case BC_STG: {
            printf("g%04x(%s) <- s%02x", LB, ASCHARS(d->k[LB]), code.a);
        } break;
        case BC_LDG: {
            printf("s%02x <- g%04x(%s)", code.a, LB, ASCHARS(d->k[LB]));
        } break;
        case BC_CLNL: {
            printf("close %d nonlocals", LB);
        } break;
        case BC_STNL: {
            printf("nl%04x <- s%02x", LB, code.a);
        } break;
        case BC_LDNL: {
            printf("s%02x <- nl%04x", code.a, LB);
        } break;
        case BC_STI: {
            printf("(s%02x)[s%02x] <- s%02x", code.a, code.b, code.c);
        } break;
        case BC_LDI: {
            printf("s%02x <- (s%02x)[s%02x]", code.a, code.b, code.c);
        } break;
        case BC_DEL: {
            printf("s%02x <- del (s%02x)[s%02x]", code.a, code.b, code.c);
        } break;
        case BC_COPY: {
            printf("s%02x <- s%02x", code.a, code.b);
        } break;
        case BC_SWAP: {
            printf("s%02x <-> s%02x", code.a, code.b);
        } break;
        case BC_ADD: {
            BIN("+");
        } break;
        case BC_SUB: {
            BIN("-");
        } break;
        case BC_MUL: {
            BIN("*");
        } break;
        case BC_POW: {
            BIN("**");
        } break;
        case BC_FDIV: {
            BIN("/");
        } break;
        case BC_IDIV: {
            BIN("//");
        } break;
        case BC_MOD: {
            BIN("%%");
        } break;
        case BC_AND: {
            BIN("&");
        } break;
        case BC_OR: {
            BIN("|");
        } break;
        case BC_XOR: {
            BIN("^");
        } break;
        case BC_EQ: {
            BIN("==");
        } break;
        case BC_LT: {
            BIN("<");
        } break;
        case BC_LE: {
            BIN("<=");
        } break;
        case BC_NOT: {
            UN("!");
        } break;
        case BC_REV: {
            UN("~");
        } break;
        case BC_NEG: {
            UN("-");
        } break;
        case BC_POS: {
            UN("+");
        } break;
        case BC_TPOF: {
            UN("typeof");
        } break;
        case BC_IS: {
            BIN("~");
        } break;
        case BC_JMP: {
            printf("jump to %d", i + LB + 1);
        } break;
        case BC_JMPF: {
            printf("jump to %d if not s%02x", i + LB + 1, code.a);
        } break;
        case BC_BJMP: {
            printf("jump to %d", i - LB + 1);
        } break;
        case BC_CALL: {
            printf("s%02x <- (s%02x)", code.a, code.b);
            printf("(%d%s)", code.c & 0x7f, code.c >> 7 ? "..." : "");
        } break;
        case BC_RET: {
            printf("return s%02x", code.a);
        } break;
        case BC_THRW: {
            printf("throw s%02x", code.a);
        } break;
        default:
            UNREACHABLE;
    }
    printf("\n");

#undef UN
#undef BIN
#undef LB
}

void sil__disasmdef(Sil *sil, Def *d) {
    printf(
        "func %s(%d:%d) {\n", d->name == NULL ? "unnamed" : d->name, d->paramc,
        d->maxslots
    );

    for (int i = 0; i < d->ctchlen; i++) {
        CatchInfo ci = d->ctch[i];
        printf(
            "  [ try {%d:%d} catch (s%02x) {%d} ]\n", ci.begin, ci.end, ci.slot,
            ci.recover
        );
    }

    for (int i = 0; i < d->codelen; i++) {
        printf("  ");
        sil__logcode(sil, d, i);
    }

    printf("}\n");

    for (int i = 0; i < d->klen; i++) {
        if (ISGCOTYPE(d->k[i], GCO_DEF)) {
            sil__disasmdef(sil, (Def *)ASGCO(d->k[i]));
        }
    }
}

static struct {
    void (*mark)(Gco *);
    void (*free)(Sil *, Gco *);
} gcointerface[] = {
    [GCO_STR] =
        {
            .mark = markstr,
            .free = freestr,
        },
    [GCO_DOC] =
        {
            .mark = markdoc,
            .free = freedoc,
        },
    [GCO_FUNC] =
        {
            .mark = markfunc,
            .free = freefunc,
        },
    [GCO_DEF] =
        {
            .mark = markdef,
            .free = freedef,
        },
    [GCO_NONLOCAL] =  //
    {
        .mark = marknonlocal,
        .free = freenonlocal,
    },
};

void sil__markgco(Gco *o) {
    if (o->marked) return;
    o->marked = true;
    gcointerface[o->type].mark(o);
}

void sil__freegco(Sil *sil, Gco *o) {
    gcointerface[o->type].free(sil, o);
}
