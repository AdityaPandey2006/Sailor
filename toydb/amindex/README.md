# B+ tree index on roll-no (assignment part 3)

This directory builds a B+ tree index on the Student file, keyed on roll-no, using the existing layers: PF (part 1), the slotted-page Student file (part 2) and the supplied AM B+ tree. It contains:

* **Method 1**: index an existing Student file.
* **Method 2**: build the Student file and its index together, one record at a time.
* A **common benchmark, validator and tree checker** that Method 3 (bulk loading, Part B) can use unchanged.

| File | Purpose |
| --- | --- |
| `ixindex.h` | Public API: keys, measurement, build interface, queries, validation |
| `ixindex.c` | Key extraction, measurement, query sets and runner, tree inspection, validation |
| `ixbuild.c` | Method 1, Method 2, the method table `IX_Methods[]` |
| `amwrap.h` | Includes the K&R `am.h` from modern C, plus AM prototypes |
| `ixbench.c` | Benchmark CLI: one run = one JSON line |
| `testindex.c` | Correctness tests |
| `run_tests.py` | Builds, runs all tests, writes `outputs/index-construction/test-results.*` |
| `run_experiments.py`, `experiments.json` | Runs the benchmark matrix and writes tables to `outputs/index-construction/` |

Design decisions are in `outputs/design-decisions/part3-design-decisions.md`.

## Build and run (Linux or WSL)

Run these from the repository root:

```sh
make -C toydb/amindex                      # builds ixbench and testindex (also ../pflayer/pflayer.o)
python3 toydb/amindex/run_tests.py         # all correctness tests -> outputs/index-construction/test-results.txt
python3 toydb/amindex/run_experiments.py   # benchmark -> outputs/index-construction/comparison.md
```

Edit `experiments.json` to change the methods, buffer sizes, policy, query counts and seed, or the number of runs. No C changes are needed.

### Running one method by hand

```sh
cd toydb/amindex
mkdir -p work
./ixbench queries --data ../../data/student.txt --hits 2000 --misses 500 --seed 42 --out work/q.txt
./ixbench run --method 1 --data ../../data/student.txt --queries work/q.txt --workdir work --bufsize 20 --validate
./ixbench run --method 2 --data ../../data/student.txt --queries work/q.txt --workdir work --bufsize 20 --validate
./ixbench methods                          # lists registered methods
```

Options for `run`: `--reps R` (query repetitions, default 5), `--policy lru|mru`, `--keep` (keep the generated files). Method K writes `work/mK_students` (the Student file) and `work/mK_students.0` (its index). Without `--keep`, both are deleted after the run. `data/student.txt` is only read, never modified.

### Output files

| File | Content |
| --- | --- |
| `outputs/index-construction/comparison.md` | The comparison table, one section per buffer size |
| `summary.csv` | Medians per (buffer size, method) |
| `results.csv`, `raw_results.jsonl` | Every run |
| `queries.txt` | The exact query file used (one roll-no per line) |
| `config.json` | Configuration of the run |
| `test-results.txt`, `test-results.json` | Test log |

## What is measured

The benchmark keeps construction statistics and query statistics in separate snapshots. Each snapshot records the difference between two `PF_GetStats()` calls and the monotonic clock. `PF_ResetStats()` is never called, so snapshots can nest.

| Reported metric | PF counter | Meaning |
| --- | --- | --- |
| Pages accessed | `logical_reads` | Page requests to the buffer pool (`PF_GetThisPage`, `GetFirstPage`, `GetNextPage`, `AllocPage`), cached or not |
| Disk reads | `physical_reads` | Pages actually read from the file |
| Disk writes | `physical_writes` | Pages actually written (evictions plus flushes at close) |
| (also) `logical_writes` | | Dirty notifications |

* **Construction** includes closing the files, so every page the method dirtied is counted as written. Setup work that is not index construction is reported separately as `setup`. For Method 1, that is writing the Student file and opening it.
* **Queries** come from one shared query file (same keys, same order). Each query is an AM `EQUAL` index scan that reads all RIDs of the key. The index file is opened before the timer starts and closed after it stops. `PF_CloseFile` releases the file's buffers, so every repetition starts with none of the index's pages cached. Repetitions must agree exactly on pages accessed and on a checksum of the returned RIDs.

## Tests (`testindex`)

For the real student.txt (17,813 records, 3 duplicated roll-nos) and for a synthetic 150,000-record file (random insertion order, 149 keys with up to 40 RIDs each, 151 records with no roll-no), both methods are checked for:

1. a search for an existing roll-no, a non-existing roll-no and several roll-nos, with the expected number of RIDs each;
2. every returned RID pointing to a live Student record with that roll-no. `IX_Validate` checks this for **every** distinct key, plus 202 absent keys below, between and above the real ones;
3. index entries = records indexed = dataset records with a roll-no, using both a full AM scan and the tree walk;
4. close and reopen: validation and queries reopen the index, and the queries are repeated on a second reopen;
5. B+ tree invariants after construction (`IX_InspectTree`): page types, sorted keys, separator ranges, equal leaf depth, a complete leaf chain, and leftmost leaf = page 2;
6. Method 2 only: the same invariants plus searches at 20 checkpoints **during** construction;
7. identical answers from both methods for 600 queries;
8. the synthetic file gives a **3-level** tree (5 internal pages, 894 leaves), which exercises internal-node and root splits.

The tests run with 3, 20 and 200 buffer frames. Three is AM's minimum, since a root split fixes 3 pages, so passing with 3 frames shows that no page is ever left fixed. They also pass under AddressSanitizer and UBSan.

## Changes to existing code

Only `toydb/amlayer/amscan.c` was changed. Three small fixes are marked `PART 3 FIX n`:

1. `AM_OpenIndexScan` now calls `AM_EmptyStack()` after `AM_Search`. Before, scans never emptied AM's 50-entry path stack, so once the root was internal the 51st scan overflowed `AM_Stack`. AddressSanitizer found this.
2. `AM_OpenIndexScan`: when the search position is past the last key of a leaf, the next leaf's number is saved *before* its header replaces `header`. The old code unfixed `header->nextLeafPage`, which by then named the leaf after the next one. The result was `AME_PF` for many absent-key searches, and a leaf left fixed.
3. The same defect in the `NOT_EQUAL` branch of `AM_FindNextEntry`.

Search semantics are unchanged. Nothing in PF or SP was changed. The prebuilt `.o` files in `toydb/amlayer` are from the original sources and are not used. This directory compiles its own copies.

**Why the AM sources are compiled from copies:** `toydb/amlayer/pf.h` still says `PF_PAGE_SIZE 1020`, but the PF layer uses 4096. AM sources include `"pf.h"`, which the compiler looks up next to the source file first. Compiling copies in `build/amsrc/` makes that include resolve to `toydb/pflayer/pf.h`. A test checks that the root's `maxKeys` is 340, the value for 4096-byte pages.

## Handover for Part B (Method 3, bulk loading)

### APIs to call

```c
#include "amwrap.h"       /* AM + PF prototypes, AM page-header structs */
#include "slotted.h"      /* Student file: SP_CreateFile/OpenFile/InsertRec/Scan... */
#include "ixindex.h"      /* everything below */

IX_ExtractKey(rec, len, key);              /* roll-no -> 8-byte key, or IXE_NOKEY */
IX_IndexFileName(studentFile, 0, name, n); /* "<studentFile>.0" */
IX_LoadStudentFile(data, studentFile, policy, &stored);  /* write records in the given order */
IX_TimerStart(&t); ...; IX_TimerStop(&t, &r->build);     /* measurement */
IX_InspectTree(studentFile, 0, &info);     /* B+ tree invariants + shape */
IX_Validate(studentFile, 0, data, &v);     /* every key -> correct RIDs */
IX_RunQueries(studentFile, 0, &queries, &qr);            /* common benchmark step */
IX_OpenScan(amfd, EQUAL, key);             /* AM scan with type/length filled in */
```

### Adding Method 3

1. Write `ixbulk.c` with `int IX_Method3(const IX_BuildSpec *spec, IX_BuildResult *r)`, and declare it in `ixindex.h`. The function must:
   * sort a copy of `spec->data` by key (`memcmp` of the 8-byte keys from `IX_ExtractKey`, which is the same order as AM's `strncmp`). Keep records without a key, but don't index them;
   * write the sorted Student file to `spec->studentFile`, for example with `IX_LoadStudentFile`, and measure that as `r->setup`;
   * bulk-build `spec->studentFile.<spec->indexNo>` from the sorted (key, RID) pairs, measured as `r->build`, including closing the files;
   * set `recordsTotal`, `recordsIndexed` and `recordsSkipped`.
2. Add `{ 3, "bulk", "...", IX_Method3 }` to `IX_Methods[]` in `ixbuild.c`, and add `build/ixbulk.o` to `IX_OBJ` in the `Makefile`.
3. Run `./ixbench run --method 3 ... --validate`. Then add `3` to `"methods"` in `experiments.json` and rerun `run_experiments.py`. The comparison table gains a Method 3 column, with the same queries and the same procedure.

Method 3's Student file is sorted, so its RIDs differ from Methods 1 and 2, and the `rid_checksum` values will not match. Correctness is shown by `--validate` (which checks contents, not RIDs) and by `found`/`rids` counts equal to the other methods.

### Index file format (must be kept so AM can read it)

The index is the unmodified AM format: a PF file of 4096-byte pages.

* **Root is page 0, always.** `AM_Search` starts at `PF_GetFirstPage`, and `AM_InsertEntry` detects a root split by `pageNum == AM_RootPageNum` (the global, which is 0).
* **When the root is internal, the leftmost leaf must be page 2.** `GetLeftPageNum` in `amscan.c` hard-codes this for full scans and `<`, `<=`, `!=` scans. `IX_InspectTree` checks it.
* **Leaf page:** `AM_LEAFHEADER` (24 bytes on x86-64; fields `pageType='l'`, `nextLeafPage` (-1 = last), `recIdPtr`, `keyPtr`, `freeListPtr`, `numinfreeList`, `attrLength=8`, `numKeys`, `maxKeys`). It is followed, from offset 24, by `numKeys` sorted entries `[key 8 bytes][short offset of first RID node]`, so `keyPtr = 24 + 10 * numKeys`. RID nodes `[int recId][short next]` (6 bytes) grow down from the end of the page, and `recIdPtr` is the lowest one. `next = 0` ends a key's list. A key needs 16 bytes plus 6 per extra duplicate, so a leaf holds at most 254 distinct keys (Methods 1 and 2 average about 163).
* **Internal page:** `AM_INTHEADER` (8 bytes: `pageType='i'`, `numKeys`, `maxKeys=340`, `attrLength=8`), then `[child0][key1][child1]...[keyN][childN]` with 4-byte page numbers. Child *i* holds keys *k* with `key_i <= k < key_(i+1)`, so a separator equals the first key of its right subtree. Use `maxKeys = 340`, as `AM_CreateIndex` does, so that later `AM_InsertEntry` calls on a bulk-loaded index still split correctly.
* An index created with `AM_CreateIndex(file, 0, 'c', 8)` is a valid empty tree: a root leaf in page 0.

### Assumptions

* Key: roll-no = field 2 of the line, `'c'`, length 8, `'\0'`-padded. Records without a roll-no are not indexed.
* RID: `page * 65536 + slot` of the slotted-page file (`REC_MakeRid`), a non-negative `int`.
* Duplicates: one key entry per leaf, with all RIDs in its list. A key's list must not be split across leaves, because AM's `EQUAL` scan stops at the end of the leaf.
* AM uses a global root page number and a global scan table (20 scans). Close every scan with `AM_CloseIndexScan`.

## Limitations

* AM allows about 670 RIDs per key, since one key's list must fit in a leaf. Leaf splits divide by key count, so heavily skewed duplicates could overflow a half-page. The data has at most 2 RIDs per key.
* The benchmark uses point queries only. Range scans (`GREATER_THAN_EQUAL` + key check) are not measured.
* The timings are milliseconds-scale. Page counts are deterministic and are the robust metric; times vary from run to run, which is why medians are reported.
