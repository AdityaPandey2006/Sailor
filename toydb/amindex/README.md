# B+ tree index on roll-no (assignment part 3)

This directory builds a B+ tree index on the Student file, keyed on roll-no, using the existing layers: PF (part 1), the slotted-page Student file (part 2) and the supplied AM B+ tree. It contains:

* **Method 1**: index an existing Student file.
* **Method 2**: build the Student file and its index together, one record at a time.
* **Method 3**: bottom-up bulk loading from a sorted key/RID stream.
* A **common benchmark, validator and tree checker** used by all three methods.

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

## Method 3: sorted bottom-up bulk loading

`ixbulk.c` implements `IX_Method3` and is registered alongside Methods 1 and 2.
It sorts a separate array of record ordinals and 8-byte keys, preserving the
original dataset. Ties use the original ordinal, making duplicate order stable.
Missing/oversized keys sort last: their records are stored, but not indexed.

Setup includes sorting, writing the reordered slotted Student file, and collecting
(key, actual new RID) pairs as records are inserted. This sorted in-memory stream
is the input to construction; the original RIDs are never reused. First-fit
placement can fill holes in earlier Student pages, so a physical SP scan need not
be sorted: the key/RID stream is the authoritative sorted representation.

Construction groups equal keys, packs leaves by actual byte capacity, and balances
the final pair of leaves when possible. A key's full RID list stays on one leaf;
a list too large for one leaf returns IXE_INVALID. A 677-RID single-key list fits;
678 does not. Leaves have the usual AM key directory and linked RID nodes.
The last leaf occupies page 1 and the first occupies page 2, so all allocated
pages are reachable and AM's hard-coded leftmost-leaf convention is preserved.
Empty and single-leaf trees live entirely in root page 0.

Parent levels are built bottom-up with separators equal to the first key of each
right subtree. Fanout is balanced across each level, up to 341 children per node,
avoiding a singleton final internal node. The final root is written to page 0.
The AM header layout and normal AM search/scan code are unchanged. Normal AM
insertions and deletions after bulk loading are also exercised by tests.

```sh
# From the repository root:
make -C toydb/amindex
python3 toydb/amindex/run_tests.py
# Requires matplotlib in this Python environment:
python3 toydb/amindex/run_experiments.py

# Run Method 3 manually:
cd toydb/amindex
mkdir -p work
./ixbench queries --data ../../data/student.txt --hits 2000 --misses 500 --seed 42 --out work/q.txt
./ixbench run --method 3 --data ../../data/student.txt --queries work/q.txt --workdir work --bufsize 20 --validate --keep
```

The experiment runner now generates construction-time.png, query-time.png,
query-pages.png and construction-pages.png in outputs/index-construction/.
It checks equal result counts across methods; Method 3 RIDs/checksums differ
because records were reordered. Every RID is validated against its Student record.

### Final validation

Five test groups pass: bulk boundary/duplicate/AM-mutation cases, real and
150,000-record synthetic datasets with 3 and 20 frames, the real dataset with
200 frames, and a validated three-method benchmark smoke test. The bulk boundary
suite covers 0, 1, 254, 255, 508, 509 and 87,000 records; 87,000 forces a
three-level bulk tree. It verifies every generated record byte-for-byte after
reordering and checks duplicate list overflow explicitly. Existing validation
checks every distinct roll number, returned RIDs, absent keys, all entries via
AM scans, sibling links and separator ranges after reopening.

The benchmark uses 17,813 records, 17,810 distinct keys, 2,500 identical point
queries, three buffer sizes and five independent runs (45 builds total), with
five cold query repetitions per run. See comparison.md and analysis.md in the
results directory for measured results and interpretation. Construction, setup,
and queries use separate counter/time snapshots. Bulk construction consumes
already collected sorted pairs, whereas Method 1 scans its existing file; compare
setup + construction for the full preprocessing cost. Fresh page allocations are
not themselves logical reads; subsequent fetches are. Physical counts are OS page
transfers, not guaranteed hardware disk accesses.

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
