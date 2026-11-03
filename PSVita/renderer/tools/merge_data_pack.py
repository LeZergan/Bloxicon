"""Merge immutable packs and optional loose content into one streaming base.pac.

Arguments are in mount order (last archive wins). Loose assets and personal
catalog overrides keep the production reader's precedence. Places, preferences,
saves, credentials and logs are never included. Inputs are never modified.
"""
from pathlib import Path
import argparse,hashlib,json,struct,tempfile
from runtime_pack import HEADER,ENTRY,ROOTS,MAX_ENTRIES,MAX_INDEX_BYTES,MAX_PACK_BYTES,verify,build_files

def digest(path):
    with Path(path).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()

def index(path):
    path=Path(path);verify(path);rows={}
    with path.open('rb') as f:
        magic,count,size=HEADER.unpack(f.read(16))
        for _ in range(count):
            if magic==b'RBXPAC02':length,offset,stored,size=ENTRY.unpack(f.read(16))
            else:length,offset,size=struct.unpack('<III',f.read(12));stored=size
            name=f.read(length).decode();rows[name]=(path,offset,stored,size)
    return rows

def merge(archives,output,loose=None,overlays=()):
    output=Path(output);sources=[];rows={};replaced=0
    for path in map(Path,archives):
        if path.resolve()==output.resolve():raise ValueError('Output cannot replace a source')
        entries=index(path);replaced+=len(rows.keys()&entries.keys());rows.update(entries)
        sources.append(dict(path=str(path.resolve()),sha256=digest(path),entries=len(entries)))
    output.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(dir=output.parent) as temporary:
        if loose:
            additions=[]
            for prefix in ROOTS:
                for path in sorted((Path(loose)/prefix).rglob('*')):
                    if path.is_symlink():raise ValueError('No symlinks in base data')
                    if not path.is_file():continue
                    name=path.relative_to(loose).as_posix()
                    override=name.startswith('assets/') or name in ('content/avatar/catalog/index.v1','content/avatar/catalog/testing.v1')
                    if name not in rows or override:additions.append((name,path))
            if additions:
                layer=Path(temporary)/'loose.pac';build_files(additions,layer);rows.update(index(layer))
        if overlays:
            layer=Path(temporary)/'updates.pac';build_files(overlays,layer);rows.update(index(layer))
        if not 0<len(rows)<=MAX_ENTRIES:raise ValueError('Combined entry limit exceeded')
        names=sorted(rows);size=sum(16+len(name.encode()) for name in names);offset=16+size
        if size>MAX_INDEX_BYTES or offset+sum(row[2] for row in rows.values())>MAX_PACK_BYTES:raise ValueError('Combined pack exceeds bounded size')
        staged=output.with_name(output.name+'.incoming')
        if staged.exists():raise ValueError('An unfinished output already exists')
        try:
            with staged.open('xb') as f:
                f.write(HEADER.pack(b'RBXPAC02',len(rows),size))
                for name in names:
                    path,start,stored,raw=rows[name];encoded=name.encode();f.write(ENTRY.pack(len(encoded),offset,stored,raw)+encoded);offset+=stored
                for name in names:
                    path,start,stored,raw=rows[name]
                    with path.open('rb') as source:
                        source.seek(start);remaining=stored
                        while remaining:
                            block=source.read(min(remaining,1024*1024))
                            if not block:raise ValueError('Source changed during merge')
                            f.write(block);remaining-=len(block)
            assert verify(staged)==len(rows)
            for source in sources:
                if digest(source['path'])!=source['sha256']:raise ValueError('Input archive changed')
            if output.exists():raise ValueError('Use a fresh output, never overwrite a released pack')
            staged.replace(output)
        except Exception:
            staged.unlink(missing_ok=True);raise
    return dict(entries=len(rows),bytes=output.stat().st_size,sha256=digest(output),sources=sources,replaced_virtual_entries=replaced,original_writes=0)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('output',type=Path);p.add_argument('archives',type=Path,nargs='+');p.add_argument('--loose',type=Path)
    a=p.parse_args();result=merge(a.archives,a.output,a.loose);a.output.with_suffix('.manifest.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
