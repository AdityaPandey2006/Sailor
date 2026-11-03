"""Create the two-student submission with sources and reproducible results."""
from pathlib import Path
import tarfile

ROOT = Path(__file__).resolve().parent
NAME = 'B24CS1087-B24CS1113'

def include(path):
    rel = path.relative_to(ROOT)
    if any(part in {'.git', '.agents', '.aws', '.codex', '__pycache__', 'build', 'work', '.venv', 'node_modules'} for part in rel.parts):
        return False
    if rel.parts[0] == 'data':
        return str(rel) == 'data/student.txt'
    if path.name.endswith('.inspect.ndjson') or path.suffix in {'.o', '.pyc', '.tar', '.gz'}:
        return False
    if rel.parts[0] == 'outputs':
        return path.suffix in {'.csv', '.json', '.jsonl', '.txt', '.md', '.png', '.xlsx', '.html'}
    return path.suffix in {'.c', '.h', '.py', '.md', '.json', '.pdf', '.ps'} or path.name in {'Makefile', 'makefile', 'mkfile', 'DOC', 'IMP.DOC'}

if __name__ == '__main__':
    target = ROOT / f'{NAME}.tar.gz'
    paths = sorted(p for p in ROOT.rglob('*') if p.is_file() and not p.is_symlink() and include(p))
    with tarfile.open(target, 'w:gz') as archive:
        for path in paths:
            archive.add(path, arcname=str(Path(NAME) / path.relative_to(ROOT)), recursive=False)
    print(f'{target.name}: {len(paths)} files, {target.stat().st_size:,} bytes')
