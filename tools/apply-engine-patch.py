# SPDX-License-Identifier: GPL-3.0-only
"""Validate or apply the client patch without publishing removed baseline secrets."""
import argparse
from pathlib import Path, PurePosixPath
import re
import subprocess
import tempfile

BASELINE = 'e0cfac59f'
ROOT = Path(__file__).resolve().parents[1]
PATCH = ROOT / 'patches/engine.patch'
ALLOWED = {'App', 'Base', 'ClientShared', 'Network', 'Rendering', 'Library', 'content', 'shaders'}
SECRET_FILES = {'App/include/v8datamodel/Stats.h', 'App/util/Analytics.cpp'}
DECLARATION = re.compile(r'(\bcountersApiKey\s*=\s*)"[^"\r\n]*"')

def run_git(directory, *args, data=None):
    return subprocess.check_output(['git', '-C', str(directory), *args], input=data, stderr=subprocess.PIPE)

def normalize(data):
    return data.decode('utf-8-sig').replace('\r\n', '\n')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--engine-dir', type=Path, required=True)
    parser.add_argument('--apply', action='store_true', help='Write validated engine changes; otherwise check only')
    args = parser.parse_args()
    engine = args.engine_dir.resolve()
    baseline = run_git(engine, 'rev-parse', BASELINE + '^{commit}').strip()
    if run_git(engine, 'rev-parse', 'HEAD').strip() != baseline:
        raise ValueError('Use the documented baseline commit in a separate authorized checkout')
    patch = PATCH.read_text(encoding='utf-8')
    names = re.findall(r'^diff --git a/(.*?) b/.*$', patch, re.M)
    if len(names) != len(set(names)):
        raise ValueError('Duplicate patch target')
    for name in names:
        path = PurePosixPath(name)
        if path.is_absolute() or '..' in path.parts or not path.parts or path.parts[0] not in ALLOWED:
            raise ValueError('Invalid patch target')
        if path.suffix.lower() not in {'.c', '.cpp', '.h', '.hpp', '.inl', '.txt', '.lua', '.hlsl'}:
            raise ValueError('Only engine source and core scripts may be patched')
    payload = run_git(engine, 'cat-file', '--batch', data=('\n'.join(BASELINE + ':' + name for name in names) + '\n').encode())
    originals = {}
    offset = 0
    for name in names:
        end = payload.index(b'\n', offset)
        header = payload[offset:end].decode().split()
        if len(header) != 3 or header[1] != 'blob':
            raise ValueError('Missing baseline source')
        size = int(header[2])
        offset = end + 1
        originals[name] = normalize(payload[offset:offset + size])
        offset += size + 1
    outputs = {}
    with tempfile.TemporaryDirectory(prefix='bloxicon-patch-') as temporary:
        stage = Path(temporary)
        run_git(stage, 'init', '-q')
        run_git(stage, 'config', 'core.autocrlf', 'false')
        for name in names:
            original = originals[name]
            target = engine / name
            if target.is_symlink() or not target.resolve().is_relative_to(engine):
                raise ValueError('Patch target is not confined to the checkout')
            if not target.is_file() or normalize(target.read_bytes()) != original:
                raise ValueError('An engine patch target has existing changes: ' + name)
            # The export's diff is against this sanitized baseline. The removed
            # value is never included in the patch, output or command arguments.
            if name in SECRET_FILES:
                original, count = DECLARATION.subn(r'\1""', original)
                if count != 1:
                    raise ValueError('Unexpected legacy telemetry declaration')
            staged = stage / name
            staged.parent.mkdir(parents=True, exist_ok=True)
            staged.write_bytes(original.encode('utf-8'))
        run_git(stage, 'apply', '--check', '--unidiff-zero', str(PATCH))
        run_git(stage, 'apply', '--unidiff-zero', str(PATCH))
        outputs = {name: normalize((stage / name).read_bytes()).encode('utf-8') for name in names}
    if args.apply:
        for name, data in outputs.items():
            (engine / name).write_bytes(data)
    print(('Applied' if args.apply else 'Validated') + ' client engine patch: ' + str(len(outputs)) + ' source files')

if __name__ == '__main__':
    try:
        main()
    except (ValueError, subprocess.CalledProcessError) as error:
        # Git diagnostics can include context from private baseline files.
        raise SystemExit(str(error) if isinstance(error, ValueError) else 'Engine patch validation failed; no source diagnostics emitted')
