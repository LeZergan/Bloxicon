# SPDX-License-Identifier: GPL-3.0-only
"""Check client-only source and reachable history without printing matched values."""
import argparse
import getpass
import hashlib
import ipaddress
import json
import os
from pathlib import Path
import re
import subprocess

PATTERNS = {
    'home-path': r'(?i)(?:[a-z]:[\\/]+(?:users|documents and settings)[\\/]|(?<![\w/-])/(?:home|Users)/[\w.-]+/)',
    'private-key': r'-----BEGIN (?:[A-Z]+ )?PRIVATE KEY-----',
    'provider-credential': r'\b(?:gh[pousr]_[\w]{25,}|github_pat_[\w]{35,}|AKIA[A-Z0-9]{16}|sk-(?:proj-)?[\w-]{25,})\b|https?://[^\s/:@]+:[^\s/@]+@|\bBearer [A-Za-z0-9_~+/=-]{24,}\b',
    'literal-credential': r'''(?ix)\b(?:password|passwd|access_token|auth_token|client_secret|api_key|countersApiKey)\s*["']?\s*[:=]\s*["'](?!test-only-|example-|your-)[^"'\r\n]{8,}["']''',
    'email': r'(?i)(?<![\w.+-])[\w.+-]{1,64}@(?!example\.(?:com|invalid|org)\b)[\w.-]+\.(?:com|net|org|io|me|ru|ae|dev|co|gg)\b',
}
IPV4 = re.compile(r'(?<![\w.])(?:\d{1,3}\.){3}\d{1,3}(?![\w.])')
DOC_NETS = ()
FORBIDDEN = {'access', 'private', 'private-release', 'pc-server', 'WindowsPCClient', 'accounts', 'state', 'userdata', 'evidence', 'benchmarks', 'deploy', 'node_modules', '__pycache__'}
ARTIFACTS = {'.key', '.pem', '.pfx', '.p12', '.vpk', '.pac', '.rbxl', '.rbxlx', '.rbxm', '.rbxmx', '.elf', '.self', '.velf', '.pdb', '.log', '.dmp', '.psp2dmp', '.zip', '.exe', '.db', '.sqlite', '.sqlite3', '.csv', '.har', '.bak'}
TOP_LEVEL = {'App', 'Base', 'Network', 'Rendering', 'PSVita', 'patches', 'docs', 'tools', '.github'}
ROOT_FILES = {'.gitignore', '.gitattributes', '.gitleaks.toml', 'README.md', 'LICENSE', 'NOTICE', 'THIRD_PARTY_NOTICES.md'}
BRANDING = {
    'PSVita/assets/branding/bloxicon_logo.png': 'b50abd41f9da3843315061451abbaaeb4bb43719a7fa206d212c101eafa5bdb9',
    'PSVita/assets/branding/sce_sys/icon0.png': 'c35182117876778d9d9a307e4e47a1e847aca6b1e0a5b09153fc2f9e7af4ffa1',
    'PSVita/assets/branding/sce_sys/pic0.png': 'ef113dd58a247012390c85fea79238091dce9955905e7cf06a430282bbf293b0',
    'PSVita/assets/branding/sce_sys/livearea/contents/bg.png': '1f11acec7a80f0ce53b583d10b86646adba3cabf2fc401ca295aa94819fcc7a7',
    'PSVita/assets/branding/sce_sys/livearea/contents/startup.png': '53bb6f3cfe1ee4c7a6592c19c68e4ddcd224a8571a7e9865ba8b6b1acfc91442',
}

def inspect(name, data, identities, issues, history=False):
    parts = Path(name).parts
    scope = 'history' if history else 'working-tree'
    def report(category, line=None):
        item = {'file': name, 'category': category, 'scope': scope}
        if line is not None:
            item['line'] = line
        issues.append(item)
    if (not parts or parts[0] not in TOP_LEVEL and name not in ROOT_FILES or FORBIDDEN.intersection(parts) or
            Path(name).suffix.lower() in ARTIFACTS or Path(name).name.startswith('.env') or name == 'PSVita/assets/site.json'):
        report('excluded-input')
    if name in BRANDING:
        if hashlib.sha256(data).hexdigest() != BRANDING[name]:
            report('unreviewed-branding')
        return
    try:
        text = data.decode('utf-8-sig')
        if '\0' in text:
            raise ValueError
    except (UnicodeError, ValueError):
        report('nontext-input')
        return
    if any(term in (name + '\n' + text).casefold() for term in identities):
        report('private-identifier')
    for category, pattern in PATTERNS.items():
        for match in re.finditer(pattern, text):
            report(category, text.count('\n', 0, match.start()) + 1)
    for match in IPV4.finditer(text):
        try:
            address = ipaddress.ip_address(match.group())
        except ValueError:
            continue
        if not address.is_loopback and not address.is_unspecified and not any(address in net for net in DOC_NETS):
            report('embedded-network-address', text.count('\n', 0, match.start()) + 1)

def git(root, *args, data=None):
    return subprocess.check_output(['git', '-C', str(root), *args], input=data, stderr=subprocess.PIPE)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--identity', action='append', default=[])
    parser.add_argument('--forbidden-term', action='append', default=[])
    parser.add_argument('--history', action='store_true')
    args = parser.parse_args()
    root = args.root.resolve()
    service_users = {'runner', 'root', 'administrator', 'system', 'default', 'public'}
    local_names = [x for x in (getpass.getuser(), Path.home().name) if x.casefold() not in service_users]
    identities = {x.casefold() for x in (*local_names, *args.identity, *args.forbidden_term) if len(x) >= 3}
    issues = []
    count = 0
    for directory, dirs, files in os.walk(root, followlinks=False):
        dirs[:] = [d for d in dirs if d != '.git']
        for d in dirs[:]:
            if (Path(directory) / d).is_symlink():
                issues.append({'file': (Path(directory) / d).relative_to(root).as_posix(), 'category': 'symlink'})
                dirs.remove(d)
        for filename in files:
            path = Path(directory) / filename
            name = path.relative_to(root).as_posix()
            if name == '.git':
                continue
            if path.is_symlink():
                issues.append({'file': name, 'category': 'symlink'})
                continue
            inspect(name, path.read_bytes(), identities, issues)
            count += 1
    blobs = 0
    if args.history:
        objects = git(root, 'rev-list', '--objects', '--all').decode().splitlines()
        names = {line.split(' ', 1)[0]: line.split(' ', 1)[1] for line in objects if ' ' in line}
        payload = git(root, 'cat-file', '--batch', data=('\n'.join(names) + '\n').encode()) if names else b''
        offset = 0
        while offset < len(payload):
            end = payload.index(b'\n', offset)
            oid, kind, size = payload[offset:end].decode().split()
            offset = end + 1
            data = payload[offset:offset + int(size)]
            offset += int(size) + 1
            if kind == 'blob':
                inspect(names[oid], data, identities, issues, history=True)
                blobs += 1
        metadata = git(root, 'log', '--all', '--format=%an%n%ae%n%cn%n%ce%n%B').decode()
        for line in metadata.splitlines():
            if '@' in line and not line.endswith(('@users.noreply.github.com', '@bloxicon.invalid')):
                issues.append({'file': '(commit metadata)', 'category': 'nonproject-email', 'scope': 'history'})
        if any(term in metadata.casefold() for term in identities):
            issues.append({'file': '(commit metadata)', 'category': 'private-identifier', 'scope': 'history'})
    print(json.dumps({'files': count, 'history_blobs': blobs, 'issues': issues}, indent=2))
    return bool(issues)

if __name__ == '__main__':
    raise SystemExit(main())
