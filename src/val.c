#include "val.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "gco.h"

void sil__markvalue(Value v) {
    if (ISGCO(v)) sil__markgco(ASGCO(v));
}

void sil__freevalue(Sil *sil, Value v) {
    if (ISGCO(v)) sil__freegco(sil, ASGCO(v));
}

Str *sil__tostr(Sil *sil, Value v) {
    char buf[0x200];

    switch (VALTYPE(v)) {
        case VAL_VOID:
            return STRLIT("void");
        case VAL_BOOL:
            if (ASBOOL(v)) return STRLIT("true");
            else return STRLIT("false");
        case VAL_NUM: {
            double num = ASNUM(v);
            if (!isfinite(num))
                return isnan(num) ? STRLIT("nan")
                       : num < 0  ? STRLIT("-inf")
                                  : STRLIT("inf");
            if (num == floor(num)) sprintf(buf, "%.0f", num);
            else sprintf(buf, "%g", num);
            return sil__newstrl(sil, buf, strlen(buf));
        }
        case VAL_GCO: {
            Gco *o = ASGCO(v);
            switch (GCOTYPE(o)) {
                case GCO_STR:
                    return ASSTR(v);
                case GCO_DOC:
                    sprintf(buf, "<doc %p>", (void *)o);
                    return sil__newstrl(sil, buf, strlen(buf));
                case GCO_FUNC: {
                    Func *c = ASFUNC(v);

                    if (c->name != NULL) sprintf(buf, "<func %.16s>", c->name);
                    else sprintf(buf, "<func %p>", (void *)o);

                    return sil__newstrl(sil, buf, strlen(buf));
                }
                case GCO_DEF: {
                    Def *def = (Def *)ASGCO(v);

                    if (def->name != NULL)
                        sprintf(buf, "<def %.16s>", def->name);
                    else sprintf(buf, "<def %p>", (void *)o);

                    return sil__newstrl(sil, buf, strlen(buf));
                }
                default:
                    UNREACHABLE;
            }
        }
        default:
            UNREACHABLE;
    }
}

double sil__tonum(Sil *sil, Value v) {
    switch (VALTYPE(v)) {
        case VAL_VOID:
            return NAN;
        case VAL_BOOL:
            if (ASBOOL(v)) return 1;
            else return 0;
        case VAL_NUM:
            return ASNUM(v);
        case VAL_GCO: {
            Gco *o = ASGCO(v);
            switch (GCOTYPE(o)) {
                case GCO_STR: {
                    int skip;
                    int readed = sil__readnum(ASCHARS(v), &skip);
                    if (skip == readed) return NAN;
                    return sil__atofl(sil, ASCHARS(v) + skip, readed - skip);
                }
                case GCO_DOC:
                case GCO_FUNC:
                    return NAN;
                default:
                    UNREACHABLE;
            }
        }
        default:
            UNREACHABLE;
    }
}

void sil__logvalue(Sil *sil, Value v) {
    printf("%s", sil__tostr(sil, v)->chars);
}

bool sil__tobool(Value v) {
    switch (VALTYPE(v)) {
        case VAL_VOID:
            return false;
        case VAL_BOOL:
            return ASBOOL(v);
        case VAL_NUM:
            return ASNUM(v) != 0;
        case VAL_GCO:
            if (ISGCOTYPE(v, GCO_STR) && ASSTR(v)->len == 0) return false;
            return true;
        default:
            UNREACHABLE;
    }
}

Value sil__typeof(Sil *sil, Value v) {
    switch (VALTYPE(v)) {
        case VAL_VOID:
            return GCO(STRLIT("void"));
        case VAL_BOOL:
            return GCO(STRLIT("bool"));
        case VAL_NUM:
            return GCO(STRLIT("num"));
        case VAL_GCO:
            switch (GCOTYPE(ASGCO(v))) {
                case GCO_STR:
                    return GCO(STRLIT("str"));
                case GCO_FUNC:
                    return GCO(STRLIT("func"));
                case GCO_DOC:
                    return GCO(STRLIT("doc"));
                default:
                    UNREACHABLE;
            }
        default:
            UNREACHABLE;
    }
}
