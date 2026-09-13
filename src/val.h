#ifndef sil_val_h
#define sil_val_h
#include "cmn.h"
//

enum valuetype {
    VAL_VOID,
    VAL_BOOL,
    VAL_NUM,
    VAL_GCO,

    VAL_UNDEFINED,
};

#ifdef NANBOXING

    #define SIGN ((uint64_t)0x8000000000000000)
    #define QNAN ((uint64_t)0x7ffc000000000000)

    #define TAGVOID 1       // 001
    #define TAGFALSE 2      // 010
    #define TAGTRUE 3       // 011
    #define TAGUNDEFINED 4  // 100
    #define TAGUNUSED1 5    // 101
    #define TAGUNUSED2 6    // 110
    #define TAGUNUSED3 7    // 111

    #define TAG(v) ((v) & 7)

    #define FALSE ((Value)(uint64_t)(QNAN | TAGFALSE))
    #define TRUE ((Value)(uint64_t)(QNAN | TAGTRUE))

    #define UNDEFINED ((Value)(uint64_t)(QNAN | TAGUNDEFINED))
    #define VOID ((Value)(uint64_t)(QNAN | TAGVOID))
    #define BOOL(v) ((v) ? TRUE : FALSE)
    #define NUM(v) sil___dtov(v)
    #define GCO(v) (Value)(SIGN | QNAN | (uint64_t)(uintptr_t)(v))

    #define ASBOOL(v) ((v) == TRUE)
    #define ASNUM(v) sil___vtod(v)
    #define ASGCO(v) ((struct gco *)(uintptr_t)((v) & ~(SIGN | QNAN)))

    #define VALTYPE(v) sil___valtype(v)
    #define ISUNDEFINED(v) ((v) == UNDEFINED)
    #define ISVOID(v) ((v) == VOID)
    #define ISBOOL(v) (((v) | 1) == TRUE)
    #define ISNUM(v) (((v) & QNAN) != QNAN)
    #define ISGCO(v) (((v) & (QNAN | SIGN)) == (QNAN | SIGN))

    #define VALEQ(a, b) sil___valeq(a, b)
    #define VALHASH(v) sil___valhash(v)

typedef uint64_t Value;

static inline double sil___vtod(Value v) {
    union {
        double d;
        Value v;
    } u;
    u.v = v;
    return u.d;
}

static inline Value sil___dtov(double d) {
    union {
        double d;
        Value v;
    } u;
    u.d = d;
    return u.v;
}

static inline enum valuetype sil___valtype(Value v) {
    if ((v & QNAN) != QNAN) return VAL_NUM;
    if ((v & SIGN) == SIGN) return VAL_GCO;
    switch (TAG(v)) {
        case TAGVOID:
            return VAL_VOID;
        case TAGFALSE:
        case TAGTRUE:
            return VAL_BOOL;
        case TAGUNDEFINED:
            return VAL_UNDEFINED;
        default:
            UNREACHABLE;
    }
}

static inline bool sil___valeq(Value a, Value b) {
    if (ISNUM(a) && ISNUM(b)) return ASNUM(a) == ASNUM(b);
    return a == b;
}

static inline uint32_t sil___valhash(Value v) {
    return sil__fnv1a(&v, sizeof(Value));
}

#else

    #define VALUE(type, as_, v) ((Value){type, .as.as_ = v})

    #define UNDEFINED VALUE(VAL_UNDEFINED, o, NULL)
    #define VOID VALUE(VAL_VOID, o, NULL)
    #define BOOL(v) VALUE(VAL_BOOL, b, v)
    #define NUM(v) VALUE(VAL_NUM, n, v)
    #define GCO(v) VALUE(VAL_GCO, o, (Gco *)v)

    #define ASBOOL(v) ((v).as.b)
    #define ASNUM(v) ((v).as.n)
    #define ASGCO(v) ((v).as.o)

    #define VALTYPE(v) ((v).type)
    #define ISUNDEFINED(v) (VALTYPE(v) == VAL_UNDEFINED)
    #define ISVOID(v) (VALTYPE(v) == VAL_VOID)
    #define ISBOOL(v) (VALTYPE(v) == VAL_BOOL)
    #define ISNUM(v) (VALTYPE(v) == VAL_NUM)
    #define ISGCO(v) (VALTYPE(v) == VAL_GCO)

    #define VALEQ(a, b) sil___valeq(a, b)
    #define VALHASH(v) sil___valhash(v)

typedef struct {
    enum valuetype type;
    union {
        bool b;
        double n;
        struct gco *o;
    } as;
} Value;

static inline bool sil___valeq(Value a, Value b) {
    if (a.type != b.type) return false;
    switch (a.type) {
        case VAL_VOID:
            return true;
        case VAL_BOOL:
            return ASBOOL(a) == ASBOOL(b);
        case VAL_NUM:
            return ASNUM(a) == ASNUM(b);
        case VAL_GCO:
            return ASGCO(a) == ASGCO(b);
        default:
            UNREACHABLE;
    }
}

static inline uint32_t sil___valhash(Value v) {
    switch (v.type) {
        case VAL_VOID:
            return sil__fnv1a(NULL, 0);
        case VAL_BOOL:
            return sil__fnv1a(&ASBOOL(v), sizeof(bool));
        case VAL_NUM:
            return sil__fnv1a(&ASNUM(v), sizeof(double));
        case VAL_GCO:
            return sil__fnv1a(&ASGCO(v), sizeof(struct gco *));
        default:
            UNREACHABLE;
    }
}

#endif

void sil__markvalue(Value v);
void sil__freevalue(Sil *sil, Value v);

bool sil__tobool(Value v);
double sil__tonum(Sil *sil, Value v);
struct str *sil__tostr(Sil *sil, Value v);
Value sil__typeof(Sil *sil, Value v);

void sil__logvalue(Sil *sil, Value v);

#endif  // sil_val_h
