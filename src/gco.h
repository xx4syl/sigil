#ifndef sil_gco_h
#define sil_gco_h
#include "cmn.h"
//

#include "code.h"
#include "map.h"

#define GCOTYPE(o) ((o)->type)
#define ISGCOTYPE(v, type) (ISGCO(v) && GCOTYPE(ASGCO(v)) == (type))
#define ISSTR(v) (ISGCOTYPE(v, GCO_STR))
#define ISFUNC(v) (ISGCOTYPE(v, GCO_FUNC))
#define ISDOC(v) (ISGCOTYPE(v, GCO_DOC))

#define ASSTR(v) ((Str *)ASGCO(v))
#define ASCHARS(v) (ASSTR(v)->chars)
#define ASFUNC(v) ((Func *)ASGCO(v))
#define ASDOC(v) ((Doc *)ASGCO(v))

#define STRLIT(lit) sil__newstrl(sil, lit, LITLEN(lit))

enum gcotype {
    GCO_STR,
    GCO_DOC,
    GCO_FUNC,

    GCO_DEF,
    GCO_NONLOCAL,
};

enum functype {
    FUNC_C,
    FUNC_NATIVE,
};

typedef struct gco {
    enum gcotype type;
    bool marked;
    struct gco *next;
} Gco;

typedef struct nonlocal {
    Gco o;

    Value *value;
    Value closed;

    struct nonlocal *next;
} Nonlocal;

typedef struct str {
    Gco o;

    uint32_t hash;
    int len;
    char chars[];
} Str;

typedef struct {
    bool local;
    int index;
} NonlocalInfo;

typedef struct {
    int begin, end;
    int recover;
    int slot;
} CatchInfo;

typedef struct {
    Gco o;

    char *name;
    int paramc;
    int maxslots;
    bool vararg;

    Code *code;
    int *slots;
    int *lines;
    int codelen, codecap;

    Value *k;
    int klen, kcap;

    NonlocalInfo *nli;
    int nlicap, nlilen;

    CatchInfo *ctch;
    int ctchlen, ctchcap;
} Def;

typedef struct {
    Gco o;

    char *name;

    enum functype type;
    union {
        SilFunc c;
        Def *native;
    } proto;

    int nlc;
    Nonlocal *nls[];
} Func;

typedef struct doc {
    Gco o;

    struct doc *klass;

    Map map;

    Value *arr;
    int arrlen, arrcap;

    void (*destruct)(void *data);
    void *data;
} Doc;

void *sil__newgco(Sil *sil, size_t size, enum gcotype type);
void sil__markgco(Gco *o);
void sil__freegco(Sil *sil, Gco *o);

Str *sil__newstrl(Sil *sil, const char *chars, int len);
Str *sil__strconcat(Sil *sil, Str *a, Str *b);

Func *sil__newnativefunc(Sil *sil, Def *def);
Func *sil__newcfunc(Sil *sil, SilFunc cfn, const char *name, int nlc);

Doc *sil__newdoc(Sil *sil, Doc *klass);
void sil__docstore(Sil *sil, Doc *doc, Value k, Value v);
Value sil__docload(Doc *doc, Value k);
bool sil__docdelete(Doc *doc, Value k);

Def *sil__newdef(Sil *sil, char *name);
int sil__defaddcode(Sil *sil, Def *def, Code code, int line, int slots);
int sil__defaddk(Sil *sil, Def *def, Value k);
int sil__defaddnli(Sil *sil, Def *def, bool local, int index);
int sil__defaddctch(Sil *sil, Def *def, int bgn, int end, int rcvr, int slot);

Nonlocal *sil__newnonlocal(Sil *sil, Value *v);
void sil__closenonlocal(Nonlocal* nl);

void sil__disasmdef(Sil *sil, Def *def);
void sil__logcode(Sil *sil, Def *def, int i);

#endif  // sil_gco_h
