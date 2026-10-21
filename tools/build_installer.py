"""Build the public-only Windows installer with an existing Inno Setup 6 compiler."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess
from audit_public import ROOT,audit_dist,source_files
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--compiler',type=Path);parser.add_argument('--validation',action='store_true');args=parser.parse_args()
    compiler=args.compiler or Path(os.environ.get('INNO_ISCC',r'C:\Program Files (x86)\Inno Setup 6\ISCC.exe'))
    if not compiler.is_file():raise SystemExit('Install Inno Setup 6 from its official site, then pass --compiler PATH or set INNO_ISCC.')
    shutil.copy2(ROOT/'README.md',ROOT/'dist/README.md')
    source_files();payload=audit_dist()
    required={'Alpha3Upper.exe','Alpha3Upper-Core.exe','README.md','THIRD-PARTY-NOTICES.md'}
    if not all((ROOT/'dist'/n).is_file() for n in required):raise SystemExit('Build the public launcher and host first: python tools/build.py')
    output=ROOT/'build'/('installer-validation' if args.validation else 'installer');output.mkdir(parents=True,exist_ok=True)
    command=[str(compiler),'/DReleaseDir='+str(ROOT/'dist'),'/DOutputDir='+str(output),'/DIconFile='+str(ROOT/'assets/app.ico')]
    if args.validation:command+=['/DInstallerValidation']
    command+=[str(ROOT/'tools/installer.iss')];subprocess.run(command,check=True)
    setup=output/'Alpha3Upper-Public-Setup-v0.16.0-Windows-x64.exe'
    report={'installer':setup.name,'bytes':setup.stat().st_size,'sha256':hashlib.sha256(setup.read_bytes()).hexdigest(),'payload':{p.relative_to(ROOT/'dist').as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in payload},'original_game_data_included':False,'validation_build':args.validation}
    (output/'manifest.json').write_text(json.dumps(report,indent=2));print(json.dumps({k:v for k,v in report.items() if k!='payload'},indent=2))
if __name__=='__main__':main()
