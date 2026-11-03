# Index construction on roll-no: three-method comparison

Generated 2026-11-03 02:03 UTC by `toydb/amindex/run_experiments.py`. Every value is the median of 5 independent runs; query time is itself the median of 5 cold repetitions per run.

Dataset `data/student.txt`; 2500 point queries (2000 existing roll-nos, 500 absent), seed 42, same order for every method. Replacement policy LRU.

*Pages accessed* = PF logical reads (page requests to the buffer pool). *Disk reads/writes* = PF physical I/O. Construction and query numbers come from separate counter snapshots.

## Buffer pool: 20 frames

| Metric | Method 1 (existing) | Method 2 (incremental) | Method 3 (bulk) |
|---|---|---|---|
| Records indexed | 17813 | 17813 | 17813 |
| Construction time | 16.96 ms | 33.08 ms | 1.50 ms |
| Construction pages accessed | 35931 | 52838 | 72 |
| Construction disk reads / writes | 519 / 176 | 77 / 640 | 72 / 144 |
| Setup outside construction (time) | 19.49 ms | 0.00 ms | 34.18 ms |
| Setup + construction time | 36.45 ms | 33.08 ms | 35.68 ms |
| Query completion time | 6.10 ms | 6.06 ms | 7.31 ms |
| Query pages accessed | 9522 | 9522 | 9511 |
| Query disk reads | 2060 | 2060 | 1843 |
| Query disk writes | 0 | 0 | 0 |
| Pages accessed per query | 3.81 | 3.81 | 3.80 |
| RIDs returned | 2002 | 2002 | 2002 |
| Tree height / internal / leaf pages | 2 / 1 / 109 | 2 / 1 / 109 | 2 / 1 / 71 |
| Mean leaf fill | 64.4% | 64.4% | 98.6% |
| Validated (all runs) | yes | yes | yes |

## Buffer pool: 50 frames

| Metric | Method 1 (existing) | Method 2 (incremental) | Method 3 (bulk) |
|---|---|---|---|
| Records indexed | 17813 | 17813 | 17813 |
| Construction time | 14.97 ms | 29.97 ms | 1.01 ms |
| Construction pages accessed | 35931 | 52838 | 72 |
| Construction disk reads / writes | 482 / 139 | 39 / 602 | 72 / 144 |
| Setup outside construction (time) | 17.85 ms | 0.00 ms | 22.78 ms |
| Setup + construction time | 32.83 ms | 29.97 ms | 23.79 ms |
| Query completion time | 4.29 ms | 4.67 ms | 4.27 ms |
| Query pages accessed | 9522 | 9522 | 9511 |
| Query disk reads | 1375 | 1375 | 806 |
| Query disk writes | 0 | 0 | 0 |
| Pages accessed per query | 3.81 | 3.81 | 3.80 |
| RIDs returned | 2002 | 2002 | 2002 |
| Tree height / internal / leaf pages | 2 / 1 / 109 | 2 / 1 / 109 | 2 / 1 / 71 |
| Mean leaf fill | 64.4% | 64.4% | 98.6% |
| Validated (all runs) | yes | yes | yes |

## Buffer pool: 200 frames

| Metric | Method 1 (existing) | Method 2 (incremental) | Method 3 (bulk) |
|---|---|---|---|
| Records indexed | 17813 | 17813 | 17813 |
| Construction time | 16.81 ms | 31.95 ms | 0.85 ms |
| Construction pages accessed | 35931 | 52838 | 72 |
| Construction disk reads / writes | 463 / 120 | 15 / 578 | 0 / 72 |
| Setup outside construction (time) | 17.13 ms | 0.00 ms | 23.47 ms |
| Setup + construction time | 33.94 ms | 31.95 ms | 24.32 ms |
| Query completion time | 2.70 ms | 2.69 ms | 2.19 ms |
| Query pages accessed | 9522 | 9522 | 9511 |
| Query disk reads | 110 | 110 | 72 |
| Query disk writes | 0 | 0 | 0 |
| Pages accessed per query | 3.81 | 3.81 | 3.80 |
| RIDs returned | 2002 | 2002 | 2002 |
| Tree height / internal / leaf pages | 2 / 1 / 109 | 2 / 1 / 109 | 2 / 1 / 71 |
| Mean leaf fill | 64.4% | 64.4% | 98.6% |
| Validated (all runs) | yes | yes | yes |

Method 1 construction covers creating the index, scanning the existing Student file and inserting every entry, and closing both files. Writing the Student file beforehand and opening it are reported as setup. Method 2 construction covers building the Student file and the index together, so it has no separate setup. Compare "Setup + construction" for the end-to-end cost. Method 3 setup includes sorting a copy and writing the reordered Student file while collecting key/RID pairs. Construction consumes those sorted pairs and writes the AM tree bottom-up, including the final close. Sorting is therefore not free in the end-to-end comparison.
