# ToyDB part 3 (Part A: Methods 1 and 2) - index construction: design decisions

These decisions were made while implementing Methods 1 and 2 of the roll-no index. For each one, this file lists the options considered, the choice made and the evidence for it. Method 3 (bulk loading) is Part B and must follow decisions 1-4 so its index stays compatible. Any of these can be revisited before submission. None of them changes the AM index format.

## 1. Which field is roll-no, and the key type

**Question:** student.txt has no header row. Which field is the roll-no, and how is it typed for AM?

| Option | Result |
| --- | --- |
| A. Field 2 as an 8-byte `'c'` key (chosen) | Field 2 values look like `95302001` and `96D07001` (year, department, serial). All 17,813 are exactly 8 characters, and some contain letters, so an `'i'` key would not work. AM compares `'c'` keys with `strncmp`, which is plain byte order. |
| B. Field 1 as an `'i'` key | Field 1 (`949981`, ...) is unique and numeric, but it is a record serial number, not a roll number. |
| C. Field 2 converted to an integer | Fails on `96D07001`-style values. |

**Decision:** A. The key is the 8 bytes of field 2, padded with `'\0'` if shorter (none are). A record whose field 2 is empty or longer than 8 bytes is stored in the Student file but not indexed, and is counted as `records_skipped`. The real data has none.

## 2. Duplicate roll numbers

**Question:** The data has 3 roll-nos that appear twice (`97305818`, `99302001`, `99310001`). What should the index do with them?

| Option | Result |
| --- | --- |
| A. Index every record (chosen) | This is AM's native behaviour: a key appears once per leaf and owns a list of RIDs, and an EQUAL scan returns all of them. No AM change is needed, and the index answers the question "which records have this roll-no". |
| B. Reject duplicates / keep the first | Loses records, and would need code that AM does not have. |

**Decision:** A. 17,813 entries for 17,810 distinct keys. Validation checks that every key returns exactly as many RIDs as the dataset has records with that roll-no. **Limitation (existing AM):** a single key's RID list must fit in one leaf, which allows about 670 RIDs per key. Leaf splits divide by key count, so a key's list is never split across leaves.

## 3. Record identifier

**Decision:** Reuse part 2's slotted-page RID, `page * 65536 + slot`. It is a non-negative `int`, which is exactly AM's `recId` type. The Student file for both methods is the part-2 slotted-page file (`SP_*` API), and no record layer was rewritten.

## 4. How the index is opened

AM has no "open index" call. An index is the PF file `<studentFile>.<indexNo>`, created by `AM_CreateIndex`, and it is opened with `PF_OpenFile`. The roll-no index always uses `indexNo = 0`.

## 5. What "construction" measures for each method

| Method | Construction (measured) | Setup (reported separately) |
| --- | --- | --- |
| 1 | `AM_CreateIndex` + sequential `SP_Scan` + one `AM_InsertEntry` per record + closing both files (which flushes dirty pages) | Writing the Student file from student.txt, and `SP_OpenFile` (its validation pass reads every page once) |
| 2 | Create both files, then for each record `SP_InsertRec` followed by `AM_InsertEntry`, then close both files | none |

Parsing student.txt into memory is excluded for both methods. The comparison table reports construction, setup, and "setup + construction" (the end-to-end cost), so the methods can be compared both ways.

## 6. Query benchmark procedure

| Choice | Reason |
| --- | --- |
| Point queries only: AM EQUAL index scans that return every RID of the key | The assignment asks for query time and pages accessed for an index on roll-no. Range scans can be added later as a second query file. |
| One query file, generated once from the dataset with a fixed seed (2,000 existing + 500 absent roll-nos, shuffled) | Gives the same queries in the same order for every method, including Method 3. |
| Absent keys are real roll-nos with the last digit replaced by a letter | They land between real keys, so a miss still descends to a leaf. |
| Index file opened before measuring and closed after | PF_CloseFile releases the file's buffers, so every repetition starts cold. Open and close are not timed. |
| Only the index is measured; RIDs are not dereferenced | This isolates the index structure. The Student file layout is shared by methods 1-2 but not necessarily by Method 3. |
| 5 query repetitions per run (median), 5 independent runs (median) | Times are in milliseconds, so medians smooth out noise. Page counts are deterministic, and repetitions are checked to agree exactly. |

**Pages accessed** = PF `logical_reads`, i.e. requests made to the buffer pool. **Disk reads/writes** = PF `physical_reads` / `physical_writes`. Both are deltas between snapshots, so construction and query numbers never mix.

## 7. Bugs found in the supplied AM layer

| Bug | Effect | Handling |
| --- | --- | --- |
| `AM_OpenIndexScan` calls `AM_Search`, which pushes the search path onto a 50-entry stack that only insert/delete empty | Once the root is an internal node, the 51st scan writes past `AM_Stack` (AddressSanitizer: global-buffer-overflow) | Fixed in `amscan.c` (FIX 1): empty the stack after the search |
| `AM_OpenIndexScan`: for an absent key that sorts after the last key of a leaf, the next leaf's header overwrites `header` before `header->nextLeafPage` is used | Wrong page unfixed: `PFE_PAGEUNFIXED` becomes `AME_PF`, the next leaf stays fixed, and `pageNum` is wrong for `>=` scans | Fixed (FIX 2): save the next leaf's page number first |
| Same pattern in `AM_FindNextEntry` for `NOT_EQUAL` scans | Same as above | Fixed (FIX 3) |
| `toydb/amlayer/pf.h` still says `PF_PAGE_SIZE 1020`, while the PF layer uses 4096 | AM would use only the first 1020 bytes of each page | Not edited. The amindex Makefile compiles copies of the AM sources so that `pf.h` resolves to `toydb/pflayer/pf.h`. A test checks that the root's `maxKeys` is 340, the value for 4096-byte pages |

Each fix is a few lines, marked `PART 3 FIX n` in `toydb/amlayer/amscan.c`. AM search semantics are unchanged.

## Observations from the first results

* For this dataset, Methods 1 and 2 build **identical** trees (2 levels, 1 root + 109 leaves, 64.4 % mean leaf fill). Method 1's sequential scan returns records in RID order, which is the order Method 2 inserted them, so AM receives the same key sequence. Query cost is therefore identical (3.81 pages per query). The methods differ only in construction cost and I/O pattern.
* Method 1's construction alone is about 45 % faster than Method 2 (about 9.5 vs 17 ms) and makes about 32 % fewer page requests, because the Student file already exists. End to end (setup + construction) the two cost about the same.
* Method 2 interleaves Student-file and index pages in the buffer pool, so it writes more pages (640 vs 176 with 20 frames) but reads almost none. Method 1 re-reads the whole Student file during its scan.
* A sorted bulk load (Method 3) should produce nearly full leaves and fewer leaf pages than the 64 % fill seen here. That is what Part B should compare against.


## 8. Bulk loading (Part B)

Method 3 uses the same key type, duplicate lists, RIDs and AM format. Sorting is
in-memory over a separate ordinal/key array; the source records are unchanged.
Stable ties preserve input order. Missing/oversized keys are stored last and
skipped in the index. Setup includes sorting and writing the Student file while
collecting actual new RIDs. Construction consumes this sorted stream directly;
first-fit physical record placement need not itself be scan-sorted.

Leaf packing uses byte occupancy, keeping duplicate groups atomic and balancing
the last pair when possible. A group too large for one page fails explicitly.
Internal levels have balanced fanout up to 341 children. Root remains page 0;
when internal, the first leaf is page 2 and the last leaf is page 1 (no orphan
pages). No PF, SP or AM implementation changes are needed for bulk loading.
Compare setup + construction as well as index construction alone; the sorted
stream preparation must not be hidden. Local replacement remains deferred.
