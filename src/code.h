#ifndef sil_code_h
#define sil_code_h
#include "cmn.h"
//

#define OPS_X                                                                  \
    X(NIL) /* nil               */ /**/ /**/ /**/                              \
    X(TRUE) /* true             */ /**/ /**/ /**/                              \
    X(FLS) /* false             */ /**/ /**/ /**/                              \
    X(LDK) /* load constant     */ /**/ /**/ /**/                              \
    X(FUNC) /* function         */ /**/ /**/ /**/                              \
    X(DOC) /* document          */ /**/ /**/ /**/                              \
    X(DEFG) /* define global    */ /**/ /**/ /**/                              \
    X(STG) /* store global      */ /**/ /**/ /**/                              \
    X(LDG) /* load global       */ /**/ /**/ /**/                              \
    X(CLNL) /* close nonlocals  */ /**/ /**/ /**/                              \
    X(STNL) /* store nonlocal   */ /**/ /**/ /**/                              \
    X(LDNL) /* load nonlocal    */ /**/ /**/ /**/                              \
    X(STI) /* store index       */ /**/ /**/ /**/                              \
    X(LDI) /* load index        */ /**/ /**/ /**/                              \
    X(DEL) /* delete            */ /**/ /**/ /**/                              \
    X(COPY) /* copy             */ /**/ /**/ /**/                              \
    X(SWAP) /* swap             */ /**/ /**/ /**/                              \
    X(ADD) /* add               */ /**/ /**/ /**/                              \
    X(SUB) /* subtract          */ /**/ /**/ /**/                              \
    X(MUL) /* multiply          */ /**/ /**/ /**/                              \
    X(POW) /* power             */ /**/ /**/ /**/                              \
    X(FDIV) /* float division   */ /**/ /**/ /**/                              \
    X(IDIV) /* integer division */ /**/ /**/ /**/                              \
    X(MOD) /* modulo            */ /**/ /**/ /**/                              \
    X(AND) /* and               */ /**/ /**/ /**/                              \
    X(XOR) /* xor               */ /**/ /**/ /**/                              \
    X(OR) /* or                 */ /**/ /**/ /**/                              \
    X(LSH) /* left shift        */ /**/ /**/ /**/                              \
    X(RSH) /* right shift       */ /**/ /**/ /**/                              \
    X(EQ) /* equality           */ /**/ /**/ /**/                              \
    X(LT) /* less than          */ /**/ /**/ /**/                              \
    X(LE) /* less or equal      */ /**/ /**/ /**/                              \
    X(NOT) /* not               */ /**/ /**/ /**/                              \
    X(REV) /* reverse           */ /**/ /**/ /**/                              \
    X(NEG) /* negate            */ /**/ /**/ /**/                              \
    X(POS) /* posite            */ /**/ /**/ /**/                              \
    X(TPOF) /* type of          */ /**/ /**/ /**/                              \
    X(IS) /* is                 */ /**/ /**/ /**/                              \
    X(JMP) /* jump              */ /**/ /**/ /**/                              \
    X(JMPF) /* jump if false    */ /**/ /**/ /**/                              \
    X(BJMP) /* back jump        */ /**/ /**/ /**/                              \
    X(CALL) /* call             */ /**/ /**/ /**/                              \
    X(RET) /* return            */ /**/ /**/ /**/                              \
    X(THRW) /* throw            */ /**/ /**/ /**/

#define X(name) BC_##name,
enum op { OPS_X };
#undef X

typedef struct {
    byte op, a, b, c;
} Code;

#endif  // sil_code_h
