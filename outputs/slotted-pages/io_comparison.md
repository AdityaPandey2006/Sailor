# Part 2: PF I/O cost of the record layers

Buffer pool 20 pages. Each phase starts with reset counters; load, delete and re-insert end by closing the file so every dirty page is written inside the phase. Delete/re-insert use 20% of the records. Counts are identical across repetitions; times are means of 3 runs.

"cached" first fit keeps each page's free space in memory and fetches only the chosen page; "scan" first fit fetches pages from page 0 until one fits. Both place every record on the same page (verified by the tests).

## Load (insert all records)

| Layout | Search | Policy | Logical reads | Logical writes | Physical reads | Physical writes | Buffer hits | Time (ms) |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Slotted | cached | LRU | 17,360 | 18,266 | 11 | 464 | 17,349 | 9.9 |
| Slotted | cached | MRU | 17,360 | 18,266 | 50 | 503 | 17,310 | 10.6 |
| Slotted | scan | LRU | 4,011,349 | 18,266 | 4,003,039 | 17,038 | 8,310 | 4540.5 |
| Slotted | scan | MRU | 4,011,349 | 18,266 | 3,677,408 | 14,704 | 333,941 | 3970.0 |
| Fixed L=108 | cached | LRU | 17,331 | 18,295 | 0 | 482 | 17,331 | 8.5 |
| Fixed L=108 | cached | MRU | 17,331 | 18,295 | 0 | 482 | 17,331 | 8.6 |
| Fixed L=108 | scan | LRU | 4,296,307 | 18,295 | 4,288,537 | 17,093 | 7,770 | 4460.1 |
| Fixed L=108 | scan | MRU | 4,296,307 | 18,295 | 3,961,885 | 14,878 | 334,422 | 4346.6 |

## Sequential scan

| Layout | Search | Policy | Logical reads | Logical writes | Physical reads | Physical writes | Buffer hits | Time (ms) |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Slotted | cached | LRU | 453 | 0 | 453 | 0 | 0 | 2.4 |
| Slotted | cached | MRU | 453 | 0 | 433 | 0 | 20 | 2.4 |
| Slotted | scan | LRU | 453 | 0 | 453 | 0 | 0 | 2.4 |
| Slotted | scan | MRU | 453 | 0 | 433 | 0 | 20 | 2.2 |
| Fixed L=108 | cached | LRU | 482 | 0 | 482 | 0 | 0 | 2.3 |
| Fixed L=108 | cached | MRU | 482 | 0 | 462 | 0 | 20 | 2.4 |
| Fixed L=108 | scan | LRU | 482 | 0 | 482 | 0 | 0 | 2.2 |
| Fixed L=108 | scan | MRU | 482 | 0 | 462 | 0 | 20 | 2.2 |

## Delete 20%

| Layout | Search | Policy | Logical reads | Logical writes | Physical reads | Physical writes | Buffer hits | Time (ms) |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Slotted | cached | LRU | 3,562 | 3,562 | 3,399 | 3,399 | 163 | 8.9 |
| Slotted | cached | MRU | 3,562 | 3,562 | 3,400 | 3,419 | 162 | 9.0 |
| Slotted | scan | LRU | 3,562 | 3,562 | 3,399 | 3,399 | 163 | 8.3 |
| Slotted | scan | MRU | 3,562 | 3,562 | 3,400 | 3,419 | 162 | 7.6 |
| Fixed L=108 | cached | LRU | 3,562 | 3,562 | 3,404 | 3,404 | 158 | 7.4 |
| Fixed L=108 | cached | MRU | 3,562 | 3,562 | 3,425 | 3,444 | 137 | 7.4 |
| Fixed L=108 | scan | LRU | 3,562 | 3,562 | 3,404 | 3,404 | 158 | 7.4 |
| Fixed L=108 | scan | MRU | 3,562 | 3,562 | 3,425 | 3,444 | 137 | 7.4 |

## Re-insert deleted records

| Layout | Search | Policy | Logical reads | Logical writes | Physical reads | Physical writes | Buffer hits | Time (ms) |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Slotted | cached | LRU | 3,562 | 3,562 | 454 | 454 | 3,108 | 3.0 |
| Slotted | cached | MRU | 3,562 | 3,562 | 510 | 528 | 3,052 | 3.8 |
| Slotted | scan | LRU | 800,630 | 3,562 | 798,728 | 3,409 | 1,902 | 886.3 |
| Slotted | scan | MRU | 800,630 | 3,562 | 740,771 | 3,029 | 59,859 | 803.4 |
| Fixed L=108 | cached | LRU | 3,562 | 3,562 | 482 | 482 | 3,080 | 2.5 |
| Fixed L=108 | cached | MRU | 3,562 | 3,562 | 462 | 482 | 3,100 | 2.5 |
| Fixed L=108 | scan | LRU | 858,506 | 3,562 | 856,885 | 3,427 | 1,621 | 880.4 |
| Fixed L=108 | scan | MRU | 858,506 | 3,562 | 798,541 | 3,057 | 59,965 | 848.5 |
