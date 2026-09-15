#include "cpl.h"

#include <stdio.h>
#include <string.h>

#include "core.h"

#define MAXLOCALS 0x80
#define MAXERRMSGLEN 0x100
#define MAXENCLDENOTS 0x140
#define MAXENCLSTMTS 0x140
#define MAXENCLFUNCS 0x140
#define MAXARGS 0x7f
#define MAXPARAMS 0x10

#define BREAKPATCH (UINT8_MAX)
#define GOTOPATCH (BREAKPATCH - 1)

enum prec {
    PREC_NULL,
    PREC_COMMA,
    PREC_ASSIGN,
    PREC_THEN,
    PREC_LOR,
    PREC_LAND,
    PREC_EQ,
    PREC_IS,
    PREC_COMP,
    PREC_OR, /* https://www.lysator.liu.se/c/dmr-on-or.html */
    PREC_XOR,
    PREC_AND,
    PREC_SHIFT,
    PREC_ADD,
    PREC_MUL,
    PREC_POW,
    PREC_PREF,
    PREC_CALL,
    PREC_POST,
    PREC_ACCESS,
};

enum definertype {
    DFNR_SCRIPT,
    DFNR_FUNC,
};

typedef struct loop {
    bool continuable;
    bool unlabelable;
    bool casable;
    int scope;
    int start;
    Lx *name;
    struct loop *outer;
} Loop;

typedef struct {
    Lx name;
    int scope;
    bool nonlocal;
    bool mutable;
} LocalInfo;

typedef struct definer {
    enum definertype type;

    LocalInfo locals[MAXLOCALS];
    int localsc;

    Def *def;
    int scope;
    int slots;

    Loop *loops;

    struct definer *outer;
} Definer;

typedef struct emitter {
    void (*get)(Compiler *c, const struct emitter *e);
    void (*unget)(Compiler *c, const struct emitter *e);
    void (*getnopop)(Compiler *c, const struct emitter *e);
    void (*set)(Compiler *c, const struct emitter *e);

    struct {
        Code code;
        int slot;
    } support;
} Emitter;

static void initemitter(Emitter *e) {
    memset(e, 0, sizeof(Emitter));
}

static void errat(Parser *p, Lx lx, enum sil_result code, const char *msg) {
    Sil *sil = (Sil *)p;
    char buf[MAXERRMSGLEN];
    sprintf(buf, "%s at line %d", msg, lx.ln);
    int msglen = strlen(buf);
    Str *str = sil__newstrl(sil, buf, msglen);
    sil__clrtmp(sil);
    sil__pushtmp(sil, GCO(str));
    longjmp(sil->errjmpbuf, code);
}

static void erratprev(Parser *p, const char *msg) {
    errat(p, p->prev, SIL_UNCOMPILED, msg);
}

static void erratcurr(Parser *p, const char *msg) {
    errat(p, p->curr, SIL_UNCOMPILED, msg);
}

static bool check(Parser *p, enum lxtype type) {
    return p->curr.type == type;
}

static Lx step(Parser *p) {
    p->prev = p->curr;
    p->curr = p->nextlx(&p->lxr);
    if (p->curr.type == LX_ERROR) erratcurr(p, p->curr.start);
    return p->prev;
}

static bool stepif(Parser *p, enum lxtype type) {
    if (check(p, type)) {
        step(p);
        return true;
    }
    return false;
}

static bool stepifsemi(Parser *p) {
#ifdef AUTOSEMI
    if (stepif(p, LX_LINE) || check(p, LX_RCUB)) return true;
#endif

    return stepif(p, LX_SEMI);
}

static void expect(Parser *p, enum lxtype type, char *msg) {
    if (!stepif(p, type)) {
        if (check(p, LX_EOF)) {
            errat(p, p->prev, SIL_UNEXPECTED_EOF, "unexpected eof");
        } else {
            erratcurr(p, msg);
        }
    }
}

static void expectsemi(Parser *p) {
#ifdef AUTOSEMI
    if (stepif(p, LX_LINE) || check(p, LX_RCUB)) return;
#endif
    expect(p, LX_SEMI, "';' expected");
}

static void ignoreline(Parser *p) {
#ifdef AUTOSEMI
    stepif(p, LX_LINE);
#endif
}

static Lx expectword(Parser *p) {
    expect(p, LX_WORD, "word expected");
    return p->prev;
}

static Lx expectuniword(Parser *p) {
    if (sil__iskwtype(p->curr.type)) {
        step(p);
    } else {
        expectword(p);
    }
    return p->prev;
}

static Lx lookahead(Parser *p) {
    int parens = 1;
    Parser state = *p;
    do {
        Lx prev = step(p);
        if (prev.type == LX_LPAREN) parens++;
        else if (prev.type == LX_RPAREN) parens--;
        else if (prev.type == LX_EOF) return prev;
    } while (parens > 0);
    Lx lx = p->curr;
    *p = state;
    return lx;
}

static bool lxeq(Lx a, Lx b) {
    return a.len == b.len && memcmp(a.start, b.start, a.len) == 0;
}

#define SIL ((Sil *)c)

#define PARSER (&c->parser)
#define PREV (PARSER->prev)
#define CURR (PARSER->curr)

#define DEFINER (c->definer)
#define DEF (DEFINER->def)
#define SLOTS (DEFINER->slots)

#define NEXTSLOT (SLOTS)
#define LASTSLOT (NEXTSLOT - 1)

static void prefix(Compiler *c, Emitter *e);
static void klass(Compiler *c, Emitter *e);
static void literal(Compiler *c, Emitter *e);
static void enum_(Compiler *c, Emitter *e);
static void func(Compiler *c, Emitter *e);
static void then(Compiler *c, Emitter *e);
static void and_(Compiler *c, Emitter *e);
static void new_(Compiler *c, Emitter *e);
static void or_(Compiler *c, Emitter *e);
static void comma(Compiler *c, Emitter *e);
static void infix(Compiler *c, Emitter *e);
static void dot(Compiler *c, Emitter *e);
static void arrow(Compiler *c, Emitter *e);
static void parens(Compiler *c, Emitter *e);
static void call(Compiler *c, Emitter *e);
static void index(Compiler *c, Emitter *e);
static void doc(Compiler *c, Emitter *e);
static void word(Compiler *c, Emitter *e);
static void num(Compiler *c, Emitter *e);
static void str(Compiler *c, Emitter *e);

static void eof(Compiler *c, Emitter *e) {
    errat(PARSER, PREV, SIL_UNEXPECTED_EOF, "unexpected eof");
}

static struct {
    enum prec prec;
    void (*nud)(Compiler *c, Emitter *e);
    void (*led)(Compiler *c, Emitter *e);
} rules[] = {
    [LX_CONTINUE] = {PREC_NULL, NULL, NULL},
    [LX_DEFAULT] = {PREC_NULL, NULL, NULL},
    [LX_FINALLY] = {PREC_NULL, NULL, NULL},
    [LX_DELETE] = {PREC_NULL, prefix, NULL},
    [LX_RETURN] = {PREC_NULL, NULL, NULL},
    [LX_SWITCH] = {PREC_NULL, NULL, NULL},
    [LX_TYPEOF] = {PREC_NULL, prefix, NULL},
    [LX_BREAK] = {PREC_NULL, NULL, NULL},
    [LX_CATCH] = {PREC_NULL, NULL, NULL},
    [LX_CLASS] = {PREC_NULL, klass, NULL},
    [LX_CONST] = {PREC_NULL, NULL, NULL},
    [LX_FALSE] = {PREC_NULL, literal, NULL},
    [LX_THROW] = {PREC_NULL, NULL, NULL},
    [LX_WHILE] = {PREC_NULL, NULL, NULL},
    [LX_CASE] = {PREC_NULL, NULL, NULL},
    [LX_ELSE] = {PREC_NULL, NULL, NULL},
    [LX_ENUM] = {PREC_NULL, enum_, NULL},
    [LX_FUNC] = {PREC_NULL, func, NULL},
    [LX_GOTO] = {PREC_NULL, NULL, NULL},
    [LX_THEN] = {PREC_THEN, NULL, then},
    [LX_TRUE] = {PREC_NULL, literal, NULL},
    [LX_VOID] = {PREC_NULL, literal, NULL},
    [LX_AND] = {PREC_LAND, NULL, and_},
    [LX_FOR] = {PREC_NULL, NULL, NULL},
    [LX_NEW] = {PREC_NULL, new_, NULL},
    [LX_NOT] = {PREC_NULL, prefix, NULL},
    [LX_TRY] = {PREC_NULL, NULL, NULL},
    [LX_VAR] = {PREC_NULL, NULL, NULL},
    [LX_DO] = {PREC_NULL, NULL, NULL},
    [LX_IF] = {PREC_NULL, NULL, NULL},
    [LX_IS] = {PREC_IS, NULL, infix},
    [LX_OR] = {PREC_LOR, NULL, or_},

    [LX_COMMA] = {PREC_COMMA, NULL, comma},
    [LX_EQUALS] = {PREC_NULL, NULL, NULL},
    [LX_PLUS_EQUALS] = {PREC_NULL, NULL, NULL},
    [LX_MINUS_EQUALS] = {PREC_NULL, NULL, NULL},
    [LX_AST_EQUALS] = {PREC_NULL, NULL, NULL},
    [LX_SOL_EQUALS] = {PREC_NULL, NULL, NULL},
    [LX_PERCNT_EQUALS] = {PREC_NULL, NULL, NULL},
    [LX_AST_AST_EQUALS] = {PREC_NULL, NULL, NULL},
    [LX_SOL_SOL_EQUALS] = {PREC_NULL, NULL, NULL},
    [LX_VERBAR_EQUALS] = {PREC_NULL, NULL, NULL},
    [LX_HAT_EQUALS] = {PREC_NULL, NULL, NULL},
    [LX_AMP_EQUALS] = {PREC_NULL, NULL, NULL},
    [LX_LT_LT_EQUALS] = {PREC_NULL, NULL, NULL},
    [LX_GT_GT_EQUALS] = {PREC_NULL, NULL, NULL},
    [LX_PLUS_PLUS] = {PREC_NULL, prefix, NULL},
    [LX_MINUS_MINUS] = {PREC_NULL, prefix, NULL},
    [LX_QUEST] = {PREC_THEN, NULL, then},
    [LX_COLON] = {PREC_NULL, NULL, NULL},
    [LX_VERBAR_VERBAR] = {PREC_LOR, NULL, or_},
    [LX_AMP_AMP] = {PREC_LAND, NULL, and_},
    [LX_VERBAR] = {PREC_OR, NULL, infix},
    [LX_HAT] = {PREC_XOR, NULL, infix},
    [LX_AMP] = {PREC_AND, NULL, infix},
    [LX_EQUALS_EQUALS] = {PREC_EQ, NULL, infix},
    [LX_EXCL_EQUALS] = {PREC_EQ, NULL, infix},
    [LX_LT] = {PREC_COMP, NULL, infix},
    [LX_LT_EQUALS] = {PREC_COMP, NULL, infix},
    [LX_GT] = {PREC_COMP, NULL, infix},
    [LX_GT_EQUALS] = {PREC_COMP, NULL, infix},
    [LX_LT_LT] = {PREC_SHIFT, NULL, infix},
    [LX_GT_GT] = {PREC_SHIFT, NULL, infix},
    [LX_PLUS] = {PREC_ADD, prefix, infix},
    [LX_MINUS] = {PREC_ADD, prefix, infix},
    [LX_AST] = {PREC_MUL, NULL, infix},
    [LX_SOL] = {PREC_MUL, NULL, infix},
    [LX_PERCNT] = {PREC_MUL, NULL, infix},
    [LX_AST_AST] = {PREC_POW, NULL, infix},
    [LX_SOL_SOL] = {PREC_MUL, NULL, infix},
    [LX_EXCL] = {PREC_NULL, prefix, NULL},
    [LX_TILDE] = {PREC_IS, prefix, infix},
    [LX_DOT] = {PREC_ACCESS, NULL, dot},
    [LX_MINUS_GT] = {PREC_ACCESS, NULL, arrow},
    [LX_LPAREN] = {PREC_CALL, parens, call},
    [LX_RPAREN] = {PREC_NULL, NULL, NULL},
    [LX_LSQB] = {PREC_ACCESS, NULL, index},
    [LX_RSQB] = {PREC_NULL, NULL, NULL},
    [LX_LCUB] = {PREC_NULL, doc, NULL},
    [LX_RCUB] = {PREC_NULL, NULL, NULL},
    [LX_HELLIP] = {PREC_NULL, NULL, NULL},
    [LX_EQUALS_GT] = {PREC_NULL, NULL, NULL},
    [LX_SEMI] = {PREC_NULL, NULL, NULL},

    [LX_WORD] = {PREC_NULL, word, NULL},
    [LX_NUM] = {PREC_NULL, num, NULL},
    [LX_STR] = {PREC_NULL, str, NULL},

    [LX_LINE] = {PREC_NULL, NULL, NULL},
    [LX_ERROR] = {PREC_NULL, NULL, NULL},
    [LX_EOF] = {PREC_NULL, eof, NULL},
};

static int writecode(Compiler *c, Code code, int effect) {
    int idx = sil__defaddcode(SIL, DEF, code, PREV.ln, SLOTS);

    ASSERT(0 <= (SLOTS) + effect, "effect underflow");

    SLOTS += effect;
    if (SLOTS > UINT8_MAX) erratprev(PARSER, "too many slots used");
    if (SLOTS > DEF->maxslots) DEF->maxslots = SLOTS;

    return idx;
}

static int writeabc(
    Compiler *c,
    enum op op,
    byte aarg,
    byte barg,
    byte carg,
    int effect
) {
    return writecode(c, (Code){op, aarg, barg, carg}, effect);
}

static int writealb(Compiler *c, enum op op, byte aarg, int lbarg, int effect) {
    return writeabc(c, op, aarg, (lbarg >> 8) & 0xff, lbarg & 0xff, effect);
}

static int addk(Compiler *c, Value k) {
    if (DEF->klen + 1 > UINT16_MAX) erratprev(PARSER, "too many constants");

    sil__pushtmp(SIL, k);
    int idx = sil__defaddk(SIL, DEF, k);
    sil__clrtmp(SIL);
    return idx;
}

static void pushk(Compiler *c, Value k) {
    writealb(c, BC_LDK, NEXTSLOT, addk(c, k), +1);
}

static int addstr(Compiler *c, const char *chars, int len) {
    Str *str = sil__newstrl(SIL, chars, len);
    return addk(c, GCO(str));
}

static void pushstr(Compiler *c, const char *chars, int len) {
    writealb(c, BC_LDK, NEXTSLOT, addstr(c, chars, len), +1);
}

static int addstrlx(Compiler *c, Lx lx) {
    return addstr(c, lx.start, lx.len);
}

static void pushstrlx(Compiler *c, Lx lx) {
    writealb(c, BC_LDK, NEXTSLOT, addstrlx(c, lx), +1);
}

static void writebackflip(Compiler *c, int to) {
    int jumplen = DEF->codelen - to + 1;
    if (jumplen > UINT16_MAX) erratprev(PARSER, "too long jump");
    writealb(c, BC_BJMP, 0, jumplen, 0);
}

static int writejump(Compiler *c, enum op op, byte a_, int effect) {
    writealb(c, op, a_, 0xffff, effect);
    return DEF->codelen - 1;
}

static void patchjumpto(Compiler *c, int from, int to) {
    int jumplen = to - from - 1;
    if (jumplen > UINT16_MAX) erratprev(PARSER, "too long jump");
    DEF->code[from].b = (jumplen >> 8) & 0xff;
    DEF->code[from].c = jumplen & 0xff;
}

static void patchjump(Compiler *c, int from) {
    patchjumpto(c, from, DEF->codelen);
}

static void closenlstil(Compiler *c, int til) {
    int nlc = 0;
    for (int i = DEFINER->localsc - 1; i > til; i--)
        if (DEFINER->locals[i].nonlocal) nlc++;
    if (nlc > 0) writealb(c, BC_CLNL, 0, nlc, 0);
}

static void closenlstilscope(Compiler *c, int scope) {
    int i;
    for (i = DEFINER->localsc - 1; i >= 0; i--)
        if (DEFINER->locals[i].scope <= scope) break;
    closenlstil(c, i);
}

static void writeret(Compiler *c) {
    closenlstilscope(c, -1);
    writealb(c, BC_RET, LASTSLOT, 0, -1);
}

static void writenilret(Compiler *c) {
    writealb(c, BC_NIL, NEXTSLOT, 0, +1);
    writeret(c);
}

static void beginscope(Compiler *c) {
    DEFINER->scope++;
}

static void endscope(Compiler *c) {
    DEFINER->scope--;

    closenlstilscope(c, DEFINER->scope);

    for (int i = DEFINER->localsc - 1; i >= 0; i--) {
        if (DEFINER->locals[i].scope > DEFINER->scope) {
            SLOTS--;
            DEFINER->localsc--;
        } else {
            break;
        }
    }
}

static void declaration(Compiler *c);
static void statement(Compiler *c);
static void expression(Compiler *c, enum prec prec);

static void ifassign(Compiler *c, enum prec prec, Emitter *e) {
    if (!sil__isassigntype(CURR.type)) {
        if (e->get != NULL) e->get(c, e);
        return;
    }

    if (e->set == NULL) erratcurr(PARSER, "invalid left-hand side");

    if (prec <= PREC_POST &&
        (stepif(PARSER, LX_PLUS_PLUS) || stepif(PARSER, LX_MINUS_MINUS))) {
        enum op op = PREV.type == LX_PLUS_PLUS ? BC_ADD : BC_SUB;
        e->getnopop(c, e);
        pushk(c, NUM(1));
        writeabc(c, op, LASTSLOT, LASTSLOT - 1, LASTSLOT, 0);
        e->set(c, e);
    } else if (prec <= PREC_ASSIGN) {
        if (stepif(PARSER, LX_EQUALS)) {
            expression(c, PREC_ASSIGN);
            e->set(c, e);
        } else {
            static enum op ops[] = {
                [LX_PLUS_EQUALS - LX_PLUS_EQUALS] = BC_ADD,
                [LX_MINUS_EQUALS - LX_PLUS_EQUALS] = BC_SUB,
                [LX_AST_EQUALS - LX_PLUS_EQUALS] = BC_MUL,
                [LX_AST_AST_EQUALS - LX_PLUS_EQUALS] = BC_POW,
                [LX_SOL_EQUALS - LX_PLUS_EQUALS] = BC_FDIV,
                [LX_SOL_SOL_EQUALS - LX_PLUS_EQUALS] = BC_IDIV,
                [LX_PERCNT_EQUALS - LX_PLUS_EQUALS] = BC_MOD,
                [LX_AMP_EQUALS - LX_PLUS_EQUALS] = BC_AND,
                [LX_HAT_EQUALS - LX_PLUS_EQUALS] = BC_XOR,
                [LX_VERBAR_EQUALS - LX_PLUS_EQUALS] = BC_OR,
                [LX_LT_LT_EQUALS - LX_PLUS_EQUALS] = BC_LSH,
                [LX_GT_GT_EQUALS - LX_PLUS_EQUALS] = BC_RSH,
            };
            step(PARSER);
            enum op op = ops[PREV.type - LX_PLUS_EQUALS];
            e->getnopop(c, e);
            expression(c, PREC_ASSIGN);
            writeabc(c, op, LASTSLOT - 1, LASTSLOT - 1, LASTSLOT, -1);
            e->set(c, e);
        }
    }

    initemitter(e);
}

static void denotation(Compiler *c, enum prec prec, Emitter *oe) {
    Emitter le;
    Emitter *e = oe != NULL ? oe : &le;

    if (++c->limit.denot > MAXENCLDENOTS)
        erratprev(PARSER, "too many enclosing denotations");

    initemitter(e);

    if (rules[PREV.type].nud == NULL) erratprev(PARSER, "expression expected");
    rules[PREV.type].nud(c, e);
    ifassign(c, prec, e);

    while (prec <= rules[CURR.type].prec) {
        step(PARSER);
        rules[PREV.type].led(c, e);
        ifassign(c, prec, e);
    }

    c->limit.denot--;
}

static void expr(Compiler *c, enum prec prec, Emitter *e) {
    step(PARSER);
    denotation(c, prec, e);
}

static void expression(Compiler *c, enum prec prec) {
    expr(c, prec, NULL);
}

static void indexgetnopop(Compiler *c, const Emitter *e) {
    writeabc(c, BC_LDI, NEXTSLOT, LASTSLOT - 1, LASTSLOT, +1);
}

static void indexget(Compiler *c, const Emitter *e) {
    writeabc(c, BC_LDI, LASTSLOT - 1, LASTSLOT - 1, LASTSLOT, -1);
}

static void indexunget(Compiler *c, const Emitter *e) {
    DEF->codelen--;
    SLOTS++;
}

static void indexset(Compiler *c, const Emitter *e) {
    int start = e->support.slot - 1;
    writeabc(c, BC_STI, start, start + 1, LASTSLOT, start + 2 - LASTSLOT);
    writeabc(c, BC_COPY, start, start + 2, 0, start - LASTSLOT);
}

static void dot(Compiler *c, Emitter *e) {
    Lx name = expectuniword(PARSER);
    Str *strname = sil__newstrl(SIL, name.start, name.len);
    pushk(c, GCO(strname));

    e->unget = indexunget;
    e->getnopop = indexgetnopop;
    e->get = indexget;
    e->set = indexset;

    e->support.slot = LASTSLOT;
}

static void comma(Compiler *c, Emitter *e) {
    SLOTS--;
    expression(c, PREC_COMMA);

    initemitter(e);
}

static void then(Compiler *c, Emitter *e) {
    ignoreline(PARSER);

    int thenjump = writejump(c, BC_JMPF, LASTSLOT, -1);
    expression(c, PREC_COMMA);

#ifdef ALTKEYWORDS
    if (!stepif(PARSER, LX_ELSE)) expect(PARSER, LX_COLON, "':' expected");
#else
    expect(PARSER, LX_COLON, "':' expected");
#endif
    ignoreline(PARSER);

    int elsejump = writejump(c, BC_JMP, 0, -1);
    patchjump(c, thenjump);
    expression(c, PREC_ASSIGN);
    patchjump(c, elsejump);

    initemitter(e);
}

static int arglist(Compiler *c, bool *spread) {
    int argc = 0;
    while (!check(PARSER, LX_RPAREN)) {
        expression(c, PREC_ASSIGN);
        if (stepif(PARSER, LX_HELLIP)) {
            *spread = true;
            stepif(PARSER, LX_COMMA);
            break;
        }
        argc++;

        if (!stepif(PARSER, LX_COMMA)) break;
    }

    ignoreline(PARSER);
    expect(PARSER, LX_RPAREN, "')' expected");
    return argc;
}

static void arrow(Compiler *c, Emitter *e) {
    pushstrlx(c, expectuniword(PARSER));
    writeabc(c, BC_LDI, LASTSLOT, LASTSLOT - 1, LASTSLOT, 0);

    while (stepif(PARSER, LX_MINUS_GT)) {
        pushstrlx(c, expectuniword(PARSER));
        writeabc(c, BC_LDI, LASTSLOT - 1, LASTSLOT - 1, LASTSLOT, -1);
    }

    expect(PARSER, LX_LPAREN, "'(' expected");
    writeabc(c, BC_SWAP, LASTSLOT, LASTSLOT - 1, 0, 0);
    bool spr = false;
    int argc = arglist(c, &spr) + 1;
    if (argc > MAXARGS) erratprev(PARSER, "too many arguments");
    int cleslot = LASTSLOT - argc - spr;
    writeabc(c, BC_CALL, cleslot, cleslot, argc | spr * 0x80, -argc - spr);

    initemitter(e);
}

static void namedfunc(Compiler *c, Lx *name, bool isarrow, Lx *param);
static Code resolvenamecode(Compiler *c, Lx name, int slot);

static void doclit(Compiler *c) {
    double i = 0;
    bool withlen = stepif(PARSER, LX_COMMA);

    while (!check(PARSER, LX_RCUB)) {
        if (stepif(PARSER, LX_DOT)) {
            Lx name = expectuniword(PARSER);
            pushstrlx(c, name);
            if (stepif(PARSER, LX_EQUALS)) expression(c, PREC_ASSIGN);
            else if (check(PARSER, LX_LPAREN)) namedfunc(c, &name, false, NULL);
            else writecode(c, resolvenamecode(c, name, NEXTSLOT), +1);
        } else if (stepif(PARSER, LX_MINUS_GT)) {
            Lx name = expectuniword(PARSER);
            pushstrlx(c, name);
            Lx thislx = LITLX("this");
            namedfunc(c, &name, false, &thislx);
        } else if (stepif(PARSER, LX_LSQB)) {
            expression(c, PREC_COMMA);
            expect(PARSER, LX_RSQB, "']' expected");
            expect(PARSER, LX_EQUALS, "'=' expected");
            expression(c, PREC_ASSIGN);
        } else {
            pushk(c, NUM(i++));
            expression(c, PREC_ASSIGN);
        }

        writeabc(c, BC_STI, LASTSLOT - 2, LASTSLOT - 1, LASTSLOT, -2);

        if (!stepif(PARSER, LX_COMMA)) break;
    }

    ignoreline(PARSER);
    expect(PARSER, LX_RCUB, "'}' expected");

    if (i > 0 || withlen) {
        pushstr(c, "__len", 5);
        pushk(c, NUM(i));
        writeabc(c, BC_STI, LASTSLOT - 2, LASTSLOT - 1, LASTSLOT, -2);
    }
}

static void parens(Compiler *c, Emitter *e) {
    if (lookahead(PARSER).type == LX_EQUALS_GT) {
        namedfunc(c, NULL, true, NULL);
        return;
    }

    expression(c, PREC_COMMA);
    ignoreline(PARSER);
    expect(PARSER, LX_RPAREN, "')' expected");

    if (stepif(PARSER, LX_LCUB)) {
        writeabc(c, BC_DOC, LASTSLOT, LASTSLOT, 0, 0);
        doclit(c);
    }

    initemitter(e);
}

static void call(Compiler *c, Emitter *e) {
    bool spr = false;
    int argc = arglist(c, &spr);
    if (argc > MAXARGS) erratprev(PARSER, "too many arguments");
    int cleslot = LASTSLOT - argc - spr;
    writeabc(c, BC_CALL, cleslot, cleslot, argc | spr * 0x80, -argc - spr);

    initemitter(e);
}

static void index(Compiler *c, Emitter *e) {
    expression(c, PREC_COMMA);
    expect(PARSER, LX_RSQB, "']' expected");

    e->unget = indexunget;
    e->getnopop = indexgetnopop;
    e->get = indexget;
    e->set = indexset;

    e->support.slot = LASTSLOT;
}

static void doc(Compiler *c, Emitter *e) {
    writealb(c, BC_NIL, NEXTSLOT, 0, +1);
    writeabc(c, BC_DOC, LASTSLOT, LASTSLOT, 0, 0);
    doclit(c);

    initemitter(e);
}

static int addlocal(Compiler *c, Lx name) {
    if (DEFINER->localsc + 1 > MAXLOCALS)
        erratprev(PARSER, "too many local variables");

    DEFINER->locals[DEFINER->localsc++] = (LocalInfo){
        .name = name,
        .scope = DEFINER->scope,
        .nonlocal = false,
        .mutable = true,
    };

    return DEFINER->localsc - 1;
}

static void func(Compiler *c, Emitter *e) {
    ignoreline(PARSER);

    if (stepif(PARSER, LX_WORD)) {
        beginscope(c);

        Lx name = PREV;
        addlocal(c, name);
        namedfunc(c, &name, false, NULL);

        endscope(c);
        SLOTS++;
    } else {
        namedfunc(c, NULL, false, NULL);
    }

    initemitter(e);
}

static void literal(Compiler *c, Emitter *e) {
    switch (PREV.type) {
        case LX_VOID: {
            writealb(c, BC_NIL, NEXTSLOT, 0, +1);
        } break;
        case LX_FALSE: {
            writealb(c, BC_FLS, NEXTSLOT, 0, +1);
        } break;
        case LX_TRUE: {
            writealb(c, BC_TRUE, NEXTSLOT, 0, +1);
        } break;
        default:
            UNREACHABLE;
    }

    initemitter(e);
}

static void new_(Compiler *c, Emitter *e) {
    ignoreline(PARSER);

    expression(c, PREC_POST);
    writeabc(c, BC_COPY, NEXTSLOT, LASTSLOT, 0, +1);
    writeabc(c, BC_DOC, LASTSLOT - 1, LASTSLOT - 1, 0, 0);
    pushstr(c, "__init", 6);
    writeabc(c, BC_LDI, LASTSLOT, LASTSLOT - 1, LASTSLOT, 0);
    writeabc(c, BC_COPY, NEXTSLOT, LASTSLOT - 2, 0, +1);

    int argc;
    bool spr = false;
    if (stepif(PARSER, LX_LPAREN)) {
        argc = arglist(c, &spr) + 1;
        if (argc > MAXARGS) erratprev(PARSER, "too many arguments");
    } else {
        argc = 1;
    }
    int cleslot = LASTSLOT - argc - spr;
    writeabc(c, BC_CALL, cleslot, cleslot, argc | spr * 0x80, -argc - spr - 2);

    initemitter(e);
}

static void prefix(Compiler *c, Emitter *e) {
#define WRITE(op) writeabc(c, op, LASTSLOT, LASTSLOT, 0, 0)

    Lx prev = PREV;

    ignoreline(PARSER);

    int pref = 0;
    if (prev.type == LX_PLUS_PLUS) pref = 1;
    else if (prev.type == LX_MINUS_MINUS) pref = -1;

    if (pref != 0) {
        Emitter le;

        enum op op = pref > 0 ? BC_ADD : BC_SUB;

        expr(c, PREC_PREF, &le);

        if (le.set == NULL) erratprev(PARSER, "invalid left-hand side");

        le.unget(c, &le);
        le.getnopop(c, &le);
        pushk(c, NUM(1));
        writeabc(c, op, LASTSLOT - 1, LASTSLOT - 1, LASTSLOT, -1);
        le.set(c, &le);
    } else {
        Emitter le;
        expr(c, PREC_PREF, &le);

        switch (prev.type) {
            case LX_PLUS: {
                WRITE(BC_POS);
            } break;
            case LX_MINUS: {
                WRITE(BC_NEG);
            } break;
            case LX_TYPEOF: {
                WRITE(BC_TPOF);
            } break;
            case LX_EXCL:
            case LX_NOT: {
                WRITE(BC_NOT);
            } break;
            case LX_TILDE: {
                WRITE(BC_REV);
            } break;
            case LX_DELETE: {
                if (le.get != indexget)
                    erratprev(PARSER, "index access expected");
                DEF->code[DEF->codelen - 1].op = BC_DEL;
            } break;
            default:
                UNREACHABLE;
        }
    }

    initemitter(e);

#undef WRITE
}

static void klass(Compiler *c, Emitter *e) {
    erratprev(PARSER, "class is not supported");
}

static void enum_(Compiler *c, Emitter *e) {
    ignoreline(PARSER);

    Lx name = LITLX("__enum");
    if (stepif(PARSER, LX_WORD)) {
        name = PREV;
        ignoreline(PARSER);
    }

    expect(PARSER, LX_LCUB, "'{' expected");

    beginscope(c);

    writealb(c, BC_NIL, NEXTSLOT, 0, +1);
    writeabc(c, BC_DOC, LASTSLOT, LASTSLOT, 0, 0);
    addlocal(c, name);

    int docslot = LASTSLOT;
    bool zero = true;
    int enumc = 0;
    while (!check(PARSER, LX_RCUB)) {
        Lx name = expectuniword(PARSER);
        if (stepif(PARSER, LX_EQUALS)) {
            expression(c, PREC_ASSIGN);
        } else {
            if (zero) {
                pushk(c, NUM(0));
            } else {
                pushk(c, NUM(1));
                writeabc(c, BC_ADD, LASTSLOT, LASTSLOT - 1, LASTSLOT, 0);
            }
        }
        zero = false;
        enumc++;

        pushstrlx(c, name);
        writeabc(c, BC_STI, docslot, LASTSLOT, LASTSLOT - 1, -1);

        if (!stepif(PARSER, LX_COMMA)) break;
    }

    ignoreline(PARSER);
    expect(PARSER, LX_RCUB, "'}' expected");

    endscope(c);
    SLOTS -= enumc - 1;

    initemitter(e);
}

static bool isleftasso(enum lxtype type) {
    return type != LX_AST_AST;
}

static void infix(Compiler *c, Emitter *e) {
#define WRITE(op) writeabc(c, op, LASTSLOT - 1, LASTSLOT - 1, LASTSLOT, -1)

    Lx prev = PREV;
    ignoreline(PARSER);
    expression(c, rules[prev.type].prec + isleftasso(prev.type));
    switch (prev.type) {
        case LX_PLUS: {
            WRITE(BC_ADD);
        } break;
        case LX_MINUS: {
            WRITE(BC_SUB);
        } break;
        case LX_AST: {
            WRITE(BC_MUL);
        } break;
        case LX_SOL: {
            WRITE(BC_FDIV);
        } break;
        case LX_AST_AST: {
            WRITE(BC_POW);
        } break;
        case LX_SOL_SOL: {
            WRITE(BC_IDIV);
        } break;
        case LX_PERCNT: {
            WRITE(BC_MOD);
        } break;
        case LX_AMP: {
            WRITE(BC_AND);
        } break;
        case LX_HAT: {
            WRITE(BC_XOR);
        } break;
        case LX_VERBAR: {
            WRITE(BC_OR);
        } break;
        case LX_LT_LT: {
            WRITE(BC_LSH);
        } break;
        case LX_GT_GT: {
            WRITE(BC_RSH);
        } break;
        case LX_LT: {
            WRITE(BC_LT);
        } break;
        case LX_GT: {
            WRITE(BC_LE);
            writeabc(c, BC_NOT, LASTSLOT, LASTSLOT, 0, 0);
        } break;
        case LX_LT_EQUALS: {
            WRITE(BC_LE);
        } break;
        case LX_GT_EQUALS: {
            WRITE(BC_LT);
            writeabc(c, BC_NOT, LASTSLOT, LASTSLOT, 0, 0);
        } break;
        case LX_EQUALS_EQUALS: {
            WRITE(BC_EQ);
        } break;
        case LX_EXCL_EQUALS: {
            WRITE(BC_EQ);
            writeabc(c, BC_NOT, LASTSLOT, LASTSLOT, 0, 0);
        } break;
        case LX_IS:
        case LX_TILDE: {
            WRITE(BC_IS);
        } break;
        default:
            UNREACHABLE;
    }

    initemitter(e);

#undef WRITE
}

static void and_(Compiler *c, Emitter *e) {
    ignoreline(PARSER);

    int jump = writejump(c, BC_JMPF, LASTSLOT, -1);
    expression(c, PREC_LAND);
    patchjump(c, jump);

    initemitter(e);
}

static void or_(Compiler *c, Emitter *e) {
    ignoreline(PARSER);

    int jump1 = writejump(c, BC_JMPF, LASTSLOT, 0);
    int jump2 = writejump(c, BC_JMP, 0, -1);
    patchjump(c, jump1);
    expression(c, PREC_LOR);
    patchjump(c, jump2);

    initemitter(e);
}

static int resolvel(Definer *d, Lx *name) {
    for (int i = d->localsc - 1; i >= 0; i--) {
        LocalInfo li = d->locals[i];
        if (lxeq(*name, li.name)) {
            return i;
        }
    }

    return -1;
}

static int resolvenl(Definer *d, Sil *sil, Lx *name) {
    int i;
    if (d->outer == NULL) {
        return -1;
    } else if ((i = resolvel(d->outer, name)) >= 0) {
        d->outer->locals[i].nonlocal = true;
        return sil__defaddnli(sil, d->def, true, i);
    } else if ((i = resolvenl(d->outer, sil, name)) >= 0) {
        return sil__defaddnli(sil, d->def, false, i);
    } else {
        return -1;
    }
}

enum nametype {
    NAME_GLOBAL,
    NAME_LOCAL,
    NAME_NONLOCAL,
};

typedef struct {
    enum nametype type;
    int index;
} NameInfo;

static NameInfo resolvename(Compiler *c, Lx name) {
    NameInfo ni;
    if ((ni.index = resolvel(DEFINER, &name)) >= 0) {
        ni.type = NAME_LOCAL;
    } else if ((ni.index = resolvenl(DEFINER, SIL, &name)) >= 0) {
        if (ni.index > UINT16_MAX)
            errat(PARSER, name, SIL_UNCOMPILED, "too many nonlocal variables");

        ni.type = NAME_NONLOCAL;
    } else {
        ni.index = addstrlx(c, name);
        ni.type = NAME_GLOBAL;
    }
    return ni;
}

static void wordget(Compiler *c, const Emitter *e) {
    writecode(c, e->support.code, +1);
}

static void wordunget(Compiler *c, const Emitter *e) {
    DEF->codelen--;
    SLOTS--;
}

static Code namecodetolhs(Code code, int slot) {
    switch (code.op) {
        case BC_COPY: {
            code.a = code.b;
            code.b = slot;
        } break;
        case BC_LDNL: {
            code.op = BC_STNL;
            code.a = slot;
        } break;
        case BC_LDG: {
            code.op = BC_STG;
            code.a = slot;
        } break;
    }

    return code;
}

static void wordset(Compiler *c, const Emitter *e) {
    Code code = namecodetolhs(e->support.code, LASTSLOT);
    writecode(c, code, e->support.slot - LASTSLOT);
}

static Code resolvenamecode(Compiler *c, Lx name, int slot) {
    Code code;
    NameInfo ni = resolvename(c, name);

    switch (ni.type) {
        case NAME_GLOBAL: {
            code.op = BC_LDG;
            code.a = slot;
            code.b = (ni.index >> 8) & 0xff;
            code.c = ni.index & 0xff;
        } break;
        case NAME_LOCAL: {
            code.op = BC_COPY;
            code.a = slot;
            code.b = ni.index;
            code.c = 0;
        } break;
        case NAME_NONLOCAL: {
            code.op = BC_LDNL;
            code.a = slot;
            code.b = (ni.index >> 8) & 0xff;
            code.c = ni.index & 0xff;
        } break;
        default:
            UNREACHABLE;
    }

    return code;
}

static void word(Compiler *c, Emitter *e) {
    Lx name = PREV;

    if (CURR.type == LX_EQUALS_GT) {
        namedfunc(c, NULL, true, &name);
        initemitter(e);
        return;
    }

    e->support.code = resolvenamecode(c, name, NEXTSLOT);

    e->unget = wordunget;
    e->getnopop = wordget;
    e->get = wordget;
    e->set = wordset;
    e->support.slot = DEFINER->slots;
}

static void num(Compiler *c, Emitter *e) {
    pushk(c, NUM(sil__atofl(SIL, PREV.start, PREV.len)));

    initemitter(e);
}

static void str(Compiler *c, Emitter *e) {
    Sil *sil = SIL;
    int len = PREV.len - 2;

    char *buf = sil__dupl(sil, PREV.start + 1, len);

    int cur = 0;
    for (int i = 0; i < len; i++) {
        char wr;
        if (buf[i] == '\\') {
            switch (buf[++i]) {
                case '0':
                    wr = '\0';
                    break;
                case '"':
                    wr = '"';
                    break;
                case '\\':
                    wr = '\\';
                    break;
                case 'a':
                    wr = '\a';
                    break;
                case 'b':
                    wr = '\b';
                    break;
                case 'e':
                    wr = '\x1b';
                    break;
                case 'f':
                    wr = '\f';
                    break;
                case 'n':
                    wr = '\n';
                    break;
                case 'r':
                    wr = '\r';
                    break;
                case 't':
                    wr = '\t';
                    break;
                case 'v':
                    wr = '\v';
                    break;
                case 'x': {
                    int x0 = sil__ctoi(buf[++i], 0x10);
                    if (x0 < 0) goto err;

                    int x1 = sil__ctoi(buf[++i], 0x10);
                    if (x1 < 0) goto err;

                    wr = x0 * 0x10 + x1;
                } break;
                default:
                    goto err;
            }
        } else if (buf[i] == '%') {
            wr = buf[i++];
        } else {
            wr = buf[i];
        }
        buf[cur++] = wr;
    }

    pushstr(c, buf, cur);
    FREE(buf, len);

    if (PREV.start[PREV.len - 1] == '%') {
        if (stepif(PARSER, LX_LPAREN)) {
            expression(c, PREC_COMMA);

            PARSER->nextlx = sil__lexstr;
            expect(PARSER, LX_RPAREN, "')' expected");
            PARSER->nextlx = sil__lex;
        } else {
            PARSER->nextlx = sil__lexstr;
            Lx name = expectword(PARSER);
            PARSER->nextlx = sil__lex;

            writecode(c, resolvenamecode(c, name, NEXTSLOT), +1);
        }
        writeabc(c, BC_ADD, LASTSLOT - 1, LASTSLOT - 1, LASTSLOT, -1);

        ASSERT(CURR.type == LX_STR, "error");
        step(PARSER);
        str(c, e);
        writeabc(c, BC_ADD, LASTSLOT - 1, LASTSLOT - 1, LASTSLOT, -1);
    }

    initemitter(e);

    return;

err:
    FREE(buf, sizeof(char) * len);
    erratprev(PARSER, "invalid escape");
}

static void beginloop(
    Compiler *c,
    Loop *loop,
    bool continuable,
    bool unlabelable,
    Lx *name
) {
    loop->name = name;
    loop->start = DEF->codelen;
    loop->scope = DEFINER->scope;
    loop->continuable = continuable;
    loop->unlabelable = unlabelable;
    loop->casable = false;

    loop->outer = DEFINER->loops;
    DEFINER->loops = loop;
}

static void endloop(Compiler *c) {
    for (int i = DEFINER->loops->start; i < DEF->codelen; i++) {
        if (DEF->code[i].op == BREAKPATCH) {
            Code code = DEF->code[i];
            int level = (code.b << 8) | (code.c & 0xff);
            if (level == 0) {
                DEF->code[i].op = BC_JMP;
                patchjump(c, i);
            } else {
                level--;
                DEF->code[i].b = (level >> 8) & 0xff;
                DEF->code[i].c = level & 0xff;
            }
        }
    }

    DEFINER->loops = DEFINER->loops->outer;
}

static void vardecl(Compiler *c);
static void funcdecl(Compiler *c);
static void enumdecl(Compiler *c);

static void block(Compiler *c) {
    beginscope(c);
    while (!stepif(PARSER, LX_RCUB)) declaration(c);
    endscope(c);
}

static void blockstmt(Compiler *c, Lx *label) {
    Loop loop;
    beginloop(c, &loop, false, false, label);

    block(c);
    ignoreline(PARSER);

    endloop(c);
}

static void ifstmt(Compiler *c, Lx *label) {
    ignoreline(PARSER);

    Loop loop;
    beginloop(c, &loop, false, false, label);
    beginscope(c);

    bool rev = stepif(PARSER, LX_EXCL) || stepif(PARSER, LX_NOT);

    expect(PARSER, LX_LPAREN, "'(' expected");
    if (stepif(PARSER, LX_VAR)) vardecl(c);
    expression(c, PREC_COMMA);
    if (rev) writeabc(c, BC_NOT, LASTSLOT, LASTSLOT, 0, 0);
    ignoreline(PARSER);
    expect(PARSER, LX_RPAREN, "')' expected");
    ignoreline(PARSER);

    int thenjump = writejump(c, BC_JMPF, LASTSLOT, -1);

    statement(c);

    int elsejump = writejump(c, BC_JMP, 0, 0);

    patchjump(c, thenjump);

    if (stepif(PARSER, LX_ELSE)) statement(c);

    patchjump(c, elsejump);

    endscope(c);
    endloop(c);
}

static void whilestmt(Compiler *c, Lx *label) {
    ignoreline(PARSER);

    Loop loop;
    beginloop(c, &loop, true, true, label);

    bool rev = stepif(PARSER, LX_EXCL) || stepif(PARSER, LX_NOT);

    expect(PARSER, LX_LPAREN, "'(' expected");
    expression(c, PREC_COMMA);
    if (rev) writeabc(c, BC_NOT, LASTSLOT, LASTSLOT, 0, 0);
    ignoreline(PARSER);
    expect(PARSER, LX_RPAREN, "')' expected");
    ignoreline(PARSER);

    int loopjump = writejump(c, BC_JMPF, LASTSLOT, -1);

    statement(c);

    writebackflip(c, DEFINER->loops->start);

    patchjump(c, loopjump);

#ifdef LOOPELSE
    if (stepif(PARSER, LX_ELSE)) statement(c);
#endif

    endloop(c);
}

static void dostmt(Compiler *c, Lx *label) {
    ignoreline(PARSER);

    Loop loop;
    beginloop(c, &loop, true, true, label);

    statement(c);

    expect(PARSER, LX_WHILE, "'while' expected");
    ignoreline(PARSER);

    bool rev = stepif(PARSER, LX_EXCL) || stepif(PARSER, LX_NOT);

    expect(PARSER, LX_LPAREN, "'(' expected");
    expression(c, PREC_COMMA);
    if (rev) writeabc(c, BC_NOT, LASTSLOT, LASTSLOT, 0, 0);
    ignoreline(PARSER);
    expect(PARSER, LX_RPAREN, "')' expected");

    int exitjump = writejump(c, BC_JMPF, LASTSLOT, -1);

    writebackflip(c, DEFINER->loops->start);

    patchjump(c, exitjump);

#ifdef LOOPELSE
    if (stepif(PARSER, LX_ELSE)) statement(c);
    else expectsemi(PARSER);
#else
    expectsemi(PARSER);
#endif

    endloop(c);
}

static void forstmt(Compiler *c, Lx *label) {
    ignoreline(PARSER);

    beginscope(c);

    bool rev = stepif(PARSER, LX_EXCL) || stepif(PARSER, LX_NOT);

    expect(PARSER, LX_LPAREN, "'(' expected");

    if (!stepifsemi(PARSER)) {
        if (stepif(PARSER, LX_VAR)) {
            vardecl(c);
        } else {
            Lx name = step(PARSER);
            if (name.type == LX_WORD && stepif(PARSER, LX_COLON)) {
                erratprev(PARSER, "foreach loop is not supported");
                return;
            }

            denotation(c, PREC_COMMA, NULL);
            expectsemi(PARSER);
            SLOTS--;
        }
    }

    Loop loop;
    beginloop(c, &loop, true, true, label);

    int loopjump = -1;
    if (!stepifsemi(PARSER)) {
        expression(c, PREC_COMMA);
        if (rev) writeabc(c, BC_NOT, LASTSLOT, LASTSLOT, 0, 0);
        expectsemi(PARSER);
        loopjump = writejump(c, BC_JMPF, LASTSLOT, -1);
    } else if (rev) {
        loopjump = writejump(c, BC_JMP, 0, 0);
    }

    if (!stepif(PARSER, LX_RPAREN)) {
        int postjump = writejump(c, BC_JMP, 0, 0);
        int post = DEF->codelen;
        expression(c, PREC_COMMA);
        SLOTS--;
        ignoreline(PARSER);
        expect(PARSER, LX_RPAREN, "')' expected");

        writebackflip(c, DEFINER->loops->start);
        DEFINER->loops->start = post;
        patchjump(c, postjump);
    }
    ignoreline(PARSER);

    statement(c);

    writebackflip(c, DEFINER->loops->start);

    if (loopjump != -1) patchjump(c, loopjump);

#ifdef LOOPELSE
    if (stepif(PARSER, LX_ELSE)) statement(c);
#endif

    endloop(c);
    endscope(c);
}

static void returnstmt(Compiler *c) {
    if (DEFINER->type == DFNR_SCRIPT)
        erratprev(PARSER, "return from script is not allowed");

    if (stepifsemi(PARSER)) {
        writenilret(c);
    } else {
        expression(c, PREC_COMMA);
        writeret(c);
        expectsemi(PARSER);
    }
}

static void throwstmt(Compiler *c) {
    ignoreline(PARSER);

    expression(c, PREC_COMMA);
    writealb(c, BC_THRW, LASTSLOT, 0, -1);
    expectsemi(PARSER);
}

static void breakstmt(Compiler *c) {
    int level = 0;

    if (stepif(PARSER, LX_WORD)) {
        Lx label = PREV;
        for (Loop *loop = DEFINER->loops; loop != NULL; loop = loop->outer) {
            if (loop->name != NULL && lxeq(*loop->name, label)) {
                closenlstilscope(c, loop->scope);
                writealb(c, BREAKPATCH, 0, level, 0);
                expectsemi(PARSER);
                return;
            }
            level++;
        }
        erratprev(PARSER, "undefined label");
    }

    for (Loop *loop = DEFINER->loops; loop != NULL; loop = loop->outer) {
        if (loop->unlabelable) {
            closenlstilscope(c, loop->scope);
            writealb(c, BREAKPATCH, 0, level, 0);
            expectsemi(PARSER);
            return;
        }
        level++;
    }

    erratprev(PARSER, "'break' outside of loop");
}

static void continuestmt(Compiler *c) {
    if (stepif(PARSER, LX_WORD)) {
        Lx label = PREV;
        for (Loop *loop = DEFINER->loops; loop != NULL; loop = loop->outer) {
            if (loop->name != NULL && lxeq(*loop->name, label)) {
                if (!loop->continuable)
                    erratprev(PARSER, "uncontinuable label");
                closenlstilscope(c, loop->scope);
                writebackflip(c, loop->start);
                expectsemi(PARSER);
                return;
            }
        }
        erratprev(PARSER, "undefined label");
    }

    for (Loop *loop = DEFINER->loops; loop != NULL; loop = loop->outer) {
        if (loop->unlabelable && loop->continuable) {
            closenlstilscope(c, loop->scope);
            writebackflip(c, loop->start);
            expectsemi(PARSER);
            return;
        }
    }

    erratprev(PARSER, "'continue' outside of loop");
}

static void switchstmt(Compiler *c, Lx *label) {
    erratprev(PARSER, "switch is not supported");

    ignoreline(PARSER);

    expect(PARSER, LX_LPAREN, "'(' expected");
    expression(c, PREC_COMMA);
    SLOTS--;
    ignoreline(PARSER);
    expect(PARSER, LX_RPAREN, "')' expected");
    ignoreline(PARSER);

    Loop loop;
    beginloop(c, &loop, false, true, label);
    loop.casable = true;

    expect(PARSER, LX_LCUB, "'{' expected");
    block(c);
    ignoreline(PARSER);

    endloop(c);
}

static void trystmt(Compiler *c, Lx *label) {
    ignoreline(PARSER);

    Loop loop;
    beginloop(c, &loop, false, false, label);

    int start = DEF->codelen;

    expect(PARSER, LX_LCUB, "'{' expected");
    block(c);
    ignoreline(PARSER);

    int catchjump = writejump(c, BC_JMP, 0, 0);
    sil__defaddctch(SIL, DEF, start, catchjump, catchjump + 1, NEXTSLOT);

    if (stepif(PARSER, LX_FINALLY))
        erratprev(PARSER, "finally is not supported");

    expect(PARSER, LX_CATCH, "'catch' expected");
    ignoreline(PARSER);
    beginscope(c);
    if (stepif(PARSER, LX_LPAREN)) {
        addlocal(c, expectword(PARSER));
        SLOTS++;
        expect(PARSER, LX_RPAREN, "')' expected");
        ignoreline(PARSER);
    }
    expect(PARSER, LX_LCUB, "'{' expected");
    block(c);
    ignoreline(PARSER);
    endscope(c);

    patchjump(c, catchjump);

    if (stepif(PARSER, LX_FINALLY))
        erratprev(PARSER, "finally is not supported");

    endloop(c);
}

static void gotostmt(Compiler *c) {
    erratprev(PARSER, "goto is not supported");
}

static void casestmt(Compiler *c) {
    erratprev(PARSER, "'case' outside of switch");
}

static void defaultstmt(Compiler *c) {
    erratprev(PARSER, "'default' outside of switch");
}

static bool stmt(Compiler *c, Lx *label);

static bool labelstmt(Compiler *c, Lx name) {
    return stmt(c, &name);
}

static void vardecl(Compiler *c) {
#define MAXNAMES 0x10

    ignoreline(PARSER);

    Lx nms[MAXNAMES];
    int cur = 0;

    do {
        Lx name = expectword(PARSER);

        if (cur >= MAXNAMES) erratprev(PARSER, "too many new variables");

        nms[cur++] = name;

        if (stepif(PARSER, LX_EQUALS)) {
            expression(c, PREC_ASSIGN);
        } else {
            writealb(c, BC_NIL, NEXTSLOT, 0, +1);
        }
    } while (stepif(PARSER, LX_COMMA));

    if (DEFINER->scope == 0) {
        for (int i = 0; i < cur; i++)
            writealb(
                c, BC_DEFG, LASTSLOT - cur + 1 + i, addstrlx(c, nms[i]), 0
            );
        SLOTS -= cur;
    } else {
        for (int i = 0; i < cur; i++) addlocal(c, nms[i]);
    }

    expectsemi(PARSER);

#undef MAXNAMES
}

static void constdecl(Compiler *c) {
    erratprev(PARSER, "const is not supported");
}

static void initdefiner(
    Definer *d,
    Sil *sil,
    Definer *outer,
    Lx *name,
    enum definertype type
) {
    char *namechars = NULL;
    if (name != NULL) namechars = sil__strdupl(sil, name->start, name->len);

    d->type = type;
    d->localsc = 0;
    d->scope = outer == NULL ? 0 : 1;
    d->outer = outer;
    d->def = sil__newdef(sil, namechars);
    d->slots = 0;
    d->loops = NULL;
}

static void singleparam(Compiler *c, Lx *name) {
    addlocal(c, *name);
    DEF->paramc++;
    SLOTS++;

    if (DEF->paramc > MAXPARAMS) erratprev(PARSER, "too many parameters");
}

static void paramlist(Compiler *c) {
    struct {
        Lx name;
        Parser state;
    } defaults[MAXPARAMS];
    int defaultc = 0;

    while (!check(PARSER, LX_RPAREN)) {
        if (stepif(PARSER, LX_HELLIP)) {
            Lx name = expectword(PARSER);
            addlocal(c, name);
            DEF->vararg = true;
            SLOTS++;
            stepif(PARSER, LX_COMMA);
            break;
        }

        Lx name = expectword(PARSER);
        singleparam(c, &name);

        if (
            stepif(PARSER, LX_COLON)
#ifdef ALTKEYWORDS
            || stepif(PARSER, LX_ELSE)
#endif
        ) {
            ignoreline(PARSER);
            defaults[defaultc].name = name;
            defaults[defaultc].state = *PARSER;
            defaultc++;
            expression(c, PREC_ASSIGN);
            SLOTS--;
        }

        if (!stepif(PARSER, LX_COMMA)) break;
    }

    ignoreline(PARSER);
    expect(PARSER, LX_RPAREN, "')' expected");

    DEF->codelen = 0;
    Parser rem = *PARSER;
    for (int i = 0; i < defaultc; i++) {
        *PARSER = defaults[i].state;
        Code namecode = resolvenamecode(c, defaults[i].name, NEXTSLOT);
        writecode(c, namecode, +1);
        writealb(c, BC_NIL, NEXTSLOT, 0, +1);
        writeabc(c, BC_EQ, LASTSLOT - 1, LASTSLOT - 1, LASTSLOT, -1);
        int jump = writejump(c, BC_JMPF, LASTSLOT, -1);
        expression(c, PREC_ASSIGN);
        namecode = namecodetolhs(namecode, LASTSLOT);
        writecode(c, namecode, -1);
        patchjump(c, jump);
    }
    *PARSER = rem;
}

static void namedfunc(Compiler *c, Lx *name, bool isarrow, Lx *param) {
    if (++c->limit.func > MAXENCLFUNCS)
        erratprev(PARSER, "too many enclosing functions");

    Definer funcdefiner;
    initdefiner(&funcdefiner, SIL, DEFINER, name, DFNR_FUNC);

    DEFINER = &funcdefiner;

    if (isarrow) {
        if (param != NULL) singleparam(c, param);
        else paramlist(c);

        ASSERT(CURR.type == LX_EQUALS_GT, "'=>' expected");
        step(PARSER);
        ignoreline(PARSER);
        if (stepif(PARSER, LX_LCUB)) {
            block(c);
            writenilret(c);
        } else {
            expression(c, PREC_ASSIGN);
            writeret(c);
        }
    } else {
        if (param != NULL) singleparam(c, param);
        expect(PARSER, LX_LPAREN, "'(' expected");
        paramlist(c);
        ignoreline(PARSER);

        expect(PARSER, LX_LCUB, "'{' expected");
        block(c);
        writenilret(c);
    }

    Def *def = DEF;
    DEFINER = DEFINER->outer;

    int defidx = addk(c, GCO(def));
    writealb(c, BC_FUNC, NEXTSLOT, defidx, +1);

    c->limit.func--;
}

static void funcdecl(Compiler *c) {
    ignoreline(PARSER);

    Lx name = expectword(PARSER);

    if (DEFINER->scope != 0) addlocal(c, name);

    namedfunc(c, &name, false, NULL);
    ignoreline(PARSER);

    if (DEFINER->scope == 0)
        writealb(c, BC_DEFG, LASTSLOT, addstrlx(c, name), -1);
}

static void klassdecl(Compiler *c) {
    erratprev(PARSER, "class is not supported");
}

static void enumdecl(Compiler *c) {
    ignoreline(PARSER);

    if (check(PARSER, LX_WORD)) {
        Lx name = CURR;
        Emitter fakeemitter;

        enum_(c, &fakeemitter);

        if (DEFINER->scope == 0) {
            writealb(c, BC_DEFG, LASTSLOT, addstrlx(c, name), -1);
        } else {
            addlocal(c, name);
        }

        return;
    }

    expect(PARSER, LX_LCUB, "'{' expected");

    bool zero = true;
    int globc = 0;
    while (!check(PARSER, LX_RCUB)) {
        Lx name = expectword(PARSER);
        if (stepif(PARSER, LX_EQUALS)) {
            expression(c, PREC_ASSIGN);
        } else {
            if (zero) {
                pushk(c, NUM(0));
            } else {
                pushk(c, NUM(1));
                writeabc(c, BC_ADD, LASTSLOT, LASTSLOT - 1, LASTSLOT, 0);
            }
        }
        zero = false;

        if (DEFINER->scope == 0) {
            writealb(c, BC_DEFG, LASTSLOT, addstrlx(c, name), 0);
            globc++;
        } else {
            addlocal(c, name);
        }

        if (!stepif(PARSER, LX_COMMA)) break;
    }

    ignoreline(PARSER);
    expect(PARSER, LX_RCUB, "'}' expected");

    if (DEFINER->scope == 0) SLOTS -= globc;

    expectsemi(PARSER);
}

static bool stmt(Compiler *c, Lx *label) {
    if (++c->limit.stmt > MAXENCLSTMTS)
        erratprev(PARSER, "too many enclosing statements");

    bool hasret = false;

    if (stepifsemi(PARSER)) {
        /* nothing */
    } else if (stepif(PARSER, LX_LCUB)) {
        blockstmt(c, label);
    } else if (stepif(PARSER, LX_IF)) {
        ifstmt(c, label);
    } else if (stepif(PARSER, LX_WHILE)) {
        whilestmt(c, label);
    } else if (stepif(PARSER, LX_DO)) {
        dostmt(c, label);
    } else if (stepif(PARSER, LX_FOR)) {
        forstmt(c, label);
    } else if (stepif(PARSER, LX_RETURN)) {
        returnstmt(c);
    } else if (stepif(PARSER, LX_THROW)) {
        throwstmt(c);
    } else if (stepif(PARSER, LX_BREAK)) {
        breakstmt(c);
    } else if (stepif(PARSER, LX_CONTINUE)) {
        continuestmt(c);
    } else if (stepif(PARSER, LX_SWITCH)) {
        switchstmt(c, label);
    } else if (stepif(PARSER, LX_TRY)) {
        trystmt(c, label);
    } else if (stepif(PARSER, LX_GOTO)) {
        gotostmt(c);
    } else if (stepif(PARSER, LX_CASE)) {
        casestmt(c);
    } else if (stepif(PARSER, LX_DEFAULT)) {
        defaultstmt(c);
    } else {
        Lx label = step(PARSER);
        if (label.type == LX_WORD && stepif(PARSER, LX_COLON)) {
            hasret = labelstmt(c, label);
        } else {
            denotation(c, PREC_COMMA, NULL);
            expectsemi(PARSER);
            hasret = true;
        }
    }

    c->limit.stmt--;

    return hasret;
}

static void statement(Compiler *c) {
    if (stmt(c, NULL)) SLOTS--;
}

static bool decl(Compiler *c) {
    if (stepif(PARSER, LX_VAR)) {
        vardecl(c);
    } else if (stepif(PARSER, LX_CONST)) {
        constdecl(c);
    } else if (stepif(PARSER, LX_FUNC)) {
        funcdecl(c);
    } else if (stepif(PARSER, LX_CLASS)) {
        klassdecl(c);
    } else if (stepif(PARSER, LX_ENUM)) {
        enumdecl(c);
    } else {
        return stmt(c, NULL);
    }

    return false;
}

static void declaration(Compiler *c) {
    if (decl(c)) SLOTS--;
}

static void initparser(Parser *p, const char *src) {
    sil__initlxr(&p->lxr, src);
    p->nextlx = sil__lex;
    p->curr = LITLX("");
    step(p);
}

void sil__initcompiler(Compiler *c) {
    c->definer = NULL;

    c->limit.denot = 0;
    c->limit.func = 0;
    c->limit.stmt = 0;
}

enum sil_result sil__compile(Compiler *c, const char *src) {
    Definer scriptdefiner;
    Def *scriptdef = NULL;

    sil__initcompiler(c);

    enum sil_result result = setjmp(SIL->errjmpbuf);

    if (result == 0) {
        initparser(PARSER, src);

        Lx scriptlx = LITLX("@script");
        initdefiner(&scriptdefiner, SIL, NULL, &scriptlx, DFNR_SCRIPT);

        DEFINER = &scriptdefiner;
        bool hasret = false;

        while (!check(PARSER, LX_EOF)) {
            hasret = decl(c);
            if (hasret) SLOTS--;
        }

        if (hasret) SLOTS++;
        else writealb(c, BC_NIL, NEXTSLOT, 0, +1);

        writeret(c);
        scriptdef = DEF;
        sil__pushtmp(SIL, GCO(scriptdef));
    }

    DEFINER = NULL;

#ifdef DEBUG_COMPILE
    printf("[[debug Compile]]\n");
    if (scriptdef != NULL) sil__disasmdef(SIL, scriptdef);
#endif

    return result;
}

void sil__markcompiler(Compiler *c) {
    for (Definer *d = DEFINER; d != NULL; d = d->outer)
        sil__markgco((Gco *)d->def);
}
