"""Build and run every part-2 (slotted-page) test; save a log of the results.

Usage (from the repository root, on Linux/WSL):
    python3 toydb/splayer/run_tests.py [--no-build] [--with-pf] [--output DIR]

Each test case runs in its own empty temporary directory, so the database
files it creates never touch the repository.  All cases run even if one
fails; the script exits with status 1 if any case failed.

Results are written to <output>/test-results.txt (human-readable, including
each program's output) and <output>/test-results.json (machine-readable).
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path

HERE = Path(__file__).resolve().parent            # toydb/splayer
REPO = HERE.parent.parent
PF_DIR = HERE.parent / 'pflayer'
DEFAULT_OUTPUT = REPO / 'outputs' / 'slotted-pages'
DEFAULT_DATA = REPO / 'data' / 'student.txt'

# The test matrix.  Small buffer pools (1 and 2 frames) prove that no
# operation leaves a page fixed; both first-fit search modes are exercised.
BUFFER_SIZES = (1, 2, 20)
SEARCH_MODES = ('cached', 'scan')
LOAD_BUFFER_SIZES = (1, 20)
LOAD_RECORD_LENGTHS = (100, 108, 128, 256)   # 100 < longest record: rejections
EXPECTED_STUDENTS = 17813                     # data lines in student.txt
PF_CFLAGS = '-std=gnu17 -fpermissive'         # see toydb/splayer/Makefile


@dataclass
class Case:
    """One invocation of a test program."""
    name: str
    program: Path
    args: list[str] = field(default_factory=list)

    @property
    def label(self) -> str:
        return ' '.join([self.name, *self.args]).replace(str(REPO) + '/', '')


@dataclass
class Result:
    case: Case
    passed: bool
    returncode: int
    seconds: float
    output: str


def part2_cases(data: Path) -> list[Case]:
    """Unit tests for both layers plus the full student-file load."""
    cases = [Case(prog, HERE / prog, [str(n), mode])
             for prog in ('testslotted', 'testfixed')
             for n in BUFFER_SIZES for mode in SEARCH_MODES]
    cases += [Case('testload', HERE / 'testload',
                   [str(data), str(n), str(EXPECTED_STUDENTS), *map(str, LOAD_RECORD_LENGTHS)])
              for n in LOAD_BUFFER_SIZES]
    return cases


def pf_cases() -> list[Case]:
    """The part-1 PF tests, mirroring toydb/pflayer/run_tests.py."""
    cases = [Case(n, PF_DIR / n) for n in ('testhash', 'testpf', 'teststats')]
    cases += [Case('testcapacity', PF_DIR / 'testcapacity', [str(n)]) for n in (1, 2, 5, 20, 32)]
    cases += [Case(n, PF_DIR / n, [p]) for n in ('testreplacement', 'testdirty') for p in ('lru', 'mru')]
    return cases


def build(with_pf: bool) -> None:
    """Rebuild from source so stale binaries are never tested."""
    commands = [['make', '-B', '-C', str(HERE)]]
    if with_pf:
        commands.append(['make', '-B', '-C', str(PF_DIR), 'tests', f'CFLAGS={PF_CFLAGS}'])
    for cmd in commands:
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode:
            sys.exit(f'build failed: {" ".join(cmd)}\n{result.stdout}\n{result.stderr}')
        print(f'built: {" ".join(cmd[1:])}')


def run_case(case: Case) -> Result:
    with tempfile.TemporaryDirectory(prefix='toydb-sp-test-') as scratch:
        start = time.monotonic()
        proc = subprocess.run([str(case.program), *case.args], cwd=scratch,
                              capture_output=True, text=True)
        seconds = time.monotonic() - start
    output = proc.stdout + (('\n[stderr]\n' + proc.stderr) if proc.stderr else '')
    return Result(case, proc.returncode == 0, proc.returncode, seconds, output)


def write_reports(results: list[Result], output: Path) -> None:
    output.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime('%Y-%m-%d %H:%M:%S UTC')
    passed = sum(r.passed for r in results)
    lines = [f'ToyDB part 2 test results - {stamp}',
             f'{passed}/{len(results)} cases passed', '']
    lines += [f'{"PASS" if r.passed else "FAIL"}  {r.case.label}  ({r.seconds:.2f}s)' for r in results]
    for r in results:
        lines += ['', '=' * 72, f'{r.case.label}  -> exit {r.returncode}', '=' * 72, r.output.rstrip()]
    (output / 'test-results.txt').write_text('\n'.join(lines) + '\n')
    (output / 'test-results.json').write_text(json.dumps({
        'timestamp': stamp, 'passed': passed, 'total': len(results),
        'cases': [{'case': r.case.label, 'passed': r.passed, 'exit_code': r.returncode,
                   'seconds': round(r.seconds, 3)} for r in results]}, indent=2) + '\n')


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--no-build', action='store_true', help='use existing binaries')
    parser.add_argument('--with-pf', action='store_true', help='also run the part-1 PF tests')
    parser.add_argument('--data', type=Path, default=DEFAULT_DATA, help='student.txt path')
    parser.add_argument('--output', type=Path, default=DEFAULT_OUTPUT, help='report directory')
    args = parser.parse_args()

    if not args.no_build:
        build(args.with_pf)
    cases = (pf_cases() if args.with_pf else []) + part2_cases(args.data.resolve())
    results = []
    for case in cases:
        result = run_case(case)
        results.append(result)
        print(f'{"PASS" if result.passed else "FAIL"} {case.label}')
        if not result.passed:
            print(result.output.rstrip())
    write_reports(results, args.output)
    failed = [r for r in results if not r.passed]
    print(f'\n{len(results) - len(failed)}/{len(results)} passed; '
          f'report: {args.output / "test-results.txt"}')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
