# Index construction on roll-no: preliminary results

Generated 2026-11-02 12:04 UTC by `toydb/amindex/run_experiments.py`. Every value is the median of 5 independent runs; query time is itself the median of 5 cold repetitions per run.

Dataset `data/student.txt`; 2500 point queries (2000 existing roll-nos, 500 absent), seed 42, same order for every method. Replacement policy LRU.

*Pages accessed* = PF logical reads (page requests to the buffer pool). *Disk reads/writes* = PF physical I/O. Construction and query numbers come from separate counter snapshots.

## Buffer pool: 20 frames

| Metric | Method 1 (existing) | Method 2 (incremental) |
|---|---|---|
| Records indexed | 17813 | 17813 |
| Construction time | 9.56 ms | 17.18 ms |
| Construction pages accessed | 35931 | 52838 |
| Construction disk reads / writes | 519 / 176 | 77 / 640 |
| Setup outside construction (time) | 8.01 ms | 0.00 ms |
| Setup + construction time | 17.58 ms | 17.18 ms |
| Query completion time | 3.23 ms | 3.09 ms |
| Query pages accessed | 9522 | 9522 |
| Query disk reads | 2060 | 2060 |
| Pages accessed per query | 3.81 | 3.81 |
| RIDs returned | 2002 | 2002 |
| Tree height / internal / leaf pages | 2 / 1 / 109 | 2 / 1 / 109 |
| Mean leaf fill | 64.4% | 64.4% |
| Validated (all runs) | yes | yes |

## Buffer pool: 50 frames

| Metric | Method 1 (existing) | Method 2 (incremental) |
|---|---|---|
| Records indexed | 17813 | 17813 |
| Construction time | 10.39 ms | 15.59 ms |
| Construction pages accessed | 35931 | 52838 |
| Construction disk reads / writes | 482 / 139 | 39 / 602 |
| Setup outside construction (time) | 8.01 ms | 0.00 ms |
| Setup + construction time | 18.40 ms | 15.59 ms |
| Query completion time | 2.44 ms | 2.45 ms |
| Query pages accessed | 9522 | 9522 |
| Query disk reads | 1375 | 1375 |
| Pages accessed per query | 3.81 | 3.81 |
| RIDs returned | 2002 | 2002 |
| Tree height / internal / leaf pages | 2 / 1 / 109 | 2 / 1 / 109 |
| Mean leaf fill | 64.4% | 64.4% |
| Validated (all runs) | yes | yes |

## Buffer pool: 200 frames

| Metric | Method 1 (existing) | Method 2 (incremental) |
|---|---|---|
| Records indexed | 17813 | 17813 |
| Construction time | 9.41 ms | 16.32 ms |
| Construction pages accessed | 35931 | 52838 |
| Construction disk reads / writes | 463 / 120 | 15 / 578 |
| Setup outside construction (time) | 7.92 ms | 0.00 ms |
| Setup + construction time | 17.33 ms | 16.32 ms |
| Query completion time | 1.25 ms | 1.29 ms |
| Query pages accessed | 9522 | 9522 |
| Query disk reads | 110 | 110 |
| Pages accessed per query | 3.81 | 3.81 |
| RIDs returned | 2002 | 2002 |
| Tree height / internal / leaf pages | 2 / 1 / 109 | 2 / 1 / 109 |
| Mean leaf fill | 64.4% | 64.4% |
| Validated (all runs) | yes | yes |

Method 1 construction covers creating the index, scanning the existing Student file and inserting every entry, and closing both files. Writing the Student file beforehand and opening it are reported as setup. Method 2 construction covers building the Student file and the index together, so it has no separate setup. Compare "Setup + construction" for the end-to-end cost.
