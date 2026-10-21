"""Reproducible host/launcher build. No original game is needed or read."""
from pathlib import Path
import argparse,os,shutil,subprocess,sys,urllib.request,importlib.metadata
ROOT=Path(__file__).resolve().parents[1]
COMMIT='4e4f4892dccb2a57fe3a490d0c9d958de4244e74'
def run(args,**kw):subprocess.run([str(a) for a in args],check=True,**kw)
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--core-only',action='store_true');args=parser.parse_args()
    os.chdir(ROOT);out=ROOT/'dist';build=ROOT/'build';out.mkdir(exist_ok=True);build.mkdir(exist_ok=True)
    mingw=Path(os.environ.get('MINGW_BIN','C:/msys64/ucrt64/bin'));os.environ['PATH']=str(mingw)+os.pathsep+os.environ['PATH']
    dep=Path(os.environ.get('UPPER_WEBRTC_SOURCE',ROOT/'src/third-party/libdatachannel')).resolve()
    db=Path(os.environ.get('UPPER_WEBRTC_BUILD',build/'webrtc')).resolve()
    if not dep.exists():
        run(['git','clone','https://github.com/paullouisageneau/libdatachannel.git',dep]);run(['git','-C',dep,'checkout','--detach',COMMIT]);run(['git','-C',dep,'submodule','update','--init','--recursive'])
    actual=subprocess.check_output(['git','-C',str(dep),'rev-parse','HEAD'],text=True).strip()
    if actual!=COMMIT:raise RuntimeError('Unexpected WebRTC revision: '+actual)
    if not(db/'libdatachannel-static.a').is_file():
        run(['cmake','-S',dep,'-B',db,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_MAKE_PROGRAM='+str(mingw/'ninja.exe'),'-DCMAKE_C_COMPILER='+str(mingw/'gcc.exe'),'-DCMAKE_CXX_COMPILER='+str(mingw/'g++.exe'),'-DNO_MEDIA=ON','-DNO_WEBSOCKET=ON','-DNO_EXAMPLES=ON','-DNO_TESTS=ON','-DBUILD_SHARED_LIBS=OFF','-DOPENSSL_USE_STATIC_LIBS=ON','-DOPENSSL_ROOT_DIR='+str(mingw.parent)])
        run(['cmake','--build',db,'--target','datachannel-static','--parallel','4'])
    run([sys.executable,ROOT/'tools/make_icon.py'])
    rc=build/'app.rc';rc.write_text('1 ICON "'+(ROOT/'assets/app.ico').as_posix()+'"\n1 VERSIONINFO\n FILEVERSION 0,16,0,0\n PRODUCTVERSION 0,16,0,0\n BEGIN\n BLOCK "StringFileInfo"\n BEGIN\n BLOCK "040904b0"\n BEGIN\n VALUE "FileDescription", "Alpha3Upper Public host"\n VALUE "FileVersion", "0.16.0"\n END\n END\n END\n')
    run([mingw/'windres.exe',rc,'-o',build/'app.o'])
    flags=['-O2','-DUPPER_COLD_BOOT','-DUPPER_ONLINE','-DUPPER_P2P','-DUPPER_ROLLBACK','-DUPPER_AUDIO','-DCAPTURE_TEXTURES','-DUPPER_WINDOW','-I'+str(dep/'include')]
    run([mingw/'gcc.exe',*flags,'-c',ROOT/'src/upper_core_probe.c','-o',build/'host.o'])
    libs=[]
    for name in ['libdatachannel-static.a','libusrsctp.a','libjuice-static.a']:
        matches=list(db.rglob(name))
        if len(matches)!=1:raise RuntimeError('Missing or ambiguous '+name)
        libs+=matches
    run([mingw/'g++.exe','-static','-s','-mwindows','-Wl,--image-base,0x180000000',build/'host.o',build/'app.o',*libs,'-lssl','-lcrypto','-lws2_32','-liphlpapi','-lbcrypt','-lwinhttp','-lcrypt32','-lncrypt','-ladvapi32','-lgdi32','-luser32','-lwinmm','-lmsacm32','-lm','-o',out/'Alpha3Upper-Core.exe'])
    if not args.core_only:run([sys.executable,'-m','PyInstaller','--noconfirm','--onefile','--windowed','--noupx','--name','Alpha3Upper','--icon',ROOT/'assets/app.ico','--distpath',out,'--workpath',build/'launcher','--specpath',build,ROOT/'launcher/app.py'])
    notices=out/'licenses';notices.mkdir(exist_ok=True)
    for source,label in [(dep/'LICENSE','libdatachannel'),(dep/'deps/libjuice/LICENSE','libjuice'),(dep/'deps/usrsctp/LICENSE.md','usrsctp'),(dep/'deps/plog/LICENSE','plog')]:
        if source.exists():shutil.copy2(source,notices/(label+'.txt'))
    shutil.copy2(ROOT/'LICENSE',notices/'host-MIT.txt');shutil.copy2(ROOT/'THIRD-PARTY-NOTICES.md',out/'THIRD-PARTY-NOTICES.md')
    ssl_license=mingw.parent/'share/licenses/openssl/LICENSE'
    if not ssl_license.is_file():raise RuntimeError('OpenSSL license not found')
    shutil.copy2(ssl_license,notices/'OpenSSL.txt')
    if not args.core_only:
        shutil.copy2(Path(sys.base_prefix)/'LICENSE.txt',notices/'Python.txt')
        package=importlib.metadata.distribution('pyinstaller');license_file=next(f for f in package.files if str(f).endswith('/licenses/COPYING.txt'));shutil.copy2(package.locate_file(license_file),notices/'PyInstaller.txt')
        for project in ['tcl','tk']:
            target=notices/(project+'.txt')
            if not target.exists():
                url='https://raw.githubusercontent.com/tcltk/'+project+'/core-8-6-branch/license.terms'
                with urllib.request.urlopen(url,timeout=20) as response:target.write_bytes(response.read())
    shutil.copy2(ROOT/'README.md',out/'README.md')
    print('PUBLIC BUILD COMPLETE:',out)
if __name__=='__main__':main()
