appname ?= sil
script ?= .
o ?= 0

flags = -std=c99 -D_CRT_SECURE_NO_WARNINGS -pedantic

srcfiles = src/cmn.c src/val.c src/gco.c src/map.c src/lex.c src/cpl.c \
	src/core.c

ifdef debug
	flags += -DDEBUG_GC -DDEBUG_LEX -DDEBUG_COMPILE -DDEBUG_EXECUTE \
		-DDEBUG_ASSERT
endif

ifdef sanitize
	flags += -fsanitize=address -g -DAPP_SAFEREALLOC -DGC_STRESS -DDEBUG_ASSERT
endif

ifdef syntax
	flags += -DAUTOSEMI -DALTKEYWORDS -DLOOPELSE
endif

ifdef nanbox
	flags += -DNANBOXING
endif

ifdef switched
	flags += -DSWITCHEDGOTO
endif

build:
	@ clang -O${o} -o $(appname).exe app/main.c $(flags) $(srcfiles)

execute:
	@ ./$(appname).exe $(script)

destroy:
	@ del $(appname).exe
	@ del $(appname).pdb 2>nul

run: build execute destroy

release: 
	@ $(MAKE) build nanbox=1 switched=1 syntax=1 o=2

.PHONY: build execute destroy run release
