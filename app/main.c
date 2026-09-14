#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../incl/sigil.h"

// #define APP_SAFEREALLOC

#define FATALF(code, ...)                                                      \
    do {                                                                       \
        fprintf(stderr, __VA_ARGS__);                                          \
        exit(code);                                                            \
    } while (0)

static char *readFile(const char *filePath) {
    FILE *file = fopen(filePath, "rb");
    if (file == NULL) FATALF(EIO, "can't open '%s'", filePath);

    fseek(file, 0l, SEEK_END);
    size_t fileSize = ftell(file);
    rewind(file);

    char *buf = malloc(sizeof(char) * fileSize + 1);
    if (buf == NULL) FATALF(EIO, "no memory for '%s'", filePath);

    size_t readed = fread(buf, sizeof(char), fileSize, file);
    if (readed < fileSize) FATALF(EIO, "can't read '%s'", filePath);

    fclose(file);

    buf[fileSize] = '\0';

    return buf;
}

static void *reallocate(void *memory, size_t size) {
    if (size == 0) free(memory);
    else memory = realloc(memory, size);
    return memory;
}

static void *safeReallocate(void *memory, size_t size) {
    memory = reallocate(memory, size);
    if (memory == NULL && size != 0) FATALF(ENOMEM, "no memory for realloc");
    return memory;
}

static Sil *sil = NULL;
static char *script = NULL;

static SilHandle *callback = NULL;

#define XX                                                                     \
    "there are no dogs around n'\n"                                            \
    "there are no gods around\n\n"                                             \
    "ain't no ~\n"                                                             \
    "ain't nobody 'round anymore\n\n"                                          \
    "nevermore\n"

static void freeAndExit(void) {
    if (script != NULL) free(script);
    if (sil != NULL) sil_free(sil);
    exit(0);
}

static void onSigint(int s) {
    freeAndExit();
}

static void repl(void) {
    printf("repl\nexit using ctrl+c or exit()\n");

    char line[0x1000];
    char *lineEnd = line + 0x1000;
    char *cursor = line;

    sil_use_slots(sil, 1);
    sil_void(sil, 0);
    sil_define(sil, "_", 0);

    for (;;) {
        if (cursor == line) printf("> ");
        else printf("| ");

        if (!fgets(cursor, lineEnd - cursor, stdin)) break;

        enum sil_result result = sil_interpret(sil, line, 0);
        if (SIL_IS_ERROR(result)) {
            if (result == SIL_UNEXPECTED_EOF) {
                int readed = strlen(line);

                if (readed == sizeof(line) - 1)
                    FATALF(EIO, "repl input overflow");

                cursor = line + readed;
                continue;
            }

            cursor = line;
            sil_to_str(sil, 0, 0);
            fprintf(
                stderr, "%s: %s\n",
                SIL_IS_COMPILE_ERROR(result) ? "uncompiled" : "uncaught",
                sil_as_chars(sil, NULL, 0)
            );
            continue;
        }

        cursor = line;
        sil_define(sil, "_", 0);
        sil_to_str(sil, 0, 0);
        printf("%s\n", sil_as_chars(sil, NULL, 0));
    }
}

static void file(const char *script) {
    sil_use_slots(sil, 1);
    enum sil_result result = sil_interpret(sil, script, 0);
    if (SIL_IS_ERROR(result)) {
        sil_to_str(sil, 0, 0);
        fprintf(
            stderr, "%s: %s\n",
            SIL_IS_COMPILE_ERROR(result) ? "uncompiled" : "uncaught",
            sil_as_chars(sil, NULL, 0)
        );
    };
}

static enum sil_result silfnBool(Sil *sil) {
    sil_use_slots(sil, 1);
    sil_to_bool(sil, 0, 0);
    return sil_return(sil, 0);
}

static enum sil_result silfnNum(Sil *sil) {
    sil_use_slots(sil, 1);
    sil_to_num(sil, 0, 0);
    return sil_return(sil, 0);
}

static enum sil_result silfnStr(Sil *sil) {
    sil_use_slots(sil, 1);
    sil_to_str(sil, 0, 0);
    return sil_return(sil, 0);
}

static enum sil_result silfnInput(Sil *sil) {
    if (sil_slots(sil) > 0) {
        sil_to_str(sil, 0, 0);
        printf("%s", sil_as_chars(sil, NULL, 0));
    } else {
        sil_use_slots(sil, 1);
    }

    char buf[0x500];
    if (fgets(buf, sizeof(buf), stdin) == NULL) {
        sil_str(sil, "can't read stdin", 0);
        return sil_throw(sil, 0);
    }

    size_t size = strlen(buf);
    if (size > 0 && buf[size - 1] == '\n') buf[size - 1] = '\0';

    sil_str(sil, buf, 0);
    return sil_return(sil, 0);
}

static enum sil_result silfnExit(Sil *sil) {
    freeAndExit();
    return SIL_OK;
}

static enum sil_result silfnPrint(Sil *sil) {
    int argc = sil_slots(sil);
    for (int i = 0; i < argc; i++) {
        sil_to_str(sil, i, i);
        printf("%s", sil_as_chars(sil, NULL, i));
        if (i != argc - 1) printf(" ");
    }
    printf("\n");
    return SIL_OK;
}

static enum sil_result silfnClock(Sil *sil) {
    sil_use_slots(sil, 1);
    sil_num(sil, (double)clock() / CLOCKS_PER_SEC, 0);
    return sil_return(sil, 0);
}

static enum sil_result silfnError(Sil *sil) {
    sil_use_slots(sil, 3);
    if (sil_slots(sil) < 1) sil_str(sil, "", 0);
    sil_to_str(sil, 0, 0);
    sil_str(sil, " at line ", 1);
    sil_num(sil, sil_line(sil), 2);
    sil_concat(sil, 1, 1, 2);
    sil_concat(sil, 0, 0, 1);
    return sil_throw(sil, 0);
}

static enum sil_result silfnSilent(Sil *sil) {
    int argc = sil_slots(sil) - 1;
    if (argc < 0) {
        sil_use_slots(sil, 1);
        sil_str(sil, "expected at least 1 argument", 0);
        return sil_throw(sil, 0);
    }
    sil_call(sil, argc, 0);
    return sil_return(sil, 0);
}

static enum sil_result silfnProtect(Sil *sil) {
    int argc = sil_slots(sil) - 1;
    if (argc < 0) {
        sil_use_slots(sil, 1);
        sil_str(sil, "expected at least 1 argument", 0);
        return sil_throw(sil, 0);
    }

    enum sil_result result = sil_call(sil, argc, 0);

    sil_use_slots(sil, 3);
    sil_doc(sil, 1);

    if (result == SIL_OK) {
        sil_str(sil, "result", 2);
        sil_doc_store(sil, 1, 2, 0);
    } else {
        sil_str(sil, "error", 2);
        sil_doc_store(sil, 1, 2, 0);
    }

    return sil_return(sil, 1);
}

static enum sil_result silfnSizeOf(Sil *sil) {
    if (sil_slots(sil) < 1) {
        sil_use_slots(sil, 1);
        sil_str(sil, "argument expected", 0);
        return sil_throw(sil, 0);
    }

    int size;
    switch (sil_type(sil, 0)) {
        case SIL_STR:
            sil_as_chars(sil, &size, 0);
            break;
        case SIL_DOC:
            size = sil_doc_size(sil, 0);
            break;
        default:
            size = -1;
    }

    sil_num(sil, size >= 0 ? size : NAN, 0);
    return sil_return(sil, 0);
}

static enum sil_result silfnGC(Sil *sil) {
    sil_gc(sil);
    return SIL_OK;
}

static enum sil_result silfnRand(Sil *sil) {
    int argc = sil_slots(sil);
    double min, max;
    if (argc == 1 && sil_type(sil, 0) == SIL_NUM) {
        min = 0;
        max = sil_as_double(sil, 0);
    } else if (
        argc > 1 && sil_type(sil, 0) == SIL_NUM && sil_type(sil, 1) == SIL_NUM
    ) {
        min = sil_as_double(sil, 0);
        max = sil_as_double(sil, 1);
    } else {
        sil_use_slots(sil, 1);
        min = 0;
        max = 1;
    }
    double num = min + (double)rand() / ((double)RAND_MAX + 1.0) * (max - min);
    sil_num(sil, num, 0);
    return sil_return(sil, 0);
}

static enum sil_result silfnEval(Sil *sil) {
    int argc = sil_slots(sil);
    if (argc < 1 || sil_type(sil, 0) != SIL_STR) {
        sil_use_slots(sil, 1);
        sil_str(sil, "'str' expected", 0);
        return sil_throw(sil, 0);
    }

    const char *script = sil_as_chars(sil, NULL, 0);
    if (SIL_IS_ERROR(sil_interpret(sil, script, 0))) {
        sil_use_slots(sil, 2);
        sil_str(sil, "eval: ", 1);
        sil_to_str(sil, 0, 0);
        sil_concat(sil, 0, 1, 0);
        return sil_throw(sil, 0);
    }

    return sil_return(sil, 0);
}

static enum sil_result silfnTrace(Sil *sil) {
    char buf[0x500];
    char *cursor = buf;

    for (int i = 0;; i++) {
        int line;
        const char *funcName = sil_trace(sil, &line, i);

        if (buf + sizeof(buf) - cursor <= 0x40) {
            cursor[0] = '.';
            cursor[1] = '.';
            cursor[2] = '.';
            cursor[3] = '\n';
            cursor[4] = '\0';
            break;
        }

        if (funcName == NULL) break;

        if (line > 0)
            cursor +=
                sprintf(cursor, "func %.24s at line %d\n", funcName, line);
        else cursor += sprintf(cursor, "func %.24s\n", funcName);
    }

    sil_use_slots(sil, 1);
    sil_str(sil, buf, 0);
    return sil_return(sil, 0);
}

static enum sil_result silfnUse(Sil *sil) {
    if (sil_slots(sil) < 1) {
        sil_use_slots(sil, 1);
        sil_str(sil, "patch expected", 0);
        return sil_throw(sil, 0);
    }

    sil_to_str(sil, 0, 0);

    const char *use = readFile(sil_as_chars(sil, NULL, 0));
    if (SIL_IS_ERROR(sil_interpret(sil, use, 0))) return sil_throw(sil, 0);
    return sil_return(sil, 0);
}

static enum sil_result silfnRegistrate(Sil *sil) {
    if (sil_slots(sil) < 1) {
        sil_use_slots(sil, 1);
        sil_str(sil, "callback expected", 0);
        return sil_throw(sil, 0);
    } else if (sil_type(sil, 0) != SIL_FUNC) {
        sil_str(sil, "callback expected", 0);
        return sil_throw(sil, 0);
    }

    if (callback != NULL) sil_free_handle(sil, callback);
    callback = sil_new_handle(sil, 0);
    return SIL_OK;
}

static enum sil_result silfnExecute(Sil *sil) {
    if (callback == NULL) return SIL_OK;

    int argc = sil_slots(sil);
    sil_use_slots(sil, argc + 1);

    for (int i = argc - 1; i >= 0; i--) sil_copy(sil, i + 1, i);

    sil_handle(sil, callback, 0);
    if (SIL_IS_ERROR(sil_call(sil, argc, 0))) {
        sil_to_str(sil, 0, 0);
        printf("execute error: %s\n", sil_as_chars(sil, NULL, 0));
    }

    return SIL_OK;
}

static enum sil_result silfn_Counter(Sil *sil) {
    sil_use_slots(sil, 2);
    sil_get_nonlocal(sil, 0, 0);
    double inc = sil_as_double(sil, 0) + 1;
    sil_num(sil, inc, 1);
    sil_set_nonlocal(sil, 0, 1);
    return sil_return(sil, 0);
}

static enum sil_result silfnCounter(Sil *sil) {
    sil_use_slots(sil, 1);
    sil_num(sil, 0, 0);
    sil_closure(sil, silfn_Counter, "_counter", 1, 0);
    return sil_return(sil, 0);
}

static void loadLib(void) {
    sil_use_slots(sil, 1);

    sil_func(sil, silfnBool, "bool", 0);
    sil_define(sil, "bool", 0);

    sil_func(sil, silfnNum, "num", 0);
    sil_define(sil, "num", 0);

    sil_func(sil, silfnStr, "str", 0);
    sil_define(sil, "str", 0);

    sil_func(sil, silfnInput, "input", 0);
    sil_define(sil, "input", 0);

    sil_func(sil, silfnExit, "exit", 0);
    sil_define(sil, "exit", 0);

    sil_func(sil, silfnPrint, "print", 0);
    sil_define(sil, "print", 0);

    sil_func(sil, silfnClock, "clock", 0);
    sil_define(sil, "clock", 0);

    sil_func(sil, silfnError, "error", 0);
    sil_define(sil, "error", 0);

    sil_func(sil, silfnSilent, "silent", 0);
    sil_define(sil, "silent", 0);

    sil_func(sil, silfnProtect, "protect", 0);
    sil_define(sil, "protect", 0);

    sil_func(sil, silfnSizeOf, "sizeof", 0);
    sil_define(sil, "sizeof", 0);

    sil_func(sil, silfnGC, "gc", 0);
    sil_define(sil, "gc", 0);

    sil_func(sil, silfnRand, "rand", 0);
    sil_define(sil, "rand", 0);

    sil_func(sil, silfnEval, "eval", 0);
    sil_define(sil, "eval", 0);

    sil_func(sil, silfnTrace, "trace", 0);
    sil_define(sil, "trace", 0);

    sil_func(sil, silfnUse, "use", 0);
    sil_define(sil, "use", 0);

    sil_func(sil, silfnRegistrate, "registrate", 0);
    sil_define(sil, "registrate", 0);

    sil_func(sil, silfnExecute, "execute", 0);
    sil_define(sil, "execute", 0);

    sil_func(sil, silfnCounter, "counter", 0);
    sil_define(sil, "counter", 0);

    sil_num(sil, NAN, 0);
    sil_define(sil, "nan", 0);

    sil_num(sil, INFINITY, 0);
    sil_define(sil, "inf", 0);

    sil_strl(sil, XX, sizeof(XX) - 1, 0);
    sil_define(sil, "xx", 0);
}

static void init(void) {
    srand(time(NULL));
    for (int i = 0; i < 0x10; i++) srand(rand());
}

static void printVersion(void) {
    printf("sigil v0\n");
}

static void printHelp(void) {
    printVersion();
    printf("\n");
    printf("there is no help here\n");
}

int main(int argc, char **argv) {
    init();

#ifdef APP_SAFEREALLOC
    sil = sil_new(safeReallocate);
#else
    sil = sil_new(reallocate);
#endif

    signal(SIGINT, onSigint);

    loadLib();

    if (argc < 2) {
        repl();
    } else {
        if (strcmp(argv[1], "help") == 0) {
            printHelp();
            return 0;
        } else if (strcmp(argv[1], "version") == 0) {
            printVersion();
            return 0;
        } else if (strcmp(argv[1], ".") == 0) {
            script = readFile("__main.sil");
        } else {
            script = readFile(argv[1]);
        }
        file(script);
    }

    freeAndExit();

    return 0;
}
