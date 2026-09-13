#ifndef sil_lex_h
#define sil_lex_h
#include "cmn.h"
//

#define LITLX(lit)                                                             \
    (Lx){.type = LX_WORD, .start = lit, .len = sizeof(lit) - 1, .ln = 0}

#define LXS_X                                                                  \
    X(CONTINUE) /* continue       */                                           \
    X(DEFAULT)  /* default        */                                           \
    X(FINALLY)  /* finally        */                                           \
    X(DELETE)   /* delete         */                                           \
    X(RETURN)   /* return         */                                           \
    X(SWITCH)   /* switch         */                                           \
    X(TYPEOF)   /* typeof         */                                           \
    X(BREAK)    /* break          */                                           \
    X(CATCH)    /* catch          */                                           \
    X(CLASS)    /* class          */                                           \
    X(CONST)    /* const          */                                           \
    X(FALSE)    /* false          */                                           \
    X(THROW)    /* throw          */                                           \
    X(WHILE)    /* while          */                                           \
    X(CASE)     /* case           */                                           \
    X(ELSE)     /* else           */                                           \
    X(ENUM)     /* enum           */                                           \
    X(FUNC)     /* func           */                                           \
    X(GOTO)     /* goto           */                                           \
    X(THEN)     /* then           */                                           \
    X(TRUE)     /* true           */                                           \
    X(VOID)     /* void           */                                           \
    X(AND)      /* and            */                                           \
    X(FOR)      /* for            */                                           \
    X(NEW)      /* new            */                                           \
    X(NOT)      /* not            */                                           \
    X(TRY)      /* try            */                                           \
    X(VAR)      /* var            */                                           \
    X(DO)       /* do             */                                           \
    X(IF)       /* if             */                                           \
    X(IS)       /* is             */                                           \
    X(OR)       /* or             */                                           \
    /* */                                                                      \
    X(COMMA)          /* ,              */                                     \
    X(EQUALS)         /* =              */                                     \
    X(PLUS_EQUALS)    /* +=             */                                     \
    X(MINUS_EQUALS)   /* -=             */                                     \
    X(AST_EQUALS)     /* *=             */                                     \
    X(SOL_EQUALS)     /* /=             */                                     \
    X(PERCNT_EQUALS)  /* %=             */                                     \
    X(AST_AST_EQUALS) /* **=            */                                     \
    X(SOL_SOL_EQUALS) /* //=            */                                     \
    X(VERBAR_EQUALS)  /* |=             */                                     \
    X(HAT_EQUALS)     /* ^=             */                                     \
    X(AMP_EQUALS)     /* &=             */                                     \
    X(LT_LT_EQUALS)   /* <<=            */                                     \
    X(GT_GT_EQUALS)   /* >>=            */                                     \
    X(PLUS_PLUS)      /* ++             */                                     \
    X(MINUS_MINUS)    /* --             */                                     \
    X(QUEST)          /* ?              */                                     \
    X(COLON)          /* :              */                                     \
    X(VERBAR_VERBAR)  /* ||             */                                     \
    X(AMP_AMP)        /* &&             */                                     \
    X(VERBAR)         /* |              */                                     \
    X(HAT)            /* ^              */                                     \
    X(AMP)            /* &              */                                     \
    X(EQUALS_EQUALS)  /* ==             */                                     \
    X(EXCL_EQUALS)    /* !=             */                                     \
    X(LT)             /* <              */                                     \
    X(LT_EQUALS)      /* <=             */                                     \
    X(GT)             /* >              */                                     \
    X(GT_EQUALS)      /* >=             */                                     \
    X(LT_LT)          /* <<             */                                     \
    X(GT_GT)          /* >>             */                                     \
    X(PLUS)           /* +              */                                     \
    X(MINUS)          /* -              */                                     \
    X(AST)            /* *              */                                     \
    X(SOL)            /* /              */                                     \
    X(PERCNT)         /* %              */                                     \
    X(AST_AST)        /* **             */                                     \
    X(SOL_SOL)        /* //             */                                     \
    X(EXCL)           /* !              */                                     \
    X(TILDE)          /* ~              */                                     \
    X(DOT)            /* .              */                                     \
    X(MINUS_GT)       /* ->             */                                     \
    X(LPAREN)         /* (              */                                     \
    X(RPAREN)         /* )              */                                     \
    X(LSQB)           /* [              */                                     \
    X(RSQB)           /* ]              */                                     \
    X(LCUB)           /* {              */                                     \
    X(RCUB)           /* }              */                                     \
    X(HELLIP)         /* ...            */                                     \
    X(EQUALS_GT)      /* =>             */                                     \
    X(SEMI)           /* ;              */                                     \
    /* */                                                                      \
    X(WORD) /* word           */                                               \
    X(NUM)  /* num            */                                               \
    X(STR)  /* str            */                                               \
    /* */                                                                      \
    X(LINE)  /* line           */                                              \
    X(ERROR) /* error          */                                              \
    X(EOF)   /* eof            */

#define X(name) LX_##name,
enum lxtype { LXS_X };
#undef X

typedef struct {
    enum lxtype type;
    const char *start;
    int len;
    int ln;
} Lx;

typedef struct {
    const char *cur, *start;
    bool insertln;
    int currln;
} Lxr;

void sil__initlxr(Lxr *l, const char *src);
Lx sil__lex(Lxr *l);
Lx sil__lexstr(Lxr *l);

bool sil__iskwtype(enum lxtype type);
bool sil__isassigntype(enum lxtype type);

#endif  // sil_lex_h
