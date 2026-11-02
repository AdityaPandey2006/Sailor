"""Run reproducible page workloads and plot mean statistics using matplotlib."""
import argparse
import csv
import json
from pathlib import Path
import statistics
import subprocess
import tempfile

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--config', type=Path, default=Path(__file__).with_name('experiments.json'))
    parser.add_argument('--output', type=Path, default=Path(__file__).resolve().parents[2] / 'outputs/page-buffering')
    args = parser.parse_args()
    config = json.loads(args.config.read_text())
    for key in ('buffer_pages', 'file_pages', 'operations', 'repetitions'):
        if type(config[key]) is not int or config[key] < 1:
            raise ValueError(f'{key} must be a positive integer')
    if not config['write_percentages'] or any(type(x) is not int or not 0 <= x <= 100 for x in config['write_percentages']):
        raise ValueError('write_percentages must contain integers from 0 to 100')
    if not config['policies'] or any(x not in ('LRU', 'MRU') for x in config['policies']):
        raise ValueError('policies must contain LRU or MRU')
    if not config['access_patterns'] or any(x not in ('sequential', 'random') for x in config['access_patterns']):
        raise ValueError('access_patterns must contain sequential or random')
    args.output.mkdir(parents=True, exist_ok=True)
    binary = Path(__file__).with_name('benchmark').resolve()
    metrics = ['logical_reads', 'logical_writes', 'physical_reads', 'physical_writes', 'buffer_hits', 'buffer_misses', 'seconds']
    rows = []
    for pattern in config['access_patterns']:
        for policy in config['policies']:
            for writes in sorted(config['write_percentages']):
                for repeat in range(config['repetitions']):
                    command = [str(binary), str(config['buffer_pages']), str(config['file_pages']), str(config['operations']), str(writes), str(int(policy == 'MRU')), str(int(pattern == 'sequential')), str(config['seed'] + repeat)]
                    with tempfile.TemporaryDirectory(prefix='toydb-benchmark-') as scratch:
                        result = subprocess.run(command, cwd=scratch, capture_output=True, text=True, check=True)
                    values = result.stdout.strip().split(',')
                    row = dict(pattern=pattern, policy=policy, write_percent=writes, repeat=repeat + 1)
                    row.update({k: float(v) if k == 'seconds' else int(v) for k, v in zip(metrics, values, strict=True)})
                    assert row['logical_reads'] == config['operations']
                    assert row['logical_writes'] == config['operations'] * writes // 100
                    assert row['buffer_hits'] + row['buffer_misses'] == row['logical_reads']
                    assert row['physical_reads'] == row['buffer_misses']
                    if writes == 0:
                        assert row['physical_writes'] == 0
                    rows.append(row)
    with (args.output / 'results.csv').open('w', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader(); writer.writerows(rows)
    (args.output / 'config.json').write_text(json.dumps(config, indent=2) + '\n')
    summary = []
    for pattern in config['access_patterns']:
        for policy in config['policies']:
            for writes in sorted(config['write_percentages']):
                group = [r for r in rows if r['pattern'] == pattern and r['policy'] == policy and r['write_percent'] == writes]
                summary.append(dict(pattern=pattern, policy=policy, write_percent=writes,
                                    **{k: statistics.mean(r[k] for r in group) for k in metrics}))
    with (args.output / 'summary.csv').open('w', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(summary[0]))
        writer.writeheader(); writer.writerows(summary)
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    for pattern in config['access_patterns']:
        fig, axes = plt.subplots(2, 3, figsize=(15, 8), layout='constrained')
        for ax, metric in zip(axes.flat, metrics[:6]):
            for policy in config['policies']:
                x = sorted(config['write_percentages'])
                y = [statistics.mean(r[metric] for r in rows if r['pattern'] == pattern and r['policy'] == policy and r['write_percent'] == w) for w in x]
                ax.plot(x, y, marker='o', label=policy)
            ax.set_title(metric.replace('_', ' ').capitalize())
            ax.set_xticks(x, [f'{100-w}/{w}' for w in x])
            ax.set_xlabel('Read/write query mixture (%)')
            ax.set_ylabel('Page operations (mean)')
            ax.grid(alpha=.25); ax.legend()
        fig.suptitle(f'{pattern.capitalize()} access | {config["buffer_pages"]} buffer pages | {config["file_pages"]} file pages | {config["operations"]:,} queries')
        fig.savefig(args.output / f'{pattern}.png', dpi=150)
        plt.close(fig)
    print(f'Saved {len(rows)} experiment runs and graphs to {args.output}')

if __name__ == '__main__':
    main()
