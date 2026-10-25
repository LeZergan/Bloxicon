"""Read-only privacy gate for this source draft. Never prints matched values."""
from pathlib import Path
import argparse
import getpass
import json
import re

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
parser.add_argument('--identity', action='append', default=[], help='Additional private name/email/handle to reject')
parser.add_argument('--forbidden-term', action='append', default=[], help='Excluded name to reject without printing its value')
args = parser.parse_args()
root = args.root.resolve()
identities = {x for x in [getpass.getuser(), Path.home().name, *args.identity] if len(x) >= 3}
identity = re.compile(r'(?<![A-Za-z0-9_])(?:' + '|'.join(re.escape(x) for x in identities) + r')(?![A-Za-z0-9_])', re.I)
patterns = {
    'private-key': re.compile(r'-----BEGIN (?:OPENSSH |RSA |EC |DSA |ENCRYPTED )?PRIVATE KEY-----\s+[A-Za-z0-9+/=\s]{100,}'),
    'provider-token': re.compile(r'\b(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{40,}|AKIA[A-Z0-9]{16}|sk-(?:proj-)?[A-Za-z0-9_-]{30,})\b'),
    'credential-literal': re.compile(r'''(?ix)(?:\b(?:[A-Za-z_]\w*)?(?:password|passwd|api[_-]?key|access[_-]?token|auth[_-]?token|client[_-]?secret)\s*=|["'](?:[A-Za-z_]\w*)?(?:password|passwd|api[_-]?key|access[_-]?token|auth[_-]?token|client[_-]?secret)["']\s*:)\s*["']([^"'\r\n]{12,})["']'''),
    'workstation-path': re.compile(r'(?i)(?:[A-Z]:[\\/]+Users[\\/]+(?!Public\b|Default\b)[^\s"\'<>]+|/home/[A-Za-z0-9_.-]+/|/Users/[A-Za-z0-9_.-]+/)'),
}
forbidden = {'.git', 'access', 'accounts', 'node_modules', '__pycache__'}
suffixes = {'.key', '.vpk', '.pac', '.rbxl', '.rbxlx', '.elf', '.self', '.velf', '.pdb', '.log', '.dmp', '.psp2dmp'}
issues = []
count = 0
for path in sorted(root.rglob('*')):
    rel = path.relative_to(root)
    if rel.parts[0] == '.git':
        continue
    if path.is_symlink():
        issues.append({'file': rel.as_posix(), 'category': 'symlink'})
        continue
    if any(x in forbidden for x in rel.parts) or path.suffix.lower() in suffixes or path.name.startswith('.env') and path.name != '.env.example':
        issues.append({'file': rel.as_posix(), 'category': 'private-or-generated-input'})
        continue
    if not path.is_file():
        continue
    count += 1
    data = path.read_bytes()
    if b'\0' in data:
        issues.append({'file': rel.as_posix(), 'category': 'binary-input'})
        continue
    text = data.decode('utf8', 'replace')
    if any(term.casefold() in text.casefold() or term.casefold() in rel.as_posix().casefold() for term in args.forbidden_term if term):
        issues.append({'file': rel.as_posix(), 'category': 'excluded-name'})
    for category, pattern in [*patterns.items(), ('personal-identity', identity)]:
        for match in pattern.finditer(text):
            # Deliberately public, nonfunctional authentication test fixture.
            if category == 'credential-literal' and match.group(1).startswith('test-only-') and '/tests/' in '/' + rel.as_posix():
                continue
            issues.append({'file': rel.as_posix(), 'line': text.count('\n', 0, match.start()) + 1, 'category': category})
    if identity.search(rel.as_posix()):
        issues.append({'file': rel.as_posix(), 'category': 'personal-file-name'})
print(json.dumps({'files': count, 'issues': issues, 'scope': 'Selected draft only; Git history and excluded private inputs are not publication inputs'}, indent=2))
raise SystemExit(bool(issues))
