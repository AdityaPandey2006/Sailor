/* ixbench.c: the common index benchmark (one run = one JSON line).
 *
 *   ixbench queries --data student.txt --hits N --misses M --seed S --out Q
 *       Writes a deterministic query file (one roll-no per line).
 *
 *   ixbench run --method K --data student.txt --queries Q --workdir DIR
 *               [--bufsize B] [--policy lru|mru] [--reps R] [--validate] [--keep]
 *       1. builds the Student file and its index with method K (IX_Methods[])
 *       2. checks the tree shape (IX_InspectTree)        - not measured
 *       3. optionally validates every key (IX_Validate)    - not measured
 *       4. runs the query file R times; each repetition reopens the index,
 *          so it starts with none of its pages in the buffer pool
 *       5. prints one JSON object and removes the files (unless --keep)
 *
 * Construction and query measurements are separate snapshots of the PF
 * counters and the monotonic clock (see ixindex.h).  The procedure does not
 * depend on the method, so a new method only needs an IX_Methods[] entry.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "amwrap.h"
#include "ixindex.h"

#define MAX_REPS 100

static void usage(void)
{
    fprintf(stderr,
        "usage: ixbench queries --data FILE --hits N --misses M --seed S --out FILE\n"
        "       ixbench run --method K --data FILE --queries FILE --workdir DIR\n"
        "                   [--bufsize B] [--policy lru|mru] [--reps R]\n"
        "                   [--validate] [--keep]\n"
        "       ixbench methods\n");
    exit(2);
}

static void die(const char *what, int err)
{
    fprintf(stderr, "ixbench: %s: %s (%d)\n", what, IX_ErrorString(err), err);
    exit(1);
}

static const char *arg(int argc, char **argv, const char *name, const char *dflt)
{
    int i;
    for (i = 2; i < argc; i++)
        if (strcmp(argv[i], name) == 0) {
            if (i + 1 >= argc)
                usage();
            return argv[i + 1];
        }
    return dflt;
}

static int flag(int argc, char **argv, const char *name)
{
    int i;
    for (i = 2; i < argc; i++)
        if (strcmp(argv[i], name) == 0)
            return 1;
    return 0;
}

static void json_measure(const char *name, const IX_Measure *m)
{
    printf("\"%s\": {\"seconds\": %.6f, \"logical_reads\": %llu, "
           "\"logical_writes\": %llu, \"physical_reads\": %llu, "
           "\"physical_writes\": %llu, \"buffer_hits\": %llu, "
           "\"buffer_misses\": %llu}", name, m->seconds,
           m->pf.logical_reads, m->pf.logical_writes, m->pf.physical_reads,
           m->pf.physical_writes, m->pf.buffer_hits, m->pf.buffer_misses);
}

static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static int cmd_queries(int argc, char **argv)
{
    const char *data = arg(argc, argv, "--data", NULL), *out = arg(argc, argv, "--out", NULL);
    int hits = atoi(arg(argc, argv, "--hits", "1000"));
    int misses = atoi(arg(argc, argv, "--misses", "0"));
    unsigned seed = (unsigned)strtoul(arg(argc, argv, "--seed", "1"), NULL, 10);
    StudentData d;
    IX_QuerySet q;
    int err;

    if (!data || !out)
        usage();
    if (SD_Load(data, 1, &d) != 0)
        exit(1);
    if ((err = IX_MakeQueries(&d, hits, misses, seed, &q)) != IXE_OK)
        die("make queries", err);
    if ((err = IX_SaveQueries(out, &q)) != IXE_OK)
        die(out, err);
    printf("{\"queries\": %d, \"hits\": %d, \"misses\": %d, \"seed\": %u}\n",
           q.n, hits, misses, seed);
    IX_FreeQueries(&q);
    SD_Free(&d);
    return 0;
}

static int cmd_run(int argc, char **argv)
{
    const char *data = arg(argc, argv, "--data", NULL);
    const char *qfile = arg(argc, argv, "--queries", NULL);
    const char *workdir = arg(argc, argv, "--workdir", ".");
    const char *pol = arg(argc, argv, "--policy", "lru");
    int methodId = atoi(arg(argc, argv, "--method", "0"));
    int bufsize = atoi(arg(argc, argv, "--bufsize", "20"));
    int reps = atoi(arg(argc, argv, "--reps", "5"));
    int validate = flag(argc, argv, "--validate"), keep = flag(argc, argv, "--keep");
    const IX_Method *method = IX_FindMethod(methodId);
    char studentFile[1024], indexFile[1100];
    StudentData d;
    IX_QuerySet q;
    IX_BuildSpec spec;
    IX_BuildResult br;
    IX_TreeInfo tree;
    IX_ValidateResult v;
    IX_QueryResult qr[MAX_REPS];
    double times[MAX_REPS], sorted[MAX_REPS];
    struct stat st;
    int err, i, policy;

    if (!data || !qfile || !method || reps < 1 || reps > MAX_REPS)
        usage();
    policy = strcmp(pol, "mru") == 0 ? PF_POLICY_MRU : PF_POLICY_LRU;
    if ((err = PF_InitWithBufferSize(bufsize)) != PFE_OK)
        die("PF_InitWithBufferSize", err);
    if (SD_Load(data, 1, &d) != 0)
        exit(1);
    if ((err = IX_LoadQueries(qfile, &q)) != IXE_OK)
        die(qfile, err);

    snprintf(studentFile, sizeof studentFile, "%s/m%d_students", workdir, methodId);
    IX_IndexFileName(studentFile, IX_INDEX_NO, indexFile, sizeof indexFile);
    IX_RemoveFiles(studentFile, IX_INDEX_NO);

    spec.data = &d;
    spec.studentFile = studentFile;
    spec.indexNo = IX_INDEX_NO;
    spec.policy = policy;
    if ((err = method->build(&spec, &br)) != IXE_OK)
        die("build", err);

    if ((err = IX_InspectTree(studentFile, IX_INDEX_NO, &tree)) != IXE_OK)
        die("inspect", err);
    if (tree.entries != br.recordsIndexed)
        die("tree entry count differs from records indexed", IXE_INVALID);
    memset(&v, 0, sizeof v);
    if (validate && (err = IX_Validate(studentFile, IX_INDEX_NO, &d, &v)) != IXE_OK)
        die("validate", err);

    for (i = 0; i < reps; i++) {
        if ((err = IX_RunQueries(studentFile, IX_INDEX_NO, &q, &qr[i])) != IXE_OK)
            die("queries", err);
        times[i] = sorted[i] = qr[i].m.seconds;
        if (qr[i].ridSum != qr[0].ridSum || qr[i].rids != qr[0].rids ||
            qr[i].m.pf.logical_reads != qr[0].m.pf.logical_reads)
            die("query repetitions disagree", IXE_INVALID);
    }
    qsort(sorted, reps, sizeof *sorted, cmp_double);
    if (stat(indexFile, &st) != 0)
        st.st_size = -1;

    printf("{\"method\": %d, \"name\": \"%s\", \"bufsize\": %d, \"policy\": \"%s\", ",
           method->id, method->name, bufsize, policy == PF_POLICY_MRU ? "mru" : "lru");
    printf("\"records_total\": %ld, \"records_indexed\": %ld, \"records_skipped\": %ld, ",
           br.recordsTotal, br.recordsIndexed, br.recordsSkipped);
    json_measure("setup", &br.setup);
    printf(", ");
    json_measure("build", &br.build);
    printf(", \"tree\": {\"height\": %d, \"internal_pages\": %ld, \"leaf_pages\": %ld, "
           "\"keys\": %ld, \"entries\": %ld, \"root_max_keys\": %d, "
           "\"leaf_fill\": %.4f, \"file_bytes\": %lld}, ",
           tree.height, tree.internalPages, tree.leafPages, tree.keys, tree.entries,
           tree.rootMaxKeys, tree.leafFill, (long long)st.st_size);
    printf("\"validated\": %s, \"validation\": {\"expected_entries\": %ld, "
           "\"scanned_entries\": %ld, \"distinct_keys\": %ld, \"keys_checked\": %ld, "
           "\"absent_checked\": %ld, \"duplicate_keys\": %ld}, ",
           validate ? "true" : "false", v.expectedEntries, v.scannedEntries,
           v.distinctKeys, v.keysChecked, v.absentChecked, v.duplicateKeys);
    printf("\"query\": {\"count\": %ld, \"found\": %ld, \"not_found\": %ld, "
           "\"rids\": %ld, \"rid_checksum\": \"%016llx\", \"reps\": %d, "
           "\"seconds_median\": %.6f, \"seconds_min\": %.6f, \"seconds_all\": [",
           qr[0].queries, qr[0].found, qr[0].notFound, qr[0].rids, qr[0].ridSum, reps,
           reps % 2 ? sorted[reps / 2] : (sorted[reps / 2 - 1] + sorted[reps / 2]) / 2,
           sorted[0]);
    for (i = 0; i < reps; i++)
        printf("%s%.6f", i ? ", " : "", times[i]);
    printf("], ");
    json_measure("first_rep", &qr[0].m);
    printf("}}\n");

    if (!keep)
        IX_RemoveFiles(studentFile, IX_INDEX_NO);
    IX_FreeQueries(&q);
    SD_Free(&d);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2)
        usage();
    if (strcmp(argv[1], "queries") == 0)
        return cmd_queries(argc, argv);
    if (strcmp(argv[1], "run") == 0)
        return cmd_run(argc, argv);
    if (strcmp(argv[1], "methods") == 0) {
        const IX_Method *m;
        for (m = IX_Methods; m->build; m++)
            printf("%d %s: %s\n", m->id, m->name, m->description);
        return 0;
    }
    usage();
    return 2;
}
