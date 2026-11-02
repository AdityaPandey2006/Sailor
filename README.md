# ToyDB assignment

Part 1 (page buffering) implements configurable pool capacity, per-open-file
LRU/MRU selection over a shared global pool, explicit dirty marking, statistics,
tests and reproducible workload graphs. Slotted records and index-construction
comparisons (parts 2 and 3) are not implemented yet.

## Build and test

Requirements: a C compiler supporting the supplied legacy C code (verified with
GCC 13.3), GNU make, Python 3.10+ and matplotlib for graphs.

From the repository root:

```sh
make -B -C toydb/pflayer tests benchmark
python3 toydb/pflayer/run_tests.py
```

Use `-B` to rebuild the supplied object files rather than trusting the old
precompiled binaries. The original source still emits legacy-C compiler warnings.
The runner isolates test files in temporary directories and exits on any failure.

## Run experiments

Install matplotlib in a virtual environment if needed:

```sh
python3 -m venv /tmp/toydb-venv
/tmp/toydb-venv/bin/pip install matplotlib
/tmp/toydb-venv/bin/python toydb/pflayer/run_experiments.py
```

If matplotlib is already installed, use `python3` directly. In the current
workspace it is installed under `/tmp/sailor-plot-deps`; reproduce using:

```sh
PYTHONPATH=/tmp/sailor-plot-deps MPLCONFIGDIR=/tmp/sailor-mpl python3 toydb/pflayer/run_experiments.py
```

Edit `toydb/pflayer/experiments.json` to change pool size, file size, query count,
write percentages, access patterns, policies, repetitions and seed. No C changes
are needed. Optional arguments: `--config path.json --output directory`.

Results go to `outputs/page-buffering/`: individual runs in `results.csv`, means
in `summary.csv`, the exact configuration in `config.json`, and graphs in
`sequential.png` and `random.png`.

## Counter definitions and API

`PF_ResetStats()` clears pool-wide counters without flushing or clearing caches.
`PF_GetStats()` returns a snapshot; `PF_PrintStats()` prints it.

| Counter | Definition |
| --- | --- |
| logical_reads | Successful buffer fetches, including internal fetches for allocation/disposal or scanning free pages |
| logical_writes | Successful update notifications: `PF_MarkDirty`, `PF_UnfixPage(..., TRUE)`, or internal `PFbufUsed` |
| physical_reads | Successful full-page reads from the OS |
| physical_writes | Successful full-page writes to the OS, including eviction and close flushes |
| buffer_hits / buffer_misses | Successful buffer fetches classified by whether already cached |

Logical I/O is logical_reads + logical_writes; physical I/O is physical_reads +
physical_writes. These count operations, not distinct pages. Header I/O, failed
operations and fresh page allocation itself are excluded. A write query fetches
a page and reports one update, so it contributes one logical read and one logical
write. Reporting the same update through both dirty APIs counts twice; the
benchmark marks once and unfixes with FALSE. Allocation can dirty pages internally
without an update notification; setup is excluded from the experiments.

Physical counts measure ToyDB-to-OS page transfers, not hardware disk activity:
the OS may serve reads from its cache. Successful writeback clears dirty state;
failed writeback retains it. Marking or unfixing dirty does not force a write.

## Experiment method and interpretation

The default configuration runs 60 trials: two access patterns, two policies,
five read/write mixtures and three repetitions. Each starts with a newly created
100-page file and an empty ToyDB pool of 20 frames. The setup is closed before
resetting counters. Each trial performs 10,000 queries and includes the final
close/dirty flush in counts and elapsed time. Each write toggles a byte; each
read fetches a page. Write queries are interspersed deterministically, with exactly
floor(query_count * write_percentage / 100) writes.

Sequential access repeatedly scans the file. Random access uses a fixed 32-bit
LCG with a seed per repetition; the same page trace is reused for both policies
and all write mixtures. Times use a monotonic clock and include page access,
updates and final close, excluding creation and initial open. Repetitions are
averaged for plots; raw timings remain available in the CSV. Overlapping lines
are expected when metrics are identical.

The default sequential experiment gives 10,000 physical reads with LRU and
8,100 with MRU. At 50% writes, it gives 5,000 physical writes with LRU and 4,050
with MRU. MRU retains some pages across repeated scans larger than the pool.
Random results are similar between policies and vary with the seed. These are
synthetic page workloads, not student-record queries; part 1 does not yet need
the record-storage layer. They do not establish that MRU is always faster.

## Design and remaining scope

See `toydb/pflayer/README.md` for page APIs. The requesting file's policy chooses
a global unfixed victim, even from another file. Local replacement is deferred
until after all three assignment parts, as recorded in
`outputs/design-decisions/toydb-design-decisions.xlsx`.

The supplied AM layer and its tests have not been rebuilt/validated as part of
this PF task. Existing legacy C declarations/warnings are not fully modernized.
