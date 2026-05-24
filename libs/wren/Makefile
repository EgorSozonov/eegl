#{{{ Boilerplate

.RECIPEPREFIX = /
.DEFAULT_GOAL = all

ifndef VERBOSE
.SILENT: # Silent mode unless you run it like "make all VERBOSE=1"
endif


help: ## Show this help
/ @grep -E -h '\s##\s' $(MAKEFILE_LIST) | sort | awk 'BEGIN {print "[Help]";print ""; FS = ":.*?## "}; {printf "\033[32m%-10s\033[0m %s\n", $$1, $$2}'
/ echo
# MAKEFILE_LIST lists the contents of this present file
# egrep selects only lines with the double sharp, they are then sorted
# BEGIN in AWK means an action to be executed once before the linewise
# FS means "field separator" - the separator between parts of a single line
# the printf looks so scary because of the ASCII color codes

#}}}
#{{{ Params

.PHONY: all library debug clean help testLexer testParser testIntegration test

CC=gcc --std=c23
WARN=-Werror=return-type -Wunused-variable -Wshadow -Wfatal-errors \
    -Werror=implicit-function-declaration -Werror=incompatible-pointer-types \
    -Wno-discarded-qualifiers \
    -Werror=int-conversion -fstrict-flex-arrays=3
SANITIZE=-fsanitize=address # include it occasionally
INCLUDES=-iquote .
OPT=-march=native
LDFLAGS=-Wl,--exclude-libs=ALL

APP=wren

RELEASE_FLAGS = $(WARN) $(OPT) $(INCLUDES) -O2
COMPILE = $(CC) $(RELEASE_FLAGS) -gdwarf-5

EXE=$(APP).o

#}}}
#{{{ Commands

$(BIN):
/ mkdir -p $(BIN)

all:  ## Build the program
/ clear
/ $(COMPILE) -O0 -gdwarf-5 -o hw src/wren.c src/main.c -lm
/ @echo "Wren compiled successfully"


#}}}
