#ifndef sigil_h
#define sigil_h

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>

#define SIL_IS_ERROR(code) ((code) != SIL_OK)
#define SIL_IS_COMPILE_ERROR(code) ((code) < 0)
#define SIL_IS_RUNTIME_ERROR(code) ((code) > 0)

enum sil_result {
    SIL_OK,
    SIL_UNCAUGHT,

    SIL_UNCOMPILED = INT_MIN,
    SIL_UNEXPECTED_EOF,
};

enum sil_type {
    SIL_VOID,
    SIL_BOOL,
    SIL_NUM,
    SIL_STR,
    SIL_DOC,
    SIL_FUNC,
};

typedef struct sil_core Sil;

typedef void *(*SilRealloc)(void *mem, size_t newsize);

typedef enum sil_result (*SilFunc)(Sil *sil);

typedef struct sil_handle SilHandle;

Sil *sil_new(SilRealloc realloc);
void sil_free(Sil *sil);

void sil_gc(Sil *sil);

enum sil_result sil_interpret(Sil *sil, const char *script, int slot);
enum sil_result sil_call(Sil *sil, int argc, int slot);

int sil_slots(Sil *sil);
void sil_use_slots(Sil *sil, int need);

enum sil_type sil_type(Sil *sil, int slot);

bool sil_as_bool(Sil *sil, int slot);
int sil_as_int(Sil *sil, int slot);
double sil_as_double(Sil *sil, int slot);
const char *sil_as_chars(Sil *sil, int *len, int slot);

void sil_to_bool(Sil *sil, int dst, int slot);
void sil_to_num(Sil *sil, int dst, int slot);
void sil_to_str(Sil *sil, int dst, int slot);

bool sil_equal(Sil *sil, int slota, int slotb);
void sil_concat(Sil *sil, int dst, int slota, int slotb);

enum sil_result sil_return(Sil *sil, int slot);
enum sil_result sil_throw(Sil *sil, int slot);

void sil_void(Sil *sil, int slot);
void sil_bool(Sil *sil, bool v, int slot);
void sil_num(Sil *sil, double v, int slot);
void sil_str(Sil *sil, const char *v, int slot);
void sil_strl(Sil *sil, const char *v, int len, int slot);
void sil_func(Sil *sil, SilFunc v, const char *name, int slot);

void sil_closure(Sil *sil, SilFunc v, const char *name, int nlc, int slot);
void sil_get_nonlocal(Sil *sil, int index, int slot);
void sil_set_nonlocal(Sil *sil, int index, int slot);

void sil_define(Sil *sil, const char *name, int slot);
bool sil_variable(Sil *sil, const char *name, int dst);
void sil_copy(Sil *sil, int dst, int slot);

void sil_doc(Sil *sil, int slot);
int sil_doc_size(Sil *sil, int d);
void sil_doc_store(Sil *sil, int d, int k, int v);
void sil_doc_load(Sil *sil, int dst, int d, int k);
bool sil_doc_delete(Sil *sil, int d, int k);

int sil_line(Sil *sil);
const char *sil_trace(Sil *sil, int *line, int lv);

SilHandle *sil_new_handle(Sil *sil, int slot);
void sil_free_handle(Sil *sil, SilHandle *hndl);
void sil_handle(Sil *sil, SilHandle *hndl, int slot);

bool sil_next_pair(Sil *sil, int *iter, int kslot, int vslot, int slot);

#endif  // sigil_h
