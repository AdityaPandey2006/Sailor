# TASK: DBMS Assignment 1 – Part 3 (Part B)

## 1. Objective

Implement Method 3 (Bulk Loading) for constructing a B+ tree index on the Student file using `roll-no` as the search key.

After implementing bulk loading, perform a complete experimental comparison of all three indexing methods:

1. Method 1: Build an index on an existing Student file by sequentially scanning records and inserting entries individually.
2. Method 2: Start with an empty Student file and maintain the B+ tree index as each student record is inserted.
3. Method 3: Sort Student records by roll number and construct the B+ tree using a bulk-loading algorithm.

The final goal is to demonstrate the performance differences between the three approaches.

Part A (Methods 1 and 2) has been implemented separately. Reuse its implementation and measurement infrastructure wherever possible.

---

# 2. Inspect and understand the existing implementation

Before writing any code:

1. Inspect the existing ToyDB codebase.
2. Understand the existing PF and AM layers.
3. Inspect the B+ tree implementation and its page structure.
4. Understand how leaf nodes and internal nodes are represented.
5. Identify how keys and RIDs are stored.
6. Understand how the root page and tree metadata are maintained.
7. Inspect the implementation of normal AM insertion and searching.
8. Examine the implementation delivered by Part A.

Pay particular attention to the following:

- Existing index file format.
- Existing B+ tree node structure.
- Key representation and data types.
- Leaf node capacity.
- Internal node capacity.
- Parent-child relationships.
- Leaf node sibling pointers.
- Root node management.
- Page allocation and writing through PF.

Do not assume a different B+ tree format. Bulk loading must produce an index compatible with the existing AM implementation.

---

# 3. Task 1 – Sort the Student records

Before bulk loading, the Student records must be sorted by `roll-no`.

Implement a sorting mechanism that:

1. Reads all Student records from the supplied Student dataset or the existing Student file.
2. Extracts the `roll-no` field from each record.
3. Sorts the records in ascending order of roll number.
4. Preserves all other fields of every student record.
5. Produces a sorted Student file or an equivalent sorted input representation for bulk loading.

Example:

Original records:

| Student | Roll No |
|---|---:|
| A | 105 |
| B | 102 |
| C | 109 |
| D | 101 |
| E | 106 |

Sorted records:

| Student | Roll No |
|---|---:|
| D | 101 |
| B | 102 |
| A | 105 |
| E | 106 |
| C | 109 |

Important requirements:

- Sorting must not modify or destroy the original dataset.
- Preserve the association between each record and its corresponding RID.
- Ensure that RIDs used in the final index point to the correct records in the sorted Student file.
- Handle duplicate or invalid roll numbers consistently with the existing AM implementation.
- Do not assume that the original Student file is already sorted.

---

# 4. Task 2 – Implement Method 3: B+ Tree Bulk Loading

## Objective

Construct a B+ tree directly from the sorted Student records instead of inserting each key individually using normal AM insertion.

This is the primary implementation task.

## Why bulk loading?

Methods 1 and 2 repeatedly invoke normal B+ tree insertion.

Normal insertion involves:

- Searching for the appropriate leaf.
- Locating the insertion position.
- Updating the leaf.
- Handling node overflow.
- Splitting nodes when necessary.
- Updating parent nodes.

Bulk loading can reduce this overhead because the keys are already sorted.

## Required algorithm

Implement a bottom-up B+ tree bulk-loading algorithm that follows the existing ToyDB B+ tree page format.

### Step A: Prepare sorted entries

For every Student record, obtain:

- `roll-no`
- Corresponding RID

Generate sorted index entries:

```text
(roll-no, RID)
```

These entries must be in ascending roll-number order.

### Step B: Construct leaf nodes

1. Allocate leaf pages through the existing PF layer.
2. Insert sorted key-RID entries sequentially into the leaf pages.
3. Fill each leaf according to the existing B+ tree node capacity and the bulk-loading occupancy policy.
4. Maintain the correct leaf-node metadata.
5. Link consecutive leaf nodes through their sibling pointers.
6. Handle the final partially filled leaf correctly.

Conceptual representation:

```text
Sorted Entries
      |
      v
+------------------+
| Leaf Node 1      |
| 101 102 103 104  |
+------------------+
         |
         v
+------------------+
| Leaf Node 2      |
| 105 106 107 108  |
+------------------+
         |
         v
+------------------+
| Leaf Node 3      |
| 109 110 111 112  |
+------------------+
```

The example is conceptual. Actual node capacities must come from the existing implementation.

### Step C: Construct internal nodes

After constructing the leaf level:

1. Collect the appropriate separator keys and child page pointers.
2. Construct the parent internal nodes.
3. Ensure that separator keys follow the existing AM implementation's conventions.
4. Connect each parent to its corresponding child pages.
5. Repeat this process level by level until only one root remains.

Conceptual representation:

```text
                 Root
              [107 | 113]
              /    |    \
             /     |     \
            v      v      v

         Internal Internal Internal
            |        |        |
            v        v        v

          Leaf     Leaf     Leaf
```

### Step D: Finalize the B+ tree

1. Set the root page correctly.
2. Update the tree metadata and height if maintained by ToyDB.
3. Ensure all page allocations and writes are completed.
4. Close the index properly.

The resulting index must be compatible with the existing AM search and scan functions.

Do not implement a separate search mechanism for the bulk-loaded tree.

---

# 5. Task 3 – Validate the Bulk-Loaded Index

Test the generated index thoroughly.

### Required tests

1. Search for the smallest roll number.
2. Search for the largest roll number.
3. Search for a roll number in the middle.
4. Search for random existing roll numbers.
5. Search for a non-existing roll number.
6. Verify that returned RIDs identify the correct Student records.
7. Test searches that traverse different internal nodes.
8. Verify leaf-level sequential scanning.
9. Close and reopen the index and repeat the searches.

### Structural correctness

Verify:

- All leaf entries are sorted.
- All leaf pages are correctly linked.
- Internal separator keys are correct.
- Child pointers reference valid pages.
- The root points to the correct page.
- The B+ tree satisfies the existing implementation's occupancy and structural rules.
- No Student record is accidentally omitted or indexed twice.

Test with a sufficiently large dataset to ensure that the tree has multiple levels.

---

# 6. Task 4 – Integrate All Three Indexing Methods

At this stage, all three indexing methods should be available.

Ensure that they use:

- The same Student dataset.
- The same roll-number field.
- The same B+ tree implementation and index format.
- The same search API.
- The same query set.
- The same measurement infrastructure.

The three methods must be independently executable.

Do not allow one method's generated index to overwrite another method's index.

Use separate output files or directories.

Suggested organization:

```text
Part3/
|
|-- method1/
|   |-- student_file
|   |-- index_file
|
|-- method2/
|   |-- student_file
|   |-- index_file
|
|-- method3/
|   |-- sorted_student_file
|   |-- index_file
|
|-- benchmarks/
|   |-- results.csv
|   |-- comparison_tables
|   |-- graphs/
|
|-- README.md
```

Adapt this structure to the existing repository instead of unnecessarily restructuring it.

---

# 7. Task 5 – Benchmark all three methods

This is a major deliverable.

Run all three methods under identical conditions.

## A. Index construction measurements

Measure the time required to construct each index.

| Metric | Method 1 | Method 2 | Method 3 |
|---|---|---|---|
| Construction time | Measure | Measure | Measure |
| Records indexed | Measure | Measure | Measure |
| Pages accessed | Measure | Measure | Measure |

Also record page reads and writes if supported.

Keep the construction measurements separate from query measurements.

## B. Query performance measurements

Use the same set of roll-number searches for all three indexes.

Measure:

1. Query completion time.
2. Number of pages accessed.

Use a consistent query set containing:

- Existing roll numbers.
- Non-existing roll numbers.
- Small roll numbers.
- Large roll numbers.
- Middle-range roll numbers.
- Randomly selected roll numbers.

Use the same query order and number of queries for each method.

Separate index opening and initialization time from query execution time.

If appropriate, execute multiple repetitions and report averages.

Clearly document the measurement procedure and whether page accesses are logical or physical.

## C. Comparison table

Produce a final comparison table:

| Metric | Method 1 | Method 2 | Method 3 |
|---|---|---|---|
| Index construction time | | | |
| Total records indexed | | | |
| Construction page accesses | | | |
| Query completion time | | | |
| Query page accesses | | | |
| Page reads | | | |
| Page writes | | | |

Populate every value using actual experimental results.

Do not fabricate or estimate measurements.

---

# 8. Task 6 – Generate Graphs

Generate graphs comparing the three indexing methods.

At minimum, include:

### Graph 1: Index construction time

- X-axis: Indexing method.
- Y-axis: Construction time.

### Graph 2: Query completion time

- X-axis: Indexing method.
- Y-axis: Query execution time.

### Graph 3: Number of pages accessed

- X-axis: Indexing method.
- Y-axis: Pages accessed during the measured queries.

Additional graphs for construction page accesses, reads and writes can be included if useful.

Use consistent units and clearly labelled axes.

Save all graphs in the project's results directory.

---

# 9. Task 7 – Interpret the results

Do not just produce graphs and tables.

Explain the observed differences.

Discuss:

1. Why Method 1 requires repeated B+ tree insertions.
2. Why Method 2 performs index maintenance during record insertion.
3. Why Method 3 can construct the tree more efficiently using sorted input.
4. Whether bulk loading actually reduces construction time.
5. Whether the three methods produce similar search performance.
6. Whether their page-access counts differ.
7. What factors influence the observed performance.

An important distinction:

Bulk loading is primarily intended to improve index construction efficiency. It does not automatically guarantee faster searches once all three indexes have the same structure and contents.

Base every conclusion on actual results.

---

# 10. Task 8 – Write the Part 3 README section

Document the complete Part 3 implementation.

Include:

### 1. Objective
Explain the purpose of comparing the three indexing methods.

### 2. Method 1
Explain how the index is constructed on an existing Student file.

### 3. Method 2
Explain incremental index construction during Student record insertion.

### 4. Method 3
Explain the sorting process and bottom-up B+ tree bulk-loading algorithm.

### 5. Compilation and execution
Provide exact commands to:
- Compile the project.
- Run Method 1.
- Run Method 2.
- Run Method 3.
- Execute the benchmark.

### 6. Experimental setup
Mention:
- Dataset used.
- Number of Student records.
- B+ tree configuration.
- Search query set.
- Measurement methodology.
- Number of repetitions, if applicable.

### 7. Results
Include comparison tables and graphs.

### 8. Analysis
Explain the results and identify the most efficient construction approach.

### 9. Limitations
Mention any implementation or measurement limitations.

---

# 11. Task 9 – Final integration and submission

Prepare the complete Part 3 deliverables.

Ensure the submission includes:

- Source code for all three methods.
- Bulk-loading implementation.
- Any required sorting implementation.
- Benchmarking infrastructure.
- Correctness tests.
- Experimental result files.
- Comparison tables.
- Graphs.
- README documentation.
- Necessary build and execution instructions.

Package the complete assignment according to the required naming convention:

`rollno1-rollno2.tar.gz`

Do not include unnecessary generated files, temporary build artifacts or unrelated datasets unless required for execution.

Verify that the archive can be extracted and that the project can be compiled and executed using the README instructions.

---

# 12. Important restrictions

1. Do not implement a separate B+ tree format for bulk loading.
2. Do not replace the existing AM search implementation.
3. Do not unnecessarily rewrite PF or AM functionality.
4. Preserve compatibility with Methods 1 and 2.
5. Do not overwrite original Student data.
6. Keep generated indexes separate.
7. Do not fabricate experimental results.
8. Do not change the measurement methodology between the three methods.
9. Explain all source-code modifications.
10. Ensure that the final implementation works with the existing ToyDB build system.

---

# 13. Final completion checklist

- [ ] Student records successfully sorted by roll-no.
- [ ] Bulk-loading algorithm implemented.
- [ ] Leaf nodes correctly constructed.
- [ ] Internal nodes correctly constructed.
- [ ] Leaf sibling pointers correctly maintained.
- [ ] Root metadata correctly updated.
- [ ] Bulk-loaded index works with normal AM search.
- [ ] Bulk-loaded index works with normal AM scanning.
- [ ] All three methods work independently.
- [ ] All three methods use the same dataset.
- [ ] Identical queries executed on all three indexes.
- [ ] Construction time measured.
- [ ] Query completion time measured.
- [ ] Page accesses measured.
- [ ] Comparison tables generated.
- [ ] Required graphs generated.
- [ ] Results interpreted and documented.
- [ ] Part 3 README completed.
- [ ] Final archive generated and tested.

At completion, provide:
1. A list of all modified and newly created files.
2. A description of the bulk-loading algorithm.
3. The exact compilation and execution commands.
4. Correctness test results.
5. Benchmark results.
6. Generated graphs and their locations.
7. Any unresolved issues or limitations.