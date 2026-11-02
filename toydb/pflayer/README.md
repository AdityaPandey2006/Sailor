# PF buffer capacity

Existing callers keep the default capacity of 20 pages. To select a different
capacity, call `PF_InitWithBufferSize(50)` once at startup instead of `PF_Init()`.
Check its return value against `PFE_OK`. The capacity is shared by all open files;
frames are allocated on demand. This does not change the page size or LRU policy.

The count must be positive. Configuration is rejected while files are open or
after any buffer frames have been allocated, even after closing files. Live
resizing is not supported. Choose the size before opening files or requesting pages.

Build with `make -B tests` in this directory. Run each test from a fresh scratch
directory because the supplied tests create files with fixed names:

```sh
pf_dir="$PWD"
test_dir=$(mktemp -d)
cd "$test_dir"
"$pf_dir/testcapacity" 5
```

Use a fresh directory for each invocation. `testcapacity` checks invalid sizes,
capacity exhaustion with pinned pages, eviction after unpinning, and persistence
of dirty data after closing and reopening. It accepts a positive capacity as its
argument (default 2). `testhash` and `testpf` are the original baseline tests.

## Per-file replacement policy selection (step 4)

Use `PF_OpenFileWithPolicy("students", PF_POLICY_LRU)` or
`PF_OpenFileWithPolicy("students", PF_POLICY_MRU)`. Both return a file descriptor
on success or a negative error. `PF_OpenFile` continues to select LRU.
`PF_GetFilePolicy(fd)` returns the selected policy or `PFE_FD` for an invalid
handle. Invalid policies are rejected before opening the disk file.

The selection is stored separately for each open handle; all files share one
buffer pool. When a request needs a frame, its file's policy chooses the oldest
(LRU) or newest (MRU) unfixed page across the pool, including other files' pages.
Fixed pages are skipped; if all frames are fixed, the request returns PFE_NOBUF.
Dirty victims are saved using their owning file descriptor before reuse.

Recency follows the existing implementation: new frames enter at the head;
unfixing a page or calling PFbufUsed moves it to the head. A fixed page remains
ineligible regardless of its list position. Local replacement is deferred until
after all three assignment parts are complete.

## Explicit dirty marking (step 6)

After fetching and modifying a fixed page, call `PF_MarkDirty(fd, pageNum)`.
Check for `PFE_OK`. This keeps the page fixed, marks it dirty and most recently
used, and does not immediately write it. Later `PF_UnfixPage(fd, pageNum, FALSE)`
preserves the dirty flag. The existing `PF_UnfixPage(..., TRUE)` remains supported.
Dirty pages are saved before eviction and on file close; fixed pages must be
unfixed before closing. The flag is cleared only after successful writeback.

Invalid handles/page numbers, uncached pages, and cached but unfixed pages return
`PFE_FD`, `PFE_INVALIDPAGE`, `PFE_PAGENOTINBUF`, and `PFE_PAGEUNFIXED`, respectively.
`testdirty lru` and `testdirty mru` verify these cases, retained pinning, repeated
marking, and persistence through eviction and closing. Run each in a fresh directory.

`testreplacement lru` and `testreplacement mru` exercise exact victim choices,
buffer-hit recency, mixed file policies, pinned-page protection and dirty
writeback. Run each invocation in a fresh scratch directory.
