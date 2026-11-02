# ToyDB assignment

| Part | Topic | Status | Code | Results |
| --- | --- | --- | --- | --- |
| 1 | PF page buffering (LRU/MRU, dirty flag, statistics) | Done | `toydb/pflayer/` | `outputs/page-buffering/`, `summary.pdf` |
| 2 | Slotted pages for variable-length records vs fixed-length records | Done | `toydb/splayer/` | `outputs/slotted-pages/`, `summary-part2.pdf` |
| 3 | Index construction on roll-no (AM layer, bulk loading) | Not started | `toydb/amlayer/` | - |

Design decisions for every part are in `outputs/design-decisions/`. The
workbook has one sheet per part, and part 2 also has a Markdown version.

## Requirements

- Linux or WSL with a C compiler and GNU make. Verified with GCC 15.2 (and
  GCC 13.3 for part 1).
- Python 3.10 or later.
- matplotlib, only for the graphs.

The PF layer uses POSIX file I/O, so build and run under Linux/WSL, not
native Windows.

**Compiler note.** The supplied PF sources are K&R C. GCC 14 and later turn
their legacy constructs into errors, and GCC 15 also defaults to C23. Build
the PF layer with `CFLAGS="-std=gnu17 -fpermissive"`, as shown below. No
source change is needed, and GCC 13 accepts the same flags. The part-2
Makefile passes these flags automatically.

```sh
python3 -m venv ~/.venvs/toydb && ~/.venvs/toydb/bin/pip install matplotlib
```

Use a venv under your home directory. On WSL, `/tmp` is cleared whenever the
distribution restarts.

## Part 1: page buffering

Part 1 provides a configurable pool capacity, a per-open-file choice of LRU
or MRU over one shared global pool, explicit dirty marking, statistics, tests
and reproducible workload graphs.

### Build and test

```sh
make -B -C toydb/pflayer tests benchmark CFLAGS="-std=gnu17 -fpermissive"
python3 toydb/pflayer/run_tests.py
```

Use `-B` to rebuild the supplied object files instead of trusting the old
precompiled binaries. The original source still emits legacy-C compiler
warnings. The runner isolates test files in temporary directories and exits
on any failure.

### Run experiments

```sh
~/.venvs/toydb/bin/python toydb/pflayer/run_experiments.py
```

Edit `toydb/pflayer/experiments.json` to change pool size, file size, query
count, write percentages, access patterns, policies, repetitions and seed. No
C changes are needed. Optional arguments are `--config path.json` and
`--output directory`.

Results go to `outputs/page-buffering/`: individual runs in `results.csv`,
means in `summary.csv`, the exact configuration in `config.json`, and graphs
in `sequential.png` and `random.png`.

### Counter definitions and API

`PF_ResetStats()` clears pool-wide counters without flushing or clearing
caches. `PF_GetStats()` returns a snapshot, and `PF_PrintStats()` prints it.

| Counter | Definition |
| --- | --- |
| logical_reads | Successful buffer fetches, including internal fetches for allocation/disposal or scanning free pages |
| logical_writes | Successful update notifications: `PF_MarkDirty`, `PF_UnfixPage(..., TRUE)`, or internal `PFbufUsed` |
| physical_reads | Successful full-page reads from the OS |
| physical_writes | Successful full-page writes to the OS, including eviction and close flushes |
| buffer_hits / buffer_misses | Successful buffer fetches classified by whether already cached |

Logical I/O is logical_reads + logical_writes, and physical I/O is
physical_reads + physical_writes. These count operations, not distinct
pages. Header I/O, failed operations and fresh page allocation itself are
excluded.

A write query fetches a page and reports one update, so it contributes one
logical read and one logical write. Reporting the same update through both
dirty APIs counts twice, so the benchmark marks once and unfixes with FALSE.
Allocation can dirty pages internally without an update notification. Setup
is excluded from the experiments.

Physical counts measure ToyDB-to-OS page transfers, not hardware disk
activity, because the OS may serve reads from its cache. Successful
writeback clears dirty state; failed writeback keeps it. Marking or unfixing
a page dirty does not force a write.

### Experiment method and interpretation

The default configuration runs 60 trials: two access patterns, two policies,
five read/write mixtures and three repetitions. Each trial starts with a
newly created 100-page file and an empty ToyDB pool of 20 frames. The setup
is closed before the counters are reset. Each trial performs 10,000 queries
and includes the final close/dirty flush in the counts and elapsed time.

Each write toggles a byte, and each read fetches a page. Write queries are
interspersed deterministically, with exactly
floor(query_count * write_percentage / 100) writes.

Sequential access repeatedly scans the file. Random access uses a fixed
32-bit LCG with a seed per repetition. The same page trace is reused for
both policies and all write mixtures. Times use a monotonic clock and
include page access, updates and the final close, but exclude creation and
the initial open. Repetitions are averaged for the plots, and raw timings
remain in the CSV. Lines overlap where the metrics are identical.

The default sequential experiment gives 10,000 physical reads with LRU and
8,100 with MRU. At 50% writes, it gives 5,000 physical writes with LRU and
4,050 with MRU. MRU keeps some pages across repeated scans that are larger
than the pool. Random results are similar between policies and vary with
the seed. These are synthetic page workloads and do not show that MRU is
always faster.

### Design

See `toydb/pflayer/README.md` for the page APIs. The requesting file's policy
chooses a global unfixed victim, even one from another file. Local
replacement is deferred, as recorded in
`outputs/design-decisions/toydb-design-decisions.xlsx`.

## Part 2: slotted pages

Part 2 is a slotted-page record layer on top of PF. It supports insertion,
deletion, point lookup by RID and sequential scanning, and it is used to
store the 17,813 student records. A separate fixed-length layer is the
"static" baseline. See `toydb/splayer/README.md` for the page layout and API.

### Build, test and run

```sh
make -C toydb/splayer                                   # builds PF too
python3 toydb/splayer/run_tests.py --with-pf            # 26 cases: 12 part-1 + 14 part-2
~/.venvs/toydb/bin/python toydb/splayer/run_experiments.py
```

`run_tests.py` rebuilds from source and runs every case in its own temporary
directory. It runs all cases even if one fails, then exits non-zero if any
failed. It writes `outputs/slotted-pages/test-results.txt` (with each
program's output) and `test-results.json`. Use `--no-build` to skip the
rebuild.

The tests cover:

- insertion, lookup and errors
- first-fit placement
- deletion with immediate compaction
- slot reuse and directory trimming
- sequential scans
- persistence after reopening
- exact logical read/write counts per operation
- the space-accounting invariant
- 4,000 random operations checked against an in-memory model, where both
  search modes must produce identical RIDs
- loading and byte-for-byte verification of the full student file

Each test runs with buffer pools of 1, 2 and 20 frames. The 1-frame case
proves that no operation leaves a page fixed.

`run_experiments.py` reads `toydb/splayer/experiments.json`, which sets the
record lengths L, the delete percentages, the buffer size, policies, search
modes, repetitions and seed. It writes the following to
`outputs/slotted-pages/`:

- `space_utilization.md` and `io_comparison.md` (the tables)
- `space_results.csv`, `io_results.csv` and `raw_results.jsonl`
- `config.json`
- `utilization.png` and `io_load.png`

### Design decisions (details in `outputs/design-decisions/part2-design-decisions.md`)

1. **Records are raw text lines.** Each `student.txt` line is stored
   unchanged, and fields are split on `;` when needed.
2. **RIDs and slot reuse.** RID = page x 65536 + slot, which fits one
   non-negative `int` as the AM layer requires. Deleted slots are reused, so
   part 3 must remove a student's index entry before the slot is reused.
3. **Fixed-length baseline.** Each record gets exactly L bytes. Records
   longer than L are rejected and counted, never truncated.
4. **Utilization = record bytes / (pages x 4096).** This uses the same
   formula for both layouts, and all overhead is included.
5. **First fit** for the load and for later inserts. The default
   implementation keeps per-page free space in memory and picks the same
   page as a literal page-by-page scan, which is kept for comparison.
6. **Compaction on every delete.** Free space stays contiguous, and slot
   numbers (RIDs) never change.

### Results (20-page buffer, LRU)

| Layout | L | Records stored | Pages | Utilization |
| --- | --- | --- | --- | --- |
| Slotted | - | 17,813 | 453 | **94.80%** |
| Fixed | 100 | 12,064 (5,749 rejected) | 302 | 94.88% |
| Fixed | 108 (= longest) | 17,813 | 482 | 89.10% |
| Fixed | 128 | 17,813 | 575 | 74.69% |
| Fixed | 160 | 17,813 | 713 | 60.23% |
| Fixed | 200 | 17,813 | 891 | 48.20% |
| Fixed | 256 | 17,813 | 1,188 | 36.15% |

Slotted pages beat every fixed layout that can hold all records. The
slotted layout spends 74,876 bytes on headers and the slot directory (4
bytes per record). Fixed L=108 wastes 206,727 bytes on padding, and the
waste grows with L. Fixed L=100 matches slotted utilization only by
discarding a third of the students.

After deleting 20% or 50% of the records, utilization falls in proportion,
because no page is released. Re-inserting the same records restores exactly
the original utilization in every layout.

For I/O, the literal page-by-page first fit needs about 4.0 million physical
reads to load the file, compared with 11 for the cached search. That is
about 4.5 s against 10 ms. With the scan search, MRU saves about 8% of those
reads compared with LRU, because the search repeatedly scans more pages than
the pool holds.

## Remaining scope

Part 3 has not been started. The AM layer and its tests have not been rebuilt
or validated. Existing legacy C declarations and warnings in the supplied
code have not been modernized.
