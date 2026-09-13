#ifndef sil_cpl_h
#define sil_cpl_h
#include "cmn.h"
//

#include "lex.h"

typedef struct {
    Lxr lxr;
    Lx prev, curr;
    Lx (*nextlx)(Lxr *lxr);
} Parser;

typedef struct {
    Parser parser;
    struct definer *definer;

    struct {
        int denot;
        int func;
        int stmt;
    } limit;
} Compiler;

void sil__initcompiler(Compiler *c);
enum sil_result sil__compile(Compiler *c, const char *src);
void sil__markcompiler(Compiler *c);

#endif  // sil_cpl_h
