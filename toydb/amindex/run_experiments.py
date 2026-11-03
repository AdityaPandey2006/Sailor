"""Run the index-construction benchmark and write tables (assignment part 3).

Usage (from the repository root, on Linux/WSL):
    python3 toydb/amindex/run_experiments.py [--config FILE] [--output DIR] [--no-build]

Procedure (identical for every method, including a future Method 3):
  1. build ixbench (make -C toydb/amindex)
  2. generate ONE query file from the dataset with the configured seed;
     every method and buffer size runs exactly these queries in this order
  3. for each buffer size, method and run: one fresh `ixbench run` process
     (fresh buffer pool, fresh files in a temporary directory) that builds
     the Student file + index, inspects/validates it, then times the query
     file `query_reps` times, reopening the index before each repetition
  4. write raw JSON lines, a per-run CSV, a median summary CSV and a
     Markdown comparison table to the output directory

Adding Method 3 needs no change here: register it in IX_Methods[]
(toydb/amindex/ixbuild.c) and add 3 to "methods" in experiments.json.
"""
from __future__ import annotations

import argparse
import csv
import json
import statistics
import subprocess
import sys
import tempfile
from datetime import datetime, timezone
from pathlib import Path

HERE = Path(__file__).resolve().parent            # toydb/amindex
REPO = HERE.parent.parent
DEFAULT_CONFIG = HERE / 'experiments.json'
DEFAULT_OUTPUT = REPO / 'outputs' / 'index-construction'
IXBENCH = HERE / 'ixbench'

PF_FIELDS = ('seconds', 'logical_reads', 'logical_writes',
             'physical_reads', 'physical_writes')


def run(cmd: list[str]) -> str:
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        sys.exit(f'command failed ({proc.returncode}): {" ".join(cmd)}\n'
                 f'{proc.stdout}{proc.stderr}')
    return proc.stdout


def flatten(r: dict) -> dict:
    """One CSV row per run."""
    row = {k: r[k] for k in ('method', 'name', 'bufsize', 'policy', 'run',
                             'records_total', 'records_indexed', 'records_skipped')}
    for part in ('setup', 'build'):
        for f in PF_FIELDS:
            row[f'{part}_{f}'] = r[part][f]
    q = r['query']
    row.update(query_count=q['count'], query_found=q['found'],
               query_not_found=q['not_found'], query_rids=q['rids'],
               query_rid_checksum=q['rid_checksum'],
               query_seconds_median=q['seconds_median'],
               query_seconds_min=q['seconds_min'])
    for f in PF_FIELDS[1:]:
        row[f'query_{f}'] = q['first_rep'][f]
    for k, v in r['tree'].items():
        row[f'tree_{k}'] = v
    row['validated'] = r['validated']
    return row


def summarize(rows: list[dict]) -> list[dict]:
    """Median over runs for every (bufsize, method).  Page counts are
    deterministic, so their median equals every run's value."""
    groups: dict[tuple, list[dict]] = {}
    for row in rows:
        groups.setdefault((row['bufsize'], row['method']), []).append(row)
    out = []
    for (bufsize, method), g in sorted(groups.items()):
        s = {'bufsize': bufsize, 'method': method, 'name': g[0]['name'], 'runs': len(g)}
        for key in g[0]:
            if key in s or key in ('run', 'policy', 'query_rid_checksum', 'validated'):
                continue
            vals = [row[key] for row in g]
            if all(isinstance(v, (int, float)) and not isinstance(v, bool) for v in vals):
                s[key] = statistics.median(vals)
        s['all_validated'] = all(row['validated'] for row in g)
        s['answers_identical'] = len({row['query_rid_checksum'] for row in g}) == 1
        out.append(s)
    return out


def ms(x: float) -> str:
    return f'{x * 1000:.2f} ms'


def markdown(summary: list[dict], cfg: dict, nq: int) -> str:
    methods = sorted({s['method'] for s in summary})
    names = {s['method']: s['name'] for s in summary}
    lines = [
        '# Index construction on roll-no: three-method comparison',
        '',
        f'Generated {datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M UTC")} by '
        '`toydb/amindex/run_experiments.py`. Every value is the median of '
        f'{cfg["runs"]} independent runs; query time is itself the median of '
        f'{cfg["query_reps"]} cold repetitions per run.',
        '',
        f'Dataset `{cfg["dataset"]}`; {nq} point queries '
        f'({cfg["queries"]["hits"]} existing roll-nos, {cfg["queries"]["misses"]} '
        f'absent), seed {cfg["queries"]["seed"]}, same order for every method. '
        f'Replacement policy {cfg["policy"].upper()}.',
        '',
        '*Pages accessed* = PF logical reads (page requests to the buffer pool). '
        '*Disk reads/writes* = PF physical I/O. Construction and query numbers '
        'come from separate counter snapshots.',
        '',
    ]
    for bufsize in sorted({s['bufsize'] for s in summary}):
        by = {s['method']: s for s in summary if s['bufsize'] == bufsize}
        head = '| Metric | ' + ' | '.join(f'Method {m} ({names[m]})' for m in methods) + ' |'
        lines += [f'## Buffer pool: {bufsize} frames', '', head,
                  '|---|' + '---|' * len(methods)]

        def row(label, fn):
            lines.append(f'| {label} | ' + ' | '.join(fn(by[m]) for m in methods) + ' |')

        row('Records indexed', lambda s: f'{int(s["records_indexed"])}')
        row('Construction time', lambda s: ms(s['build_seconds']))
        row('Construction pages accessed', lambda s: f'{int(s["build_logical_reads"])}')
        row('Construction disk reads / writes',
            lambda s: f'{int(s["build_physical_reads"])} / {int(s["build_physical_writes"])}')
        row('Setup outside construction (time)', lambda s: ms(s['setup_seconds']))
        row('Setup + construction time', lambda s: ms(s['setup_seconds'] + s['build_seconds']))
        row('Query completion time', lambda s: ms(s['query_seconds_median']))
        row('Query pages accessed', lambda s: f'{int(s["query_logical_reads"])}')
        row('Query disk reads', lambda s: f'{int(s["query_physical_reads"])}')
        row('Query disk writes', lambda s: f'{int(s["query_physical_writes"])}')
        row('Pages accessed per query', lambda s: f'{s["query_logical_reads"] / nq:.2f}')
        row('RIDs returned', lambda s: f'{int(s["query_rids"])}')
        row('Tree height / internal / leaf pages',
            lambda s: f'{int(s["tree_height"])} / {int(s["tree_internal_pages"])} / '
                      f'{int(s["tree_leaf_pages"])}')
        row('Mean leaf fill', lambda s: f'{s["tree_leaf_fill"] * 100:.1f}%')
        row('Validated (all runs)', lambda s: 'yes' if s['all_validated'] else 'NO')
        lines.append('')
    lines += [
        'Method 1 construction covers creating the index, scanning the existing '
        'Student file and inserting every entry, and closing both files. Writing '
        'the Student file beforehand and opening it are reported as setup. '
        'Method 2 construction covers building the Student file and the index '
        'together, so it has no separate setup. Compare "Setup + construction" '
        'for the end-to-end cost. Method 3 setup includes sorting a copy and '
        'writing the reordered Student file while collecting key/RID pairs. '
        'Construction consumes those sorted pairs and writes the AM tree '
        'bottom-up, including the final close. Sorting is therefore not free '
        'in the end-to-end comparison.',
        '',
    ]
    return '\n'.join(lines)


def graphs(summary: list[dict], out: Path) -> None:
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    plots = [('construction-time', 'build_seconds', 'Index construction time (ms)', 1000),
             ('query-time', 'query_seconds_median', 'Query completion time (ms)', 1000),
             ('query-pages', 'query_logical_reads', 'Query page requests (logical reads)', 1),
             ('construction-pages', 'build_logical_reads', 'Construction page requests (logical reads)', 1)]
    sizes = sorted({r['bufsize'] for r in summary})
    methods = sorted({r['method'] for r in summary})
    for filename, metric, ylabel, scale in plots:
        fig, ax = plt.subplots(figsize=(8, 4.8), layout='constrained')
        width = .8 / len(sizes)
        for j, size in enumerate(sizes):
            by = {r['method']: r for r in summary if r['bufsize'] == size}
            ax.bar([i + (j-(len(sizes)-1)/2)*width for i in range(len(methods))],
                   [by[m][metric]*scale for m in methods], width, label=f'{size} frames')
        ax.set_xticks(range(len(methods)), [f'Method {m}' for m in methods])
        ax.set_xlabel('Indexing method'); ax.set_ylabel(ylabel)
        ax.set_title('Student roll-number index | medians over independent runs')
        ax.grid(axis='y', alpha=.2); ax.set_axisbelow(True); ax.margins(y=.22); ax.legend()
        fig.savefig(out / f'{filename}.png', dpi=160); plt.close(fig)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--config', type=Path, default=DEFAULT_CONFIG)
    ap.add_argument('--output', type=Path, default=DEFAULT_OUTPUT)
    ap.add_argument('--no-build', action='store_true')
    args = ap.parse_args()

    cfg = json.loads(args.config.read_text())
    dataset = (REPO / cfg['dataset']).resolve()
    out = args.output
    out.mkdir(parents=True, exist_ok=True)
    if not args.no_build:
        run(['make', '-C', str(HERE)])

    queries = out / 'queries.txt'
    qinfo = json.loads(run([str(IXBENCH), 'queries', '--data', str(dataset),
                            '--hits', str(cfg['queries']['hits']),
                            '--misses', str(cfg['queries']['misses']),
                            '--seed', str(cfg['queries']['seed']),
                            '--out', str(queries)]))
    results = []
    with tempfile.TemporaryDirectory(prefix='ixbench-') as work:
        for bufsize in cfg['buffer_sizes']:
            for method in cfg['methods']:
                for i in range(cfg['runs']):
                    cmd = [str(IXBENCH), 'run', '--method', str(method),
                           '--data', str(dataset), '--queries', str(queries),
                           '--workdir', work, '--bufsize', str(bufsize),
                           '--policy', cfg['policy'], '--reps', str(cfg['query_reps'])]
                    if cfg.get('validate', True):
                        cmd.append('--validate')
                    r = json.loads(run(cmd))
                    r['run'] = i + 1
                    results.append(r)
                    print(f'bufsize {bufsize:4} method {method} run {i + 1}: '
                          f'build {r["build"]["seconds"] * 1000:7.2f} ms, '
                          f'queries {r["query"]["seconds_median"] * 1000:6.2f} ms, '
                          f'{r["query"]["first_rep"]["logical_reads"]} pages')

    rows = [flatten(r) for r in results]
    summary = summarize(rows)
    # Different sorted-file RIDs are expected, but result cardinalities must agree.
    if len({(r['query_found'], r['query_not_found'], r['query_rids']) for r in rows}) != 1:
        sys.exit('query result cardinalities differ between runs/methods')
    with open(out / 'raw_results.jsonl', 'w') as f:
        for r in results:
            f.write(json.dumps(r) + '\n')
    for name, data in (('results.csv', rows), ('summary.csv', summary)):
        with open(out / name, 'w', newline='') as f:
            w = csv.DictWriter(f, fieldnames=list(data[0]), lineterminator="\n")
            w.writeheader()
            w.writerows(data)
    (out / 'config.json').write_text(json.dumps(
        {**cfg, 'dataset_resolved': str(dataset.relative_to(REPO)),
         'query_file': qinfo}, indent=2) + '\n')
    (out / 'comparison.md').write_text(markdown(summary, cfg, qinfo['queries']))
    graphs(summary, out)
    print(f'wrote {out.relative_to(REPO)}/: comparison.md, summary.csv, '
          'results.csv, raw_results.jsonl, config.json, queries.txt')


if __name__ == '__main__':
    main()
