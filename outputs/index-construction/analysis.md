# Final three-method results

Measurements: `summary.csv` (medians), `results.csv` / `raw_results.jsonl`
(all 45 runs), and `comparison.md`. Same input, key type, query file/order,
AM format, snapshot-based counters and query timing for all three methods.
Each query run starts cold in ToyDB, not necessarily in the operating system.

## Construction and preprocessing

With 20 frames, construction medians are 16.96 ms (existing-file inserts),
33.08 ms (incremental file/index inserts), and 1.50 ms (bulk loading).
Method 3 eliminates repeated root-to-leaf searches and split propagation by
writing sorted leaves and parent levels directly. Logical construction reads
are 35,931 / 52,838 / 72 respectively.

That construction-only advantage excludes sorting and file preparation.
Method 3 prepares an in-memory sorted key/RID stream while writing its Student
file, so construction need not scan the file again. Setup + construction is
36.45 / 33.08 / 35.68 ms with 20 frames: bulk loading is not the fastest
end-to-end in this measurement. At 50 and 200 frames it is the fastest
end-to-end (23.79 and 24.32 ms). Timing noise and OS caching are substantial
at this scale. Do not present sorting as free or generalize these timings to
large external-memory datasets.

The pool-wide counters count requests/notifications, not distinct pages.
Fresh allocations do not increment logical reads; the bulk writer's subsequent
fetches do. Allocation initializes blank pages before populating them, so with
a small pool some blank pages are written and read again: 72 reads / 144 writes
at 20 frames, compared with 0 / 72 at 200 frames. These transfers are included.

## Tree shape and query performance

Methods 1 and 2 insert in the same order and produce identical trees:
109 leaf pages, 64.4% mean leaf utilization. Method 3 has 71 leaves at 98.6%
utilization, about 35% fewer leaves. All three have a two-level tree.

For the same 2,500 searches, logical page requests are 9,522 / 9,522 / 9,511.
The similar height gives similar logical search work; the small difference
comes from absent-key leaf transitions. At 20 frames physical reads are
2,060 / 2,060 / 1,843, and at 50 frames 1,375 / 1,375 / 806: the denser tree
fits better in the buffer pool. At 200 frames each index fits completely,
requiring 110 / 110 / 72 initial physical reads. Query writes are zero.

Query-time medians at 20 frames are 6.10 / 6.06 / 7.31 ms, so Method 3 is
slower in that timing despite fewer physical reads. At 50 frames the times
are 4.29 / 4.67 / 4.27 ms, and at 200 frames 2.70 / 2.69 / 2.19 ms.
Bulk loading mainly improves construction and occupancy; it does not promise
faster searches under every measured condition.

All methods return 2,000 hits, 500 misses and 2,002 RIDs. The duplicate
roll numbers account for the extra RIDs. Method 3 uses new RIDs from the
reordered Student file, so cross-method RID checksums are not compared;
independent validation verifies every indexed RID against its record.

## Validation and limitations

Five correctness groups pass, including 150,000-record synthetic data,
three-level trees, 3-frame pools, MRU edge tests, leaf boundaries, normal AM
mutations after loading, duplicate overflow, scans and reopen persistence.
Byte-preservation tests compare every synthetic record with its original bytes.
AddressSanitizer/UBSan results are recorded separately; LeakSanitizer cannot run
under this container's ptrace environment and is disabled.

Sorting and entry/group/node arrays use O(n) RAM, suitable for the supplied
dataset; this is not an external merge sort. One duplicate group must fit in
one leaf (up to 677 RIDs for an otherwise empty leaf); larger groups fail
explicitly. The existing AM algorithm has additional limits for heavily
skewed duplicate lists during later splits. Leaf packing is byte-based;
the last pair is balanced when possible, with atomic duplicate groups.
Internal levels use balanced fanout. No new AM format or search engine is used.
Only point queries are benchmarked; range/full scans are correctness checks.
Legacy PF/AM compiler warnings remain. Local replacement remains deferred.

## Files changed for Part B

New source: `toydb/amindex/ixbulk.c`, `testbulk.c`; root
`package_submission.py`. Modified: `ixbuild.c`, `ixindex.h`, `Makefile`,
`testindex.c`, `run_tests.py`, `run_experiments.py`, `experiments.json`,
the amindex README and root README. Updated outputs: comparison/config,
raw and summarized results and test logs; four new PNG graphs, this analysis,
and sanitizer results. The PF, SP and AM source implementations were not
changed for Part B. Old part-3(1) summaries remain historical Part A reports.
