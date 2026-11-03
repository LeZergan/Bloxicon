"""Scan a source draft without emitting matched values."""
import argparse
import getpass
import json
from pathlib import Path
import re

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
parser.add_argument("--identity", action="append", default=[])
parser.add_argument("--forbidden-term", action="append", default=[])
args = parser.parse_args()
root = args.root.resolve()
identities = {x.casefold() for x in (getpass.getuser(), Path.home().name, *args.identity, *args.forbidden_term) if len(x) >= 3}
patterns = {
    "home-path": r"(?i)(?:[a-z]:[\\/]+(?:users|documents and settings)[\\/]|(?<![\w/-])/(?:home|Users)/[\w.-]+/)",
    "private-address": r"(?<![\d.])(?:192\.168|10\.\d{1,3}|172\.(?:1[6-9]|2\d|3[01])|169\.254)\.\d{1,3}\.\d{1,3}(?![\d.])",
    "private-key": r"-----BEGIN (?:\w+ )?PRIVATE KEY-----",
    "credential": r"(?i)\b(?:gh[pousr]_[\w]{25,}|github_pat_[\w]{35,}|AKIA[A-Z0-9]{16}|sk-(?:proj-)?[\w-]{25,})\b|https?://[^\s/:@]+:[^\s/@]+@|\bBearer [A-Za-z0-9_~+/=-]{24,}\b",
    "literal-credential": r"""(?ix)\b(?:password|passwd|access_token|client_secret|api_key)\s*["']?\s*[:=]\s*["'](?!test-only-|example-|your-)[^"'\r\n]{14,}["']""",
    "email": r"(?i)(?<![\w.+-])[\w.+-]{1,64}@(?!example\.(?:com|invalid|org)\b)[\w.-]+\.(?:com|net|org|io|me|ru|ae|dev|co|gg)\b",
}
excluded = {".git", "state", "userdata", "evidence", "node_modules", "__pycache__", "deploy"}
artifacts = {".key", ".vpk", ".pac", ".rbxl", ".rbxlx", ".elf", ".self", ".velf", ".pdb", ".log", ".dmp", ".psp2dmp", ".zip", ".exe"}
checker = Path(__file__).resolve()
issues = []
count = 0
for path in sorted(root.rglob("*")):
    rel = path.relative_to(root)
    name = rel.as_posix()
    if path.is_symlink() or excluded.intersection(rel.parts) or path.suffix.lower() in artifacts or path.name.startswith(".env") and path.name != ".env.example":
        issues.append({"file": name, "category": "excluded input"})
        continue
    if not path.is_file():
        continue
    count += 1
    try:
        contents = path.read_text(encoding="utf-8-sig")
        if "\0" in contents:
            raise ValueError
    except (UnicodeError, ValueError):
        issues.append({"file": name, "category": "nontext input"})
        continue
    if any(term in (contents + name).casefold() for term in identities):
        issues.append({"file": name, "category": "private identifier"})
    if path.resolve() == checker:
        continue
    for category, pattern in patterns.items():
        for match in re.finditer(pattern, contents):
            issues.append({"file": name, "line": contents.count("\n", 0, match.start()) + 1, "category": category})
print(json.dumps({"files": count, "issues": issues}, indent=2))
raise SystemExit(bool(issues))

