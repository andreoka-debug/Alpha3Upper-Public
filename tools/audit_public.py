"""Fail-closed public source and package inventory. Does not read user caches."""
from pathlib import Path
import hashlib,json,re,sys
ROOT=Path(__file__).resolve().parents[1]
TOP={'.gitignore','README.md','LICENSE','THIRD-PARTY-NOTICES.md'}
FOLDERS={'src','tools','launcher','tests','docs','assets','server','.github'}
SUFFIXES={'.c','.h','.py','.js','.md','.txt','.json','.jsonc','.yml','.yaml','.svg','.ico'}
DENY={'.bin','.afs','.arc','.sngw','.wav','.ogg','.rom','.bmp','.dmp','.log','.csv','.zip','.exe','.dll','.a','.o','.pem','.key'}
SECRET=re.compile(rb'(?:github_pat_[A-Za-z0-9_]{20,}|gh[pousr]_[A-Za-z0-9]{25,}|-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----)')
def source_files():
    result=[]
    for p in ROOT.rglob('*'):
        if not p.is_file():continue
        rel=p.relative_to(ROOT)
        if rel.parts[0] in {'.git','build','dist'} or any(x in rel.parts for x in ['__pycache__','node_modules','.wrangler']):continue
        if rel.parts[:3]==('src','third-party','libdatachannel'):continue
        if len(rel.parts)==1:
            if rel.name not in TOP:raise ValueError('Unexpected top-level file: '+str(rel))
        elif rel.parts[0] not in FOLDERS:raise ValueError('Unexpected source directory: '+str(rel))
        if p.suffix.lower() in DENY or (p.suffix.lower() not in SUFFIXES and rel.name not in ['LICENSE','.gitignore']):raise ValueError('Forbidden source file: '+str(rel))
        if p.stat().st_size>3*1024*1024:raise ValueError('Oversized source file: '+str(rel))
        data=p.read_bytes()
        if SECRET.search(data):raise ValueError('Credential pattern detected: '+str(rel))
        if re.search(rb'[A-Za-z]:[\\/](?:Users|SteamLibrary)[\\/]',data):raise ValueError('Private machine path detected: '+str(rel))
        if p.suffix=='.ico' and rel.as_posix()!='assets/app.ico':raise ValueError('Unexpected artwork: '+str(rel))
        result.append(p)
    if not result:raise ValueError('Empty source inventory')
    return sorted(result)
def audit_dist():
    d=ROOT/'dist'
    if not d.exists():return []
    files=[]
    for p in d.rglob('*'):
        if not p.is_file():continue
        rel=p.relative_to(d)
        if len(rel.parts)==1 and rel.name not in ['Alpha3Upper.exe','Alpha3Upper-Core.exe','README.md','THIRD-PARTY-NOTICES.md']:raise ValueError('Unexpected release file: '+str(rel))
        if len(rel.parts)>1 and (rel.parts[0]!='licenses' or p.suffix.lower()!='.txt'):raise ValueError('Unexpected release payload: '+str(rel))
        if p.suffix.lower() in DENY and rel.name not in ['Alpha3Upper.exe','Alpha3Upper-Core.exe']:raise ValueError('Game data/binary in release: '+str(rel))
        files.append(p)
    return files
def main():
    source=source_files();dist=audit_dist();report={'source_file_count':len(source),'distribution_file_count':len(dist),'source':{p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in source},'distribution':{p.relative_to(ROOT/'dist').as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in dist},'proprietary_payload_allowed':False}
    (ROOT/'build').mkdir(exist_ok=True);(ROOT/'build/public-audit.json').write_text(json.dumps(report,indent=2));print('PUBLIC AUDIT PASS',len(source),'source files;',len(dist),'distribution files')
if __name__=='__main__':main()
