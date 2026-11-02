"""Run the part-2 storage experiments and write tables, CSVs and graphs.

Usage (from the repository root, on Linux/WSL):
    python3 toydb/splayer/run_experiments.py [--config FILE] [--output DIR] [--no-build]

Everything that can vary lives in experiments.json (next to this script);
no C changes are needed.  Two experiment groups are run:

  space  slotted pages vs fixed-length records for every record length L in
         space.fixed_record_lengths, each followed by deleting and re-inserting
         space.delete_percentages of the records.  Produces the utilization
         tables the assignment asks for.
  io     the PF I/O cost of the same workload for each layout x first-fit
         search mode x replacement policy, repeated io.repetitions times
         (counts are deterministic; repetitions only average elapsed time).

Each run is one invocation of ./spbench in a fresh temporary directory.
Outputs (default outputs/slotted-pages/):
  config.json            the exact configuration used
  raw_results.jsonl      one JSON object per run, as printed by spbench
  space_results.csv      flattened space runs
  io_results.csv         flattened io runs
  space_utilization.md   the comparison tables
  io_comparison.md       I/O tables (means over repetitions)
  utilization.png, io_load.png   graphs (skipped if matplotlib is missing)
"""
from __future__ import annotations

import argparse
import csv
import json
import statistics
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
DEFAULT_CONFIG = HERE / 'experiments.json'
DEFAULT_OUTPUT = REPO / 'outputs' / 'slotted-pages'
PHASES = ('load', 'scan', 'delete', 'reinsert')
COUNTERS = ('logical_reads', 'logical_writes', 'physical_reads', 'physical_writes',
            'buffer_hits', 'buffer_misses', 'ms')
SPACE_POINTS = ('after_load', 'after_delete', 'after_reinsert')
SPACE_FIELDS = ('pages', 'records', 'record_bytes', 'metadata_bytes', 'padding_bytes',
                'free_bytes', 'utilization')
PAGE_SIZE = 4096


# ---------------------------------------------------------------- running

def run_spbench(cfg: dict, data: Path, layout: str, rec_len: int, policy: str,
                search: str, delete_pct: int) -> dict:
    """Runs one experiment and returns spbench's JSON result."""
    cmd = [str(HERE / 'spbench'), layout, str(rec_len), str(data),
           str(cfg['skip_header_lines']), str(cfg['buffer_pages']), policy.lower(),
           search, str(delete_pct), str(cfg['seed'])]
    with tempfile.TemporaryDirectory(prefix='toydb-sp-bench-') as scratch:
        proc = subprocess.run(cmd, cwd=scratch, capture_output=True, text=True)
    if proc.returncode:
        sys.exit(f'spbench failed ({proc.returncode}): {" ".join(cmd)}\n{proc.stdout}\n{proc.stderr}')
    result = json.loads(proc.stdout)
    if not result['verified']:
        sys.exit(f'spbench verification failed: {" ".join(cmd)}')
    return result


def space_runs(cfg: dict, data: Path) -> list[dict]:
    sp = cfg['space']
    layouts = [('slotted', 0)] + [('fixed', n) for n in sp['fixed_record_lengths']]
    runs = []
    for layout, rec_len in layouts:
        for pct in sp['delete_percentages']:
            r = run_spbench(cfg, data, layout, rec_len, sp['policy'], sp['search'], pct)
            r['group'] = 'space'
            runs.append(r)
            print(f'space  {label(r):<16} delete {pct:>3}%  '
                  f'utilization {100 * r["space"]["after_load"]["utilization"]:.2f}%')
    return runs


def io_runs(cfg: dict, data: Path) -> list[dict]:
    io = cfg['io']
    runs = []
    for spec in io['layouts']:
        for search in io['search_modes']:
            for policy in io['policies']:
                for rep in range(io['repetitions']):
                    r = run_spbench(cfg, data, spec['layout'], spec.get('record_length', 0),
                                    policy, search, io['delete_percentage'])
                    r['group'] = 'io'
                    r['repetition'] = rep + 1
                    runs.append(r)
                    print(f'io     {label(r):<16} {search:<6} {policy}  rep {rep + 1}  '
                          f'load {r["phases"]["load"]["ms"]:.1f} ms')
    return runs


# ---------------------------------------------------------------- reporting

def label(r: dict) -> str:
    return 'Slotted' if r['layout'] == 'slotted' else f'Fixed L={r["record_length"]}'


def flatten(r: dict) -> dict:
    row = {k: r[k] for k in ('group', 'layout', 'record_length', 'buffer_pages', 'policy',
                             'search', 'delete_pct', 'seed', 'records_input', 'records_stored',
                             'records_rejected', 'deleted')}
    row['repetition'] = r.get('repetition', 1)
    for phase in PHASES:
        for c in COUNTERS:
            row[f'{phase}_{c}'] = r['phases'][phase][c]
    for point in SPACE_POINTS:
        for f in SPACE_FIELDS:
            row[f'{point}_{f}'] = r['space'][point][f]
    return row


def write_csv(path: Path, runs: list[dict]) -> None:
    rows = [flatten(r) for r in runs]
    with path.open('w', newline='') as fh:
        writer = csv.DictWriter(fh, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def md_table(header: list[str], rows: list[list]) -> str:
    out = ['| ' + ' | '.join(header) + ' |', '|' + '|'.join(' --- ' for _ in header) + '|']
    out += ['| ' + ' | '.join(str(c) for c in row) + ' |' for row in rows]
    return '\n'.join(out)


def pct(x: float) -> str:
    return f'{100 * x:.2f}%'


def by_layout(runs: list[dict]) -> dict[tuple, dict[int, dict]]:
    """{(layout, L): {delete_pct: run}} preserving configuration order."""
    table: dict[tuple, dict[int, dict]] = {}
    for r in runs:
        table.setdefault((r['layout'], r['record_length']), {})[r['delete_pct']] = r
    return table


def space_report(runs: list[dict], cfg: dict) -> str:
    groups = by_layout(runs)
    first = runs[0]
    lines = ['# Part 2: space utilization - slotted pages vs fixed-length records', '',
             f'Input: {first["records_input"]} student records, {first["input_bytes"]:,} bytes, '
             f'longest {first["max_input_length"]} bytes. Page size {PAGE_SIZE} bytes; '
             f'buffer pool {cfg["buffer_pages"]} pages; first-fit insertion.', '',
             'Utilization = record bytes / (pages x 4096). Every byte of every page is counted '
             'as record data, metadata (page header + slot directory or bitmap), padding '
             '(unused part of fixed slots and the page tail) or free space; the four add up '
             'to pages x 4096.', '',
             '## Table 1 - after loading every record', '']
    rows = []
    for (layout, rec_len), per_pct in groups.items():
        r = next(iter(per_pct.values()))
        s = r['space']['after_load']
        rows.append(['Slotted (variable)' if layout == 'slotted' else 'Fixed',
                     '-' if layout == 'slotted' else rec_len,
                     f'{r["records_stored"]:,}', f'{r["records_rejected"]:,}', f'{s["pages"]:,}',
                     f'{s["records"] / s["pages"]:.1f}', f'{s["record_bytes"]:,}',
                     f'{s["metadata_bytes"]:,}', f'{s["padding_bytes"]:,}', f'{s["free_bytes"]:,}',
                     pct(s['utilization'])])
    lines += [md_table(['Layout', 'Max record length L', 'Records stored', 'Rejected (> L)',
                        'Pages', 'Records / page', 'Record bytes', 'Metadata bytes',
                        'Padding bytes', 'Free bytes', 'Utilization'], rows), '']

    pcts = [p for p in cfg['space']['delete_percentages'] if p > 0]
    if pcts:
        lines += ['## Table 2 - after deleting records and inserting them again', '',
                  'The same randomly chosen students (seeded shuffle) are deleted from every '
                  'layout, then inserted again with first fit. The page count never changes '
                  'here: deletion frees space inside pages but no page is released.', '']
        header = ['Layout', 'After load']
        for p in pcts:
            header += [f'After {p}% deleted', f'After re-insert ({p}%)']
        rows = []
        for (layout, rec_len), per_pct in groups.items():
            name = 'Slotted' if layout == 'slotted' else f'Fixed L={rec_len}'
            row = [name, pct(next(iter(per_pct.values()))['space']['after_load']['utilization'])]
            for p in pcts:
                r = per_pct[p]
                row += [pct(r['space']['after_delete']['utilization']),
                        pct(r['space']['after_reinsert']['utilization'])]
            rows.append(row)
        lines += [md_table(header, rows), '']
    return '\n'.join(lines)


def io_means(runs: list[dict]) -> list[dict]:
    """Averages repetitions of each (layout, L, search, policy) configuration."""
    groups: dict[tuple, list[dict]] = {}
    for r in runs:
        groups.setdefault((r['layout'], r['record_length'], r['search'], r['policy']), []).append(r)
    means = []
    for (layout, rec_len, search, policy), rs in groups.items():
        m = {'layout': layout, 'record_length': rec_len, 'search': search, 'policy': policy,
             'label': label(rs[0]), 'n': len(rs)}
        for phase in PHASES:
            m[phase] = {c: statistics.mean(r['phases'][phase][c] for r in rs) for c in COUNTERS}
        means.append(m)
    return means


def io_report(means: list[dict], cfg: dict) -> str:
    io = cfg['io']
    lines = ['# Part 2: PF I/O cost of the record layers', '',
             f'Buffer pool {cfg["buffer_pages"]} pages. Each phase starts with reset counters; '
             'load, delete and re-insert end by closing the file so every dirty page is '
             f'written inside the phase. Delete/re-insert use {io["delete_percentage"]}% of the '
             f'records. Counts are identical across repetitions; times are means of '
             f'{io["repetitions"]} runs.', '',
             '"cached" first fit keeps each page\'s free space in memory and fetches only the '
             'chosen page; "scan" first fit fetches pages from page 0 until one fits. Both place '
             'every record on the same page (verified by the tests).', '']
    titles = {'load': 'Load (insert all records)', 'scan': 'Sequential scan',
              'delete': f'Delete {io["delete_percentage"]}%', 'reinsert': 'Re-insert deleted records'}
    for phase in PHASES:
        rows = [[m['label'], m['search'], m['policy'],
                 f'{m[phase]["logical_reads"]:,.0f}', f'{m[phase]["logical_writes"]:,.0f}',
                 f'{m[phase]["physical_reads"]:,.0f}', f'{m[phase]["physical_writes"]:,.0f}',
                 f'{m[phase]["buffer_hits"]:,.0f}', f'{m[phase]["ms"]:.1f}'] for m in means]
        lines += [f'## {titles[phase]}', '',
                  md_table(['Layout', 'Search', 'Policy', 'Logical reads', 'Logical writes',
                            'Physical reads', 'Physical writes', 'Buffer hits', 'Time (ms)'], rows), '']
    return '\n'.join(lines)


# ---------------------------------------------------------------- graphs

# Validated categorical palette (dataviz reference palette, light mode).
SURFACE, INK, INK_2, GRID = '#fcfcfb', '#0b0b0b', '#52514e', '#e4e3df'
SERIES = ('#2a78d6', '#eb6834', '#1baf7a')


def compact(v: float) -> str:
    """4003039 -> '4.0M', 17360 -> '17k', 10.3 -> '10'."""
    if v >= 1e6:
        return f'{v / 1e6:.1f}M'
    if v >= 1e4:
        return f'{v / 1e3:.0f}k'
    return f'{v:,.0f}'


def style_axes(ax) -> None:
    ax.set_facecolor(SURFACE)
    for side in ('top', 'right', 'left'):
        ax.spines[side].set_visible(False)
    ax.spines['bottom'].set_color(INK_2)
    ax.tick_params(colors=INK_2, length=0)
    ax.yaxis.grid(True, color=GRID, linewidth=0.8)
    ax.set_axisbelow(True)


def plot(space: list[dict], means: list[dict], cfg: dict, output: Path) -> list[Path]:
    try:
        import matplotlib
        matplotlib.use('Agg')
        import matplotlib.pyplot as plt
    except ImportError:
        print('matplotlib not installed: skipping graphs (tables and CSVs were written)')
        return []
    written = []

    # Utilization per layout: after load and after each delete percentage.
    groups = by_layout(space)
    names, rejected = [], []
    for (layout, rec_len), per_pct in groups.items():
        r = next(iter(per_pct.values()))
        names.append('Slotted' if layout == 'slotted' else f'Fixed\nL={rec_len}')
        rejected.append(r['records_rejected'])
    pcts = [p for p in cfg['space']['delete_percentages'] if p > 0][:2]
    series = [('After load', [next(iter(g.values()))['space']['after_load']['utilization'] * 100
                              for g in groups.values()])]
    series += [(f'After {p}% deleted', [g[p]['space']['after_delete']['utilization'] * 100
                                       for g in groups.values()]) for p in pcts]
    fig, ax = plt.subplots(figsize=(10, 5.2), facecolor=SURFACE)
    style_axes(ax)
    width = 0.8 / len(series)
    for i, (name, values) in enumerate(series):
        xs = [x + (i - (len(series) - 1) / 2) * width for x in range(len(names))]
        bars = ax.bar(xs, values, width, color=SERIES[i], label=name,
                      edgecolor=SURFACE, linewidth=2)
        if i == 0:   # label the headline series only
            for b, v in zip(bars, values):
                ax.text(b.get_x() + b.get_width() / 2, v + 1, f'{v:.1f}', ha='center',
                        va='bottom', fontsize=8, color=INK)
    ax.set_xticks(range(len(names)))
    ax.set_xticklabels([n + (f'\n({k:,} rejected)' if k else '') for n, k in zip(names, rejected)],
                       color=INK_2, fontsize=9)
    ax.set_ylim(0, 105)
    ax.set_ylabel('Space utilization (%)', color=INK_2)
    ax.set_title('Space utilization: slotted pages vs fixed-length records\n'
                 f'{space[0]["records_input"]:,} student records, 4096-byte pages',
                 color=INK, fontsize=11, loc='left')
    ax.legend(frameon=False, loc='upper right', fontsize=9, labelcolor=INK_2, ncol=len(series))
    fig.tight_layout()
    path = output / 'utilization.png'
    fig.savefig(path, dpi=150, facecolor=SURFACE)
    plt.close(fig)
    written.append(path)

    # Load-phase I/O per search mode x policy, one panel per measure.
    configs = []
    for m in means:
        key = (m['search'], m['policy'])
        if key not in configs:
            configs.append(key)
    layouts = []
    for m in means:
        if m['label'] not in layouts:
            layouts.append(m['label'])
    panels = [('physical_reads', 'Physical reads (log scale)'),
              ('logical_reads', 'Logical reads (log scale)'),
              ('ms', 'Elapsed time, ms (log scale)')]
    fig, axes = plt.subplots(1, 3, figsize=(13, 4.6), facecolor=SURFACE)
    width = 0.8 / len(layouts)
    for ax, (metric, title) in zip(axes, panels):
        style_axes(ax)
        for i, lay in enumerate(layouts):
            values = [next(m['load'][metric] for m in means
                           if m['label'] == lay and (m['search'], m['policy']) == c) for c in configs]
            xs = [x + (i - (len(layouts) - 1) / 2) * width for x in range(len(configs))]
            # Log axis starts at 1: a zero count draws no bar but keeps its "0" label.
            ax.bar(xs, [v if v >= 1 else 0 for v in values], width, bottom=0,
                   color=SERIES[i], label=lay, edgecolor=SURFACE, linewidth=2)
            for x, v in zip(xs, values):
                ax.text(x, max(v, 1) * 1.25, compact(v), ha='center', va='bottom',
                        fontsize=7.5, color=INK)
        ax.set_yscale('log')
        top = max(m['load'][metric] for m in means)
        ax.set_ylim(1, max(top, 1) * 12)            # headroom for the value labels
        ax.yaxis.set_minor_locator(plt.NullLocator())
        ax.set_xticks(range(len(configs)))
        ax.set_xticklabels([f'{s}\n{p}' for s, p in configs], color=INK_2, fontsize=9)
        ax.set_title(title, color=INK, fontsize=10, loc='left')
    axes[0].legend(frameon=False, fontsize=9, labelcolor=INK_2, loc='upper left')
    fig.suptitle(f'Loading {space[0]["records_input"]:,} records: '
                 'first-fit search mode x replacement policy '
                 f'({cfg["buffer_pages"]}-page buffer)', color=INK, fontsize=11, x=0.01, ha='left')
    fig.tight_layout()
    path = output / 'io_load.png'
    fig.savefig(path, dpi=150, facecolor=SURFACE)
    plt.close(fig)
    written.append(path)
    return written


# ---------------------------------------------------------------- main

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--config', type=Path, default=DEFAULT_CONFIG)
    parser.add_argument('--output', type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument('--no-build', action='store_true', help='use the existing spbench binary')
    parser.add_argument('--skip-io', action='store_true', help='run only the space experiments')
    args = parser.parse_args()

    cfg = json.loads(args.config.read_text())
    data = (args.config.resolve().parent / cfg['data_file']).resolve()
    if not args.no_build:
        proc = subprocess.run(['make', '-C', str(HERE), 'spbench'], capture_output=True, text=True)
        if proc.returncode:
            sys.exit(f'build failed:\n{proc.stdout}\n{proc.stderr}')
    args.output.mkdir(parents=True, exist_ok=True)

    space = space_runs(cfg, data)
    io = [] if args.skip_io else io_runs(cfg, data)
    (args.output / 'config.json').write_text(json.dumps(cfg, indent=2) + '\n')
    with (args.output / 'raw_results.jsonl').open('w') as fh:
        for r in space + io:
            fh.write(json.dumps(r) + '\n')
    write_csv(args.output / 'space_results.csv', space)
    (args.output / 'space_utilization.md').write_text(space_report(space, cfg))
    means = []
    if io:
        write_csv(args.output / 'io_results.csv', io)
        means = io_means(io)
        (args.output / 'io_comparison.md').write_text(io_report(means, cfg))
    graphs = plot(space, means, cfg, args.output) if means else []
    print(f'\nwrote results to {args.output}' + (f' ({len(graphs)} graphs)' if graphs else ''))
    return 0


if __name__ == '__main__':
    sys.exit(main())
