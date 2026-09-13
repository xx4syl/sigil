#include "lex.h"

#include <stdio.h>
#include <string.h>

#include "core.h"
#include "gco.h"

#define SIL ((Sil *)l)

void sil__initlxr(Lxr *l, const char *src) {
#ifdef DEBUG_LEX
    printf("[[debug Lex]]\n");
#endif

    l->cur = src;
    l->start = NULL;
    l->insertln = false;
    l->currln = 1;
}

bool sil__iskwtype(enum lxtype type) {
    return type <= LX_OR;
}

bool sil__isassigntype(enum lxtype type) {
    return LX_EQUALS <= type && type <= LX_MINUS_MINUS;
}

static int litlen(const Lxr *l) {
    return l->cur - l->start;
}

static Lx make(Lxr *l, enum lxtype type) {
#ifdef AUTOSEMI
    switch (type) {
        case LX_PLUS_PLUS:
        case LX_MINUS_MINUS:

        case LX_RPAREN:
        case LX_RSQB:
        case LX_RCUB:
        case LX_HELLIP:

        case LX_WORD:
        case LX_NUM:
        case LX_STR:
            l->insertln = true;
            break;
        default:
            l->insertln = sil__iskwtype(type);
    }
#endif

    Lx lx = {
        .type = type,
        .start = l->start,
        .len = litlen(l),
        .ln = l->currln,
    };

#ifdef DEBUG_LEX
    #define X(name) [LX_##name] = #name,
    static const char *names[] = {LXS_X};
    #undef X

    bool isfull = lx.len <= 0x10;
    int size = isfull ? lx.len : 0x10;
    printf("{ ");
    printf("ln: %d, type: %s", lx.ln, names[type]);
    if (size != 0)
        printf(", lit: '%.*s%s", size, lx.start, isfull ? "'" : "...");
    printf(" }\n");
#endif

    return lx;
}

static Lx error(Lxr *l, const char *msg) {
    Lx lx = make(l, LX_ERROR);
    lx.start = msg;
    lx.len = strlen(msg);
    return lx;
}

static enum lxtype wordtype(const char *word, int len) {
    struct kw {
        int len;
        const char *kw;
        enum lxtype type;
    };

    static struct kw kws[] = {
#ifdef ALTKEYWORDS
        {2, "or", LX_OR},
        {2, "is", LX_IS},
#endif
        {2, "if", LX_IF},
        {2, "do", LX_DO},
        {3, "var", LX_VAR},
        {3, "try", LX_TRY},
#ifdef ALTKEYWORDS
        {3, "not", LX_NOT},
#endif
        {3, "new", LX_NEW},
        {3, "for", LX_FOR},
#ifdef ALTKEYWORDS
        {3, "and", LX_AND},
#endif
        {4, "void", LX_VOID},
        {4, "true", LX_TRUE},
#ifdef ALTKEYWORDS
        {4, "then", LX_THEN},
#endif
        {4, "goto", LX_GOTO},
        {4, "func", LX_FUNC},
        {4, "enum", LX_ENUM},
        {4, "else", LX_ELSE},
        {4, "case", LX_CASE},
        {5, "while", LX_WHILE},
        {5, "throw", LX_THROW},
        {5, "false", LX_FALSE},
        {5, "const", LX_CONST},
        {5, "class", LX_CLASS},
        {5, "catch", LX_CATCH},
        {5, "break", LX_BREAK},
        {6, "typeof", LX_TYPEOF},
        {6, "switch", LX_SWITCH},
        {6, "return", LX_RETURN},
        {6, "delete", LX_DELETE},
        {7, "finally", LX_FINALLY},
        {7, "default", LX_DEFAULT},  //
        {8, "continue", LX_CONTINUE},
    };

    static const int kwslen = sizeof(kws) / sizeof(struct kw);

    if (2 <= len && len <= 8) {
        for (int i = kwslen - 1; i >= 0; i--) {
            if (kws[i].len == len) {
                int cmpr = memcmp(kws[i].kw, word, len);
                if (cmpr == 0) return kws[i].type;
                else if (cmpr > 0) break;
            } else if (kws[i].len < len) {
                break;
            }
        }
    }

    return LX_WORD;
}

static Lx word(Lxr *l) {
    while (sil__isalpha(*l->cur) || sil__isdigit(*l->cur)) l->cur++;
    return make(l, wordtype(l->start, litlen(l)));
}

static Lx num(Lxr *l) {
    l->cur--;
    l->cur += sil__readnum(l->cur, NULL);

    if (sil__isalpha(*l->cur) || *l->cur == NUMSEP)
        return error(l, "malformed number");

    return make(l, LX_NUM);
}

static Lx str(Lxr *l) {
    for (;;) {
        if (*l->cur == '"' && *(l->cur - 1) != '\\') break;

        if (*l->cur == '\0' || *l->cur == '\n')
            return error(l, "unfinished string");

        if (*l->cur == '%') {
            if (*(l->cur + 1) != '%') {
                break;
            } else {
                l->cur++;
            }
        }

        l->cur++;
    }

    l->cur++;

    return make(l, LX_STR);
}

Lx sil__lexstr(Lxr *l) {
    l->start = l->cur - 1;
    return str(l);
}

Lx sil__lex(Lxr *l) {
    int prevln = l->currln;

whitespace:
    do {
        if (l->cur[0] == '#') {
            while (l->cur[0] != '\n' && l->cur[0] != '\0') l->cur++;
        } else if (l->cur[0] == '/' && l->cur[1] == '*') {
            l->cur += 2;

            while (!(l->cur[0] == '*' && l->cur[1] == '/')) {
                if (l->cur[0] == '\n') l->currln++;
                else if (l->cur[0] == '\0')
                    return error(l, "unfinished block comment");
                l->cur++;
            }

            l->cur += 2;
        }

        switch (l->cur[0]) {
            case '\n':
                l->currln++;
            case '\r':
            case '\t':
            case '\v':
            case '\f':
            case ' ':
                l->cur++;
                goto whitespace;
        }
    } while (false);

    l->start = l->cur;

#ifdef AUTOSEMI
    if (l->insertln && (l->currln > prevln || l->cur[0] == '\0')) {
        int remline = l->currln;
        l->currln = prevln;
        Lx lx = make(l, LX_LINE);
        l->currln = remline;
        return lx;
    }
#endif

    if (l->cur[0] == '\0') return make(l, LX_EOF);

#define CASE1(lx0, type0)                                                      \
    case lx0:                                                                  \
        return make(l, type0);

#define CASE12(lx0, type0, lx1, type1)                                         \
    case lx0: {                                                                \
        if (l->cur[0] == lx1) {                                                \
            l->cur += 1;                                                       \
            return make(l, type1);                                             \
        }                                                                      \
        return make(l, type0);                                                 \
    }

#define CASE13(lx0, type0, lx10, lx11, type1)                                  \
    case lx0: {                                                                \
        if (l->cur[0] == lx10 && l->cur[1] == lx11) {                          \
            l->cur += 2;                                                       \
            return make(l, type1);                                             \
        }                                                                      \
        return make(l, type0);                                                 \
    }

#define CASE122(lx0, type0, lx1, type1, lx2, type2)                            \
    case lx0: {                                                                \
        if (l->cur[0] == lx2) {                                                \
            l->cur += 1;                                                       \
            return make(l, type2);                                             \
        } else if (l->cur[0] == lx1) {                                         \
            l->cur += 1;                                                       \
            return make(l, type1);                                             \
        }                                                                      \
        return make(l, type0);                                                 \
    }

#define CASE1222(lx0, type0, lx1, type1, lx2, type2, lx3, type3)               \
    case lx0: {                                                                \
        if (l->cur[0] == lx3) {                                                \
            l->cur += 1;                                                       \
            return make(l, type3);                                             \
        } else if (l->cur[0] == lx2) {                                         \
            l->cur += 1;                                                       \
            return make(l, type2);                                             \
        } else if (l->cur[0] == lx1) {                                         \
            l->cur += 1;                                                       \
            return make(l, type1);                                             \
        }                                                                      \
        return make(l, type0);                                                 \
    }

#define CASE1223(lx0, type0, lx1, type1, lx2, type2, lx30, lx31, type3)        \
    case lx0: {                                                                \
        if (l->cur[0] == lx30 && l->cur[1] == lx31) {                          \
            l->cur += 2;                                                       \
            return make(l, type3);                                             \
        } else if (l->cur[0] == lx2) {                                         \
            l->cur += 1;                                                       \
            return make(l, type2);                                             \
        } else if (l->cur[0] == lx1) {                                         \
            l->cur += 1;                                                       \
            return make(l, type1);                                             \
        }                                                                      \
        return make(l, type0);                                                 \
    }

    char sym = *l->cur++;
    switch (sym) {
        CASE1(',', LX_COMMA)
        CASE122('=', LX_EQUALS, '=', LX_EQUALS_EQUALS, '>', LX_EQUALS_GT)
        CASE122('+', LX_PLUS, '=', LX_PLUS_EQUALS, '+', LX_PLUS_PLUS)
        CASE1222(
            '-', LX_MINUS, '=', LX_MINUS_EQUALS, '-', LX_MINUS_MINUS, '>',
            LX_MINUS_GT
        )
        CASE1223(
            '*', LX_AST, '=', LX_AST_EQUALS, '*', LX_AST_AST, '*', '=',
            LX_AST_AST_EQUALS
        )
        CASE1223(
            '/', LX_SOL, '=', LX_SOL_EQUALS, '/', LX_SOL_SOL, '/', '=',
            LX_SOL_SOL_EQUALS
        )
        CASE12('%', LX_PERCNT, '=', LX_PERCNT_EQUALS)
        CASE122('|', LX_VERBAR, '=', LX_VERBAR_EQUALS, '|', LX_VERBAR_VERBAR)
        CASE12('^', LX_HAT, '=', LX_HAT_EQUALS)
        CASE122('&', LX_AMP, '=', LX_AMP_EQUALS, '&', LX_AMP_AMP)
        CASE1223(
            '<', LX_LT, '=', LX_LT_EQUALS, '<', LX_LT_LT, '<', '=',
            LX_LT_LT_EQUALS
        )
        CASE1223(
            '>', LX_GT, '=', LX_GT_EQUALS, '>', LX_GT_GT, '>', '=',
            LX_GT_GT_EQUALS
        )
        CASE1('?', LX_QUEST)
        CASE1(':', LX_COLON)
        CASE12('!', LX_EXCL, '=', LX_EXCL_EQUALS)
        CASE1('~', LX_TILDE)
        CASE13('.', LX_DOT, '.', '.', LX_HELLIP)
        CASE1('(', LX_LPAREN)
        CASE1(')', LX_RPAREN)
        CASE1('{', LX_LCUB)
        CASE1('}', LX_RCUB)
        CASE1('[', LX_LSQB)
        CASE1(']', LX_RSQB)
        CASE1(';', LX_SEMI)
    }

    if (sil__isalpha(sym)) return word(l);

    if (sil__isdigit(sym)) return num(l);

    if (sym == '"') return str(l);

    char buf[0x40];
    int len = (byte)sym < 0x80
                  ? sprintf(buf, "unexpected symbol '%c'", sym)
                  : sprintf(buf, "unexpected symbol '\\x%02x'", (byte)sym);
    Str *str = sil__newstrl(SIL, buf, len);
    sil__pushtmp(SIL, GCO(str));
    return error(l, str->chars);

#undef CASE1223
#undef CASE1222
#undef CASE122
#undef CASE13
#undef CASE12
#undef CASE1
}
