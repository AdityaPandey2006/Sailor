# Slotted-page (part 2) layer.  Links against the PF layer in ../pflayer,
# which is rebuilt first so its object file always matches its sources.
#
#   make            build the library objects, tests and benchmark
#   make tests      build the test programs only
#   make clean      remove everything built here

PF      = ../pflayer
CC     ?= cc
CFLAGS  = -std=gnu99 -O2 -Wall -Wextra -Wno-unused-function -I$(PF)
# The supplied PF sources are K&R C.  GCC 14+ rejects them by default (and
# GCC 15 defaults to C23, where "f()" means "f(void)"), so they are compiled
# as GNU C17 with legacy errors downgraded to warnings.  GCC 13 accepts this too.
PF_CFLAGS = -std=gnu17 -fpermissive

LIB_OBJ = recfile.o slotted.o fixedrec.o studentdata.o
HDR     = recfile.h slotted.h fixedrec.h studentdata.h testutil.h $(PF)/pf.h
TESTS   = testslotted testfixed testload
PROGS   = $(TESTS) spbench

all: $(PROGS)
tests: $(TESTS)

$(PF)/pflayer.o: $(PF)/pf.c $(PF)/buf.c $(PF)/hash.c $(PF)/pf.h $(PF)/pftypes.h
	$(MAKE) -C $(PF) pflayer.o CFLAGS="$(PF_CFLAGS)"

%.o: %.c $(HDR)
	$(CC) $(CFLAGS) -c $<

$(PROGS): %: %.o $(LIB_OBJ) $(PF)/pflayer.o
	$(CC) -o $@ $^

clean:
	rm -f *.o $(PROGS)

.PHONY: all tests clean
