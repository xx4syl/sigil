#ifndef sil_map_h
#define sil_map_h
#include "cmn.h"
//

#include "val.h"

typedef struct {
    struct pair *pairs;
    int len, cap, tombs;
} Map;

void sil__initmap(Map *m);
void sil__markmap(Map *m);
void sil__freemap(Sil *sil, Map *m);

bool sil__mapstore(Sil *sil, Map *m, Value k, Value v);
bool sil__mapload(Map *m, Value k, Value *v);
bool sil__mapdelete(Map *m, Value k);
struct str *sil__maploadstr(Map *m, const char *chars, int len, uint32_t hash);
bool sil__mapnext(Map *m, int *iter, Value *k, Value *v);

#endif  // sil_map_h
