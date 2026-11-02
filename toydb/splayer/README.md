# Slotted-page record layer (assignment part 2)

Variable-length records stored in slotted pages on top of the PF layer, plus
a fixed-length ("static") record layer used as the comparison baseline. Both
use only the PF interface; no PF or AM source file is modified.

| File | Purpose |
| --- | --- |
| `recfile.h/.c` | Shared by both layers: RID packing, error codes, first-fit search modes, `REC_SpaceStats` |
| `slotted.h/.c` | Slotted-page layer (`SP_*`) |
| `fixedrec.h/.c` | Fixed-length layer (`FX_*`) |
| `studentdata.h/.c` | Reads `data/student.txt` into raw records |
| `testslotted.c`, `testfixed.c` | Unit tests for each layer |
| `testload.c` | Loads the real student file into both layouts and verifies every record |
| `testutil.h` | Test helpers (`CHECK`, shadow model, LCG) |
| `spbench.c` | One experiment run, printed as one JSON line |
| `run_tests.py` | Builds and runs every test and writes a report |
| `run_experiments.py`, `experiments.json` | Runs the experiments and writes tables, CSVs and graphs |

## Slotted page layout

The full 4096-byte PF data area is used. The PF layer keeps its own page
header outside it. All fields are 16-bit integers at fixed offsets, and no
pointers are stored.

```
offset 0  magic 0x5350 | 2 numSlots | 4 liveCount | 6 freeEnd
offset 8  slot directory: numSlots x {offset, length}   (empty slot: offset 0)

[header 8B][slot 0][slot 1]...[slot n-1]   free space   [rec]...[rec][rec]
                               ^freeStart = 8 + 4n      ^freeEnd       ^4096
```

The directory grows forward and records grow backward from the end of the
page. Because deletes compact the page at once, the record area is always
exactly `[freeEnd, 4096)`. Every page is checked against this rule
whenever it is fetched. The largest record is 4084 bytes.

**RID** = `page * 65536 + slot`. This is a single non-negative `int`, which
is what `AM_InsertEntry` expects. `REC_MakeRid` and `REC_SplitRid` handle the
conversion.

## API

```c
PF_InitWithBufferSize(20);
SP_CreateFile("students");
int fd = SP_OpenFile("students", PF_POLICY_LRU, REC_SEARCH_CACHED);
SP_InsertRec(fd, bytes, len, &rid);       /* first fit; reuses empty slots   */
SP_GetRec(fd, rid, buf, sizeof buf, &len);
SP_DeleteRec(fd, rid);                    /* compacts the page immediately   */
SP_Scan sc; SP_ScanOpen(fd, &sc);
while (SP_ScanNext(&sc, &rid, buf, sizeof buf, &len) == RECE_OK) { ... }
SP_ScanClose(&sc);
SP_GetSpaceStats(fd, &stats);             /* REC_SpaceStats, see recfile.h   */
SP_CloseFile(fd);
```

The `FX_*` functions mirror these. `FX_OpenFile` also takes the record length
`L`, and `FX_InsertRec` rejects records longer than `L` with `RECE_RECSIZE`.
Errors are negative. Record-layer codes start at -101; PF codes pass through
unchanged.

PF discipline: a call fixes at most one page and unfixes it on every path.
Each update is reported once with `PF_MarkDirty` followed by
`PF_UnfixPage(..., FALSE)`, so an insert or delete counts as exactly one
logical write. The tests also pass with a 1-frame buffer pool.

## Build, test, experiment (Linux or WSL)

```sh
make -C toydb/splayer                       # also rebuilds ../pflayer/pflayer.o
python3 toydb/splayer/run_tests.py          # add --with-pf to include part-1 tests
python3 toydb/splayer/run_experiments.py    # needs matplotlib for the graphs
```

The Makefile compiles the K&R PF sources with `-std=gnu17 -fpermissive`.
GCC 14 and later reject that code by default, and GCC 15 also switches the
default standard to C23.

Design decisions are documented in
`outputs/design-decisions/part2-design-decisions.md`. Results are written to
`outputs/slotted-pages/`.
