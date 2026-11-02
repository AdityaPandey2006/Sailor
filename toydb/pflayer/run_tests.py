"""Run every PF test in a separate temporary working directory."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent
cases = [('testhash', []), ('testpf', []), ('teststats', [])]
cases += [('testcapacity', [str(n)]) for n in (1, 2, 5, 20, 32)]
cases += [(name, [policy]) for name in ('testreplacement', 'testdirty') for policy in ('lru', 'mru')]
for name, args in cases:
    with tempfile.TemporaryDirectory(prefix='toydb-test-') as scratch:
        result = subprocess.run([str(root / name), *args], cwd=scratch, capture_output=True, text=True)
    if result.returncode:
        raise SystemExit(f'{name} {args} failed ({result.returncode}):\n{result.stdout}\n{result.stderr}')
    print(f'PASS {name} {" ".join(args)}')
