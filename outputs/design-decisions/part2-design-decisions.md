# ToyDB part 2 - slotted pages: design decisions

Six decisions were open in the assignment text. For each one this lists the options considered, what each would lead to, the agreed choice and the evidence from tests and experiments. Implementation notes at the end record smaller choices made while coding.

Part 1's decision (global victim selection with a per-file LRU/MRU policy) is unchanged and is recorded on the first sheet of `toydb-design-decisions.xlsx`; part 2 is on its second sheet.

## 1. Record representation

**Question:** How should one student record be stored inside a page?

| Option | Result |
| --- | --- |
| A. Raw text line (chosen) | Store each student.txt line unchanged as a variable-length byte string, semicolons included. Simple; every record can be verified byte-for-byte against the input file. Individual fields (e.g. the roll number for part 3) are obtained by splitting on ";". |
| B. Serialized fields | Parse the 16 fields and store each with a length prefix. Typed field access, but more code, extra bytes per record, and the stored bytes no longer equal the input, so the space comparison would not measure the same data. |

**Decision:** A - raw text. The loader skips the title line and blank lines and strips "\r\n".

**Evidence:** testload reads all 17,813 records back through their RIDs and by scan and compares bytes; stored record bytes (1,759,011) equal the input bytes.

## 2. Record identifiers and slot reuse

**Question:** What identifies a record, and may a deleted slot be reused?

| Option | Result |
| --- | --- |
| A. Reuse deleted slots (chosen) | A deleted slot is marked empty and taken by the next insert into that page. The slot directory does not grow without bound. Risk: an index entry still holding the old RID would point at the new record. |
| B. Never reuse slot numbers | Deleted slots stay as permanent tombstones. A stale RID can always be detected, but the directory only grows and wastes 4 bytes per deleted record. |

**Decision:** A. RID = page x 65536 + slot, packed into one C int because AM_InsertEntry takes "int recId". Pages are limited to 0..32767, so the largest RID is exactly INT_MAX and every valid RID is non-negative (negative values stay free for error codes). The lowest empty slot is reused; empty slots at the end of the directory are trimmed, which never renumbers a live slot. Part 3 rule: delete a student's index entry before (or together with) the record, so a slot is never reused while an index still points to it.

**Evidence:** testslotted: RID boundary values (0, 65538, INT_MAX, out-of-range rejects), slot 10 reused after deleting it, trailing slot trimmed (40 -> 39 slots), RIDs unchanged by compaction.

## 3. Fixed-length baseline and oversized records

**Question:** In static (fixed-length) storage every record gets L bytes. What happens to a record longer than L?

| Option | Result |
| --- | --- |
| A. Truncate | Every record is stored, but data is silently lost and utilization looks better than it really is. |
| B. Reject | No corruption; the stored-record count drops and must be reported. |
| C. Only test L >= longest record (chosen, with B) | No conflict for those L values; adding one L below the maximum (with rejection) shows the real weakness of static storage. |

**Decision:** C with rejection reporting. L in {100, 108, 128, 160, 200, 256}; the longest record is 108 bytes. Records longer than L return RECE_RECSIZE and are counted per L. Shorter records are padded with "\0" (see implementation note I2).

**Evidence:** L = 100 rejects 5,749 of 17,813 records; every L >= 108 stores all records (outputs/slotted-pages/space_utilization.md, Table 1).

## 4. Definition of space utilization

**Question:** Which denominator should utilization use?

| Option | Result |
| --- | --- |
| A. Total allocated page space (chosen) | record bytes / (pages x 4096). Page headers, slot directory or bitmap, padding and free space all count as non-record space. Fair: each layout pays for its own overhead. |
| B. Space allocated to records | Excludes per-page overhead, which hides the cost of the slot directory and makes the comparison unfair. |

**Decision:** A, identical for both layouts. Every byte of every page is classified as record data, metadata, padding or free; the four always add up to pages x 4096.

**Evidence:** The sum invariant is checked by testslotted, testfixed and testload after every verification step.

## 5. Choosing the page for an insert

**Question:** Which page receives a new record?

| Option | Result |
| --- | --- |
| A. Append to the last page | One page fetch per insert, but space freed by deletes is never reused. |
| B. First fit (chosen) | The lowest-numbered page with enough contiguous free space (or an empty slot, for the fixed layout); a new page only if none fits. Reuses freed space. |
| C. Free-space map stored on disk | B's placement at A's cost, but needs its own persistent structure. |

**Decision:** B, for the initial load and for later inserts. The search is implemented in two interchangeable ways (see I1): "cached" (default) keeps each page's free space in memory and fetches only the chosen page; "scan" fetches pages 0, 1, 2, ... until one fits. Both pick the same page.

**Evidence:** Tests run 4,000 random inserts/deletes through both modes and require identical RIDs. Loading the student file: cached = 17,360 logical / 11 physical reads (about 10 ms); scan = 4.0 million logical and physical reads (about 4.5 s) with LRU, 3.7 million physical with MRU (outputs/slotted-pages/io_comparison.md).

## 6. When to compact a page

**Question:** After a delete, when are the remaining records moved together?

| Option | Result |
| --- | --- |
| A. Compact on every delete (chosen) | Free space is always one contiguous region, so an insert only compares sizes. Each delete moves the records stored below the deleted one. |
| B. Compact lazily, when an insert needs the space | Cheaper deletes, but fragmented free space must be tracked and inserts become less predictable. |

**Decision:** A. On delete: shift the records below the deleted one up by its length, add that length to the offsets of those slots, mark the slot empty, update the header, mark the page dirty. Slot numbers never change, so RIDs stay valid.

**Evidence:** testslotted checks the freed 100 bytes become one region (freeEnd - freeStart = 100) and that all other records on the page read back unchanged; every fetched page is validated so the record area exactly fills [freeEnd, 4096).

## Implementation notes

| Note | Detail |
| --- | --- |
| I1. In-memory free-space cache for first fit | Rebuilt by one scan of the file when it is opened and kept up to date by every insert and delete; it is never written to disk, so it cannot become inconsistent with the pages. It changes how the first-fit page is found, not which page is chosen. The literal scan is kept as REC_SEARCH_SCAN for comparison. |
| I2. Fixed layout pads with "\0" | A record's length is the position of its first "\0" (or L if there is none), so the layout stores no per-slot length - it is purely static. Records containing "\0" are rejected; text lines never contain it. |
| I3. Scans hold no page fixed | SP_ScanNext/FX_ScanNext copy the current page into the scan handle and unfix it at once: one logical read per page, and a 1-frame buffer pool is enough. Changes to the page being scanned are visible from the next page. |
| I4. Separate layers | slotted.c and fixedrec.c are independent implementations. They share only recfile.h (RID packing, error codes, search modes and the space-statistics structure), so both are measured on identical terms. |
| I5. No PF source changes | Both layers use only the PF interface (PF_AllocPage, PF_GetThisPage, PF_GetNextPage, PF_MarkDirty, PF_UnfixPage, PF_OpenFileWithPolicy, ...). GCC 14+ rejects the K&R PF sources by default, so the Makefile compiles them with "-std=gnu17 -fpermissive" instead of editing them. |
| I6. Pages are never released | Deleting records frees space inside pages but never disposes a page, so utilization falls in proportion to the deletions and re-inserted records refill the same pages (the page count never changed in the experiments). |
| I7. Measured phases include the close | In spbench, load, delete and re-insert each end by closing the file, so every dirty page is written inside the phase being measured. |
