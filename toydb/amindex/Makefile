# Index construction on roll-no (assignment part 3).
#
#   make            build ixbench and testindex
#   make clean      remove everything built here
#
# Uses, without modifying them:
#   ../pflayer   PF layer (rebuilt first so pflayer.o matches its sources)
#   ../splayer   slotted-page Student file (sources compiled into build/)
#   ../amlayer   the supplied AM B+ tree (sources compiled into build/)
#
# Why the AM sources are copied before compiling: they say #include "pf.h",
# and a quoted include is looked up in the source file's own directory
# first.  toydb/amlayer still holds the original pf.h with
# PF_PAGE_SIZE 1020, while the PF layer we link uses 4096 (part 1).  Copying
# each .c into build/amsrc/ makes the include resolve through -I$(PF) to
# the real header, so the B+ tree uses full 4096-byte pages.

PF  = ../pflayer
SP  = ../splayer
AM  = ../amlayer
CC ?= cc

CFLAGS    = -std=gnu99 -O2 -Wall -Wextra -Wno-unused-function -I$(PF) -I$(SP) -I$(AM)
# The supplied PF and AM sources are K&R C: GCC 14+ rejects them by default
# (and GCC 15 defaults to C23), so they are compiled as GNU C17 with legacy
# errors downgraded to warnings, which are silenced for AM (-w).
PF_CFLAGS = -std=gnu17 -fpermissive
AM_CFLAGS = -std=gnu17 -fpermissive -w -O2 -I$(PF) -I$(AM)

AM_SRC = am amfns amsearch aminsert amstack amglobals amscan amprint
AM_OBJ = $(patsubst %,build/am_%.o,$(AM_SRC))
SP_SRC = recfile slotted studentdata
SP_OBJ = $(patsubst %,build/sp_%.o,$(SP_SRC))
IX_OBJ = build/ixindex.o build/ixbuild.o
HDR    = amwrap.h ixindex.h $(PF)/pf.h $(AM)/am.h $(SP)/recfile.h $(SP)/slotted.h \
         $(SP)/studentdata.h
LIBS   = $(IX_OBJ) $(SP_OBJ) $(AM_OBJ) $(PF)/pflayer.o
PROGS  = ixbench testindex

all: $(PROGS)

build build/amsrc:
	mkdir -p $@

$(PF)/pflayer.o: $(PF)/pf.c $(PF)/buf.c $(PF)/hash.c $(PF)/pf.h $(PF)/pftypes.h
	$(MAKE) -C $(PF) pflayer.o CFLAGS="$(PF_CFLAGS)"

build/amsrc/%.c: $(AM)/%.c | build/amsrc
	cp $< $@

build/am_%.o: build/amsrc/%.c $(AM)/am.h $(PF)/pf.h | build
	$(CC) $(AM_CFLAGS) -c $< -o $@

build/sp_%.o: $(SP)/%.c $(HDR) | build
	$(CC) $(CFLAGS) -c $< -o $@

build/%.o: %.c $(HDR) $(SP)/testutil.h | build
	$(CC) $(CFLAGS) -c $< -o $@

ixbench: build/ixbench.o $(LIBS)
	$(CC) -o $@ $^

testindex: build/testindex.o $(LIBS)
	$(CC) -o $@ $^

clean:
	rm -rf build $(PROGS)

.PHONY: all clean
.SECONDARY:
