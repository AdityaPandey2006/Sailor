# Part 2: space utilization - slotted pages vs fixed-length records

Input: 17813 student records, 1,759,011 bytes, longest 108 bytes. Page size 4096 bytes; buffer pool 20 pages; first-fit insertion.

Utilization = record bytes / (pages x 4096). Every byte of every page is counted as record data, metadata (page header + slot directory or bitmap), padding (unused part of fixed slots and the page tail) or free space; the four add up to pages x 4096.

## Table 1 - after loading every record

| Layout | Max record length L | Records stored | Rejected (> L) | Pages | Records / page | Record bytes | Metadata bytes | Padding bytes | Free bytes | Utilization |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Slotted (variable) | - | 17,813 | 0 | 453 | 39.3 | 1,759,011 | 74,876 | 0 | 21,601 | 94.80% |
| Fixed | 100 | 12,064 | 5,749 | 302 | 39.9 | 1,173,650 | 3,926 | 57,816 | 1,600 | 94.88% |
| Fixed | 108 | 17,813 | 0 | 482 | 37.0 | 1,759,011 | 6,266 | 206,727 | 2,268 | 89.10% |
| Fixed | 128 | 17,813 | 0 | 575 | 31.0 | 1,759,011 | 6,900 | 587,753 | 1,536 | 74.69% |
| Fixed | 160 | 17,813 | 0 | 713 | 25.0 | 1,759,011 | 8,556 | 1,150,961 | 1,920 | 60.23% |
| Fixed | 200 | 17,813 | 0 | 891 | 20.0 | 1,759,011 | 9,801 | 1,879,324 | 1,400 | 48.20% |
| Fixed | 256 | 17,813 | 0 | 1,188 | 15.0 | 1,759,011 | 11,880 | 3,093,365 | 1,792 | 36.15% |

## Table 2 - after deleting records and inserting them again

The same randomly chosen students (seeded shuffle) are deleted from every layout, then inserted again with first fit. The page count never changes here: deletion frees space inside pages but no page is released.

| Layout | After load | After 20% deleted | After re-insert (20%) | After 50% deleted | After re-insert (50%) |
| --- | --- | --- | --- | --- | --- |
| Slotted | 94.80% | 75.84% | 94.80% | 47.40% | 94.80% |
| Fixed L=100 | 94.88% | 75.91% | 94.88% | 47.45% | 94.88% |
| Fixed L=108 | 89.10% | 71.28% | 89.10% | 44.55% | 89.10% |
| Fixed L=128 | 74.69% | 59.75% | 74.69% | 37.34% | 74.69% |
| Fixed L=160 | 60.23% | 48.19% | 60.23% | 30.11% | 60.23% |
| Fixed L=200 | 48.20% | 38.56% | 48.20% | 24.10% | 48.20% |
| Fixed L=256 | 36.15% | 28.92% | 36.15% | 18.07% | 36.15% |
