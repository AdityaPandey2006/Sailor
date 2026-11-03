"""Build and run every part-3 (index construction) test; save a log.

Usage (from the repository root, on Linux/WSL):
    python3 toydb/amindex/run_tests.py [--no-build] [--output DIR]

Each case runs in its own empty temporary directory, so the files it
creates never touch the repository.  All cases run even if one fails; the
script exits with status 1 if any case failed.  Results are written to
<output>/test-results.txt and <output>/test-results.json.
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
import time
from datetime import datetime, timezone
from pathlib import Path

HERE = Path(__file__).resolve().parent            # toydb/amindex
REPO = HERE.parent.parent
DATA = REPO / 'data' / 'student.txt'
DEFAULT_OUTPUT = REPO / 'outputs' / 'index-construction'

# (buffer frames, synthetic records).  3 frames is the AM layer's minimum
# (a root split fixes 3 pages at once) and proves no page is left fixed;
# 150000 synthetic records force internal-node splits (a 3-level tree).
TESTINDEX_CASES = ((3, 150000), (20, 150000), (200, 0))


def run_case(label: str, cmd_fn) -> dict:
    with tempfile.TemporaryDirectory(prefix='ixtest-') as work:
        cmds = cmd_fn(work)
        t0 = time.monotonic()
        output, rc = '', 0
        for cmd in cmds:
            p = subprocess.run(cmd, capture_output=True, text=True)
            output += f'$ {" ".join(cmd)}\n{p.stdout}{p.stderr}'
            rc = p.returncode
            if rc != 0:
                break
        extra = check_bench(output) if rc == 0 and label.startswith('ixbench') else None
        if extra:
            output += f'CHECK FAILED: {extra}\n'
            rc = 1
        output = output.replace(work, '<tmpdir>')
    output = output.replace(str(REPO) + '/', '')
    res = {'name': label, 'passed': rc == 0, 'returncode': rc,
           'seconds': round(time.monotonic() - t0, 3), 'output': output}
    print(f'{"PASS" if res["passed"] else "FAIL"}  {label}  ({res["seconds"]} s)')
    return res


def check_bench(output: str) -> str | None:
    """Both methods must validate and return the same answers."""
    runs = [json.loads(l) for l in output.splitlines() if l.startswith('{"method"')]
    if len(runs) != 3:
        return f'expected 3 benchmark results, got {len(runs)}'
    for r in runs:
        if not r['validated'] or r['records_indexed'] != 17813:
            return f'method {r["method"]} not validated / wrong count'
        if r['query']['found'] != 300 or r['query']['not_found'] != 100:
            return f'method {r["method"]} found {r["query"]["found"]}'
    if runs[0]['query']['rid_checksum'] != runs[1]['query']['rid_checksum']:
        return 'methods returned different answers'
    if len({(r['query']['found'], r['query']['rids']) for r in runs}) != 1:
        return 'methods returned different result counts'
    return None


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--no-build', action='store_true')
    ap.add_argument('--output', type=Path, default=DEFAULT_OUTPUT)
    args = ap.parse_args()

    if not args.no_build:
        p = subprocess.run(['make', '-C', str(HERE)], capture_output=True, text=True)
        if p.returncode != 0:
            sys.exit(f'build failed:\n{p.stdout}{p.stderr}')

    results = [run_case('testbulk boundaries, duplicate limits, MRU and AM mutations',
                        lambda w: [[str(HERE / 'testbulk'), w]])]
    for bufsize, large in TESTINDEX_CASES:
        results.append(run_case(
            f'testindex bufsize={bufsize} synthetic={large}',
            lambda w, b=bufsize, n=large: [[str(HERE / 'testindex'), str(DATA), w,
                                            str(b), str(n)]]))
    results.append(run_case(
        'ixbench methods 1+2+3 --validate (bufsize=20)',
        lambda w: [[str(HERE / 'ixbench'), 'queries', '--data', str(DATA),
                    '--hits', '300', '--misses', '100', '--seed', '5',
                    '--out', f'{w}/q.txt']] +
                  [[str(HERE / 'ixbench'), 'run', '--method', m, '--data', str(DATA),
                    '--queries', f'{w}/q.txt', '--workdir', w, '--bufsize', '20',
                    '--reps', '2', '--validate'] for m in ('1', '2', '3')]))

    args.output.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime('%Y-%m-%d %H:%M:%S UTC')
    passed = sum(r['passed'] for r in results)
    with open(args.output / 'test-results.txt', 'w') as f:
        f.write(f'Part 3 index tests - {stamp}\n{passed}/{len(results)} passed\n\n')
        for r in results:
            f.write(f'=== {"PASS" if r["passed"] else "FAIL"} {r["name"]} '
                    f'({r["seconds"]} s)\n{r["output"]}\n')
    (args.output / 'test-results.json').write_text(json.dumps(
        {'generated': stamp, 'passed': passed, 'total': len(results),
         'cases': results}, indent=2) + '\n')
    print(f'{passed}/{len(results)} passed; log in '
          f'{(args.output / "test-results.txt").relative_to(REPO)}')
    sys.exit(0 if passed == len(results) else 1)


if __name__ == '__main__':
    main()
