"""Local-only import from a supported, user-selected original installation.

No network requests, original process writes, suspension or installation writes.
The original game must be started normally by its owner for first-time code import.
"""
from pathlib import Path, PureWindowsPath
import ctypes as C
from ctypes import wintypes as W
import hashlib, json, math, os, re, struct, tempfile, zlib

EXE = 'CapcomFightingCollection2.exe'
EXE_SHA = 'ba1e089e56ca8296ec084c40c190298ee6dc8dcdd34d0b648e50a9c5b88337a8'
TEXT_SHA = '741e1df4f1a3e70a45b0a08655f3617dcc42845802bb9c9ed98d11a350472c14'
IMAGE_BASE, IMAGE_SIZE = 0x140000000, 66387968
AFS_SHA = '7426d9d7bd9211c25d06c83835fc602053170010602af512268f4d0818e4e337'
ARCHIVES = {'game_32.arc':'b3f8969c58bc8a07f6a8b0576abb014985c065142965acc5eedd9f837d4ddd61',
            'game_3s.arc':'99a3133056d1d89ce218342b7ff1301e4d1b6912822ccb9df43a3b43c918647f',
            'game_3t.arc':'d5020bb91d23ce1651ac7512dbd9ec299dbf493fcb9832eadf163ca6e5aeffaf'}

class ImportErrorDetail(RuntimeError): pass

def sha_file(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as f:
        for block in iter(lambda:f.read(1024*1024),b''):h.update(block)
    return h.hexdigest()

def validate_installation(path):
    root=Path(path).resolve()
    if not (root/EXE).is_file():raise ImportErrorDetail('Selecione a pasta que contém '+EXE)
    if sha_file(root/EXE)!=EXE_SHA:raise ImportErrorDetail('Versão do executável ainda não suportada. A importação foi interrompida; nenhum arquivo original foi alterado.')
    for name,digest in ARCHIVES.items():
        p=root/'nativeDX11x64/arc/pc'/name
        if not p.is_file() or sha_file(p)!=digest:raise ImportErrorDetail('Arquivo ausente ou versão não suportada: '+name)
    return root

def records(path):
    """Bounded ARC v7 decoding; internal paths never become output paths."""
    with Path(path).open('rb') as f:
        size=os.fstat(f.fileno()).st_size;header=f.read(8)
        if len(header)!=8 or header[:4]!=b'ARC\0':raise ImportErrorDetail('Cabeçalho ARC inválido.')
        version,count=struct.unpack_from('<HH',header,4)
        if version!=7 or not 1<=count<=16384 or 8+80*count>size:raise ImportErrorDetail('Tabela ARC inválida.')
        rows=[f.read(80) for _ in range(count)]
        for row in rows:
            name=row[:64].split(b'\0',1)[0].decode('ascii');parts=PureWindowsPath(name)
            if parts.is_absolute() or parts.drive or '..' in parts.parts:raise ImportErrorDetail('Caminho interno inválido.')
            kind,packed,flags,offset=struct.unpack_from('<4I',row,64);expected=flags&0x3fffffff
            if expected>160*1024*1024 or packed>160*1024*1024 or offset<8+80*count or offset+packed>size:raise ImportErrorDetail('Limites do recurso ARC inválidos.')
            f.seek(offset);stream=zlib.decompressobj();data=stream.decompress(f.read(packed),expected+1)
            if len(data)!=expected or not stream.eof or stream.unused_data or stream.unconsumed_tail:raise ImportErrorDetail('Recurso ARC truncado ou inválido.')
            yield name,kind,data

def safe_name(s):
    if not re.fullmatch(r'[A-Za-z0-9_\-]+',s):raise ImportErrorDetail('Nome de áudio inválido.')
    return s

def extract_assets(installation, output, notify=lambda s:None):
    root=validate_installation(installation);out=Path(output);out.mkdir(parents=True,exist_ok=False)
    notify('Importando gráficos e recursos do Alpha 3 Upper...')
    rows=list(records(root/'nativeDX11x64/arc/pc/game_32.arc'))
    if len(rows)!=1 or rows[0][:2]!=('bin\\z3uu',0x21d3d8a7):raise ImportErrorDetail('Recurso Upper não encontrado.')
    body=rows[0][2]
    if body[:4]!=b'IBIS' or len(body)<16:raise ImportErrorDetail('Cabeçalho IBIS inválido.')
    base,size=struct.unpack_from('<II',body,8);afs=body[base:base+size]
    if base+size!=len(body) or afs[:4]!=b'AFS\0' or hashlib.sha256(afs).hexdigest()!=AFS_SHA:raise ImportErrorDetail('Recurso AFS não suportado.')
    (out/'arena.bin').write_bytes(afs);del rows,body,afs
    notify('Importando efeitos e vozes...')
    resources=[r for archive in ['game_3s.arc','game_3t.arc'] for r in records(root/'nativeDX11x64/arc/pc'/archive)]
    waves={name:data for name,kind,data in resources if data[:4]==b'RIFF' and data[8:12]==b'WAVE'};banks={};music_blob=None
    for name,kind,data in resources:
        if kind in [0x15d782fb,0x1bcc4966]:banks.setdefault(name,{})[kind]=data
        if kind==0x167dbbff:music_blob=data
    manifest=[]
    for name,parts in banks.items():
        if not all(k in parts for k in [0x15d782fb,0x1bcc4966]):continue
        label=safe_name(name.split('\\')[-2]);bank=48 if label=='comn' else 49 if label=='NA' else int(label[2:],16)
        sb,sq=parts[0x15d782fb],parts[0x1bcc4966];paths=[x.group().decode('ascii') for x in re.finditer(rb'sound\\[^\x00]+',sb)]
        if len(paths)!=struct.unpack_from('<I',sb,12)[0]:raise ImportErrorDetail('Banco de áudio inválido.')
        count=struct.unpack_from('<I',sq,8)[0]
        if 72+count*144>len(sq):raise ImportErrorDetail('Tabela de efeitos truncada.')
        for i in range(count):
            rec=sq[72+i*144:72+(i+1)*144];rid=struct.unpack_from('<H',rec)[0];wi=rec[2]
            if wi>=len(paths) or paths[wi] not in waves:continue
            filename=safe_name(paths[wi].split('\\')[-1]);rel=Path('audio/se')/label/'wav'/(filename+'.wav');target=out/rel;target.parent.mkdir(parents=True,exist_ok=True)
            if not target.exists():target.write_bytes(waves[paths[wi]])
            gain=10**(struct.unpack_from('<f',rec,0x24)[0]/20)
            if not math.isfinite(gain) or not 0<=gain<=16:raise ImportErrorDetail('Ganho de efeito inválido.')
            manifest.append(f'S\t{bank}\t{rid}\t{gain:.8f}\tdata/{rel.as_posix()}')
    notify('Importando músicas...')
    if music_blob is None:raise ImportErrorDetail('Tabela de música ausente.')
    b=music_blob;stream_count,request_count=struct.unpack_from('<II',b,8);stream_base,request_base=struct.unpack_from('<QQ',b,32)
    if stream_count>4096 or request_count>4096 or stream_base+stream_count*40>len(b) or request_base+request_count*152>len(b):raise ImportErrorDetail('Tabela de música inválida.')
    for i in range(request_count):
        rec=b[request_base+i*152:request_base+(i+1)*152];rid=struct.unpack_from('<I',rec)[0];si=struct.unpack_from('<I',rec,0x5c)[0]
        if not 0<si<stream_count:continue
        sr=b[stream_base+si*40:stream_base+(si+1)*40];offset=struct.unpack_from('<Q',sr)[0]
        if offset>=len(b) or b.find(b'\0',offset)<0:raise ImportErrorDetail('Nome de música inválido.')
        name=b[offset:b.find(b'\0',offset)].decode('ascii');filename=safe_name(name.split('\\')[-1]);src=root/'nativeDX11x64/sound/bgm/zero3/wav'/(filename+'.sngw')
        data=src.read_bytes()
        if not data.startswith(b'OggS') or len(data)>64*1024*1024:raise ImportErrorDetail('Música não suportada: '+filename)
        rel=Path('audio/bgm')/(filename+'.ogg');target=out/rel;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
        ls,le=struct.unpack_from('<II',sr,24);gain=10**(struct.unpack_from('<f',rec,0x24)[0]/20)
        if not math.isfinite(gain) or not 0<=gain<=16:raise ImportErrorDetail('Ganho de música inválido.')
        manifest.append(f'M\t{rid}\t{ls}\t{le}\t{gain:.8f}\tdata/{rel.as_posix()}')
    (out/'audio-map.tsv').write_text('\n'.join(manifest)+'\n',encoding='ascii')
    return root

def capture_live(installation):
    """Read only the main executable image of the matching original process."""
    if os.name!='nt':raise ImportErrorDetail('A importação de código requer Windows x64.')
    root=validate_installation(installation);k=C.WinDLL('kernel32',use_last_error=True);ps=C.WinDLL('psapi',use_last_error=True);size=C.c_size_t
    k.OpenProcess.argtypes=[W.DWORD,W.BOOL,W.DWORD];k.OpenProcess.restype=W.HANDLE;k.CloseHandle.argtypes=[W.HANDLE]
    k.ReadProcessMemory.argtypes=[W.HANDLE,C.c_void_p,C.c_void_p,size,C.POINTER(size)];k.ReadProcessMemory.restype=W.BOOL
    class MBI(C.Structure):_fields_=[('BaseAddress',C.c_void_p),('AllocationBase',C.c_void_p),('AllocationProtect',W.DWORD),('PartitionId',W.WORD),('RegionSize',size),('State',W.DWORD),('Protect',W.DWORD),('Type',W.DWORD)]
    class MODULEINFO(C.Structure):_fields_=[('base',C.c_void_p),('size',W.DWORD),('entry',C.c_void_p)]
    k.VirtualQueryEx.argtypes=[W.HANDLE,C.c_void_p,C.POINTER(MBI),size];k.VirtualQueryEx.restype=size
    ps.EnumProcesses.argtypes=[C.POINTER(W.DWORD),W.DWORD,C.POINTER(W.DWORD)]
    ps.EnumProcessModulesEx.argtypes=[W.HANDLE,C.POINTER(W.HMODULE),W.DWORD,C.POINTER(W.DWORD),W.DWORD]
    ps.GetModuleFileNameExW.argtypes=[W.HANDLE,W.HMODULE,W.LPWSTR,W.DWORD]
    ps.GetModuleInformation.argtypes=[W.HANDLE,W.HMODULE,C.POINTER(MODULEINFO),W.DWORD]
    ids=(W.DWORD*8192)();needed=W.DWORD()
    if not ps.EnumProcesses(ids,C.sizeof(ids),C.byref(needed)):raise ImportErrorDetail('Não foi possível consultar o processo original.')
    for pid in ids[:min(len(ids),needed.value//4)]:
        h=k.OpenProcess(0x410,False,pid)  # QUERY_INFORMATION | VM_READ only
        if not h:continue
        try:
            mods=(W.HMODULE*2048)();n=W.DWORD()
            if not ps.EnumProcessModulesEx(h,mods,C.sizeof(mods),C.byref(n),3):continue
            name=C.create_unicode_buffer(32768)
            if not ps.GetModuleFileNameExW(h,mods[0],name,len(name)):continue
            if Path(name.value).resolve()!=root/EXE:continue
            info=MODULEINFO()
            if not ps.GetModuleInformation(h,mods[0],C.byref(info),C.sizeof(info)) or info.base!=IMAGE_BASE or info.size!=IMAGE_SIZE:raise ImportErrorDetail('Endereço ou tamanho de código não suportado.')
            image=bytearray(IMAGE_SIZE);address=IMAGE_BASE
            while address<IMAGE_BASE+IMAGE_SIZE:
                m=MBI()
                if not k.VirtualQueryEx(h,address,C.byref(m),C.sizeof(m)):raise ImportErrorDetail('Não foi possível ler o módulo original.')
                end=min(m.BaseAddress+m.RegionSize,IMAGE_BASE+IMAGE_SIZE)
                if end<=address:raise ImportErrorDetail('Mapa de memória inválido.')
                if m.State==0x1000 and not m.Protect&(0x100|1) and m.Protect&0xff in (2,4,8,0x20,0x40,0x80):
                    buf=C.create_string_buffer(end-address);got=size()
                    if not k.ReadProcessMemory(h,address,buf,len(buf),C.byref(got)) or got.value!=len(buf):raise ImportErrorDetail('Leitura incompleta. Mantenha o original aberto no Alpha 3 Upper.')
                    image[address-IMAGE_BASE:end-IMAGE_BASE]=buf.raw
                address=end
            if hashlib.sha256(image[0x1000:0xfdc000]).hexdigest()!=TEXT_SHA:raise ImportErrorDetail('Código carregado diferente da versão suportada.')
            context=struct.unpack_from('<Q',image,0x1c14f98)[0]
            if context!=0x141c14fa0 or struct.unpack_from('<I',image,context-IMAGE_BASE+0x2a4)[0]!=2:raise ImportErrorDetail('Entre em uma partida ou treino do Alpha 3 Upper no jogo original e tente novamente.')
            return bytes(image)
        finally:k.CloseHandle(h)
    raise ImportErrorDetail('Abra normalmente a coletânea pela Steam, entre no Alpha 3 Upper e mantenha uma partida ou treino aberto. Depois clique em Importar novamente.')

def write_manifest(data):
    p=Path(data);files={f.relative_to(p).as_posix():sha_file(f) for f in p.rglob('*') if f.is_file() and f.name not in ['import.json','settings.ini']}
    (p/'import.json').write_text(json.dumps({'schema':1,'version':'0.16.0','exe_sha256':EXE_SHA,'text_sha256':TEXT_SHA,'files':files},indent=2),encoding='utf-8')

def verify_import(data):
    p=Path(data)
    try:
        m=json.loads((p/'import.json').read_text());files=m['files']
        if m['schema']!=1 or m['text_sha256']!=TEXT_SHA or not all(n in files for n in ['main.bin','arena.bin','audio-map.tsv']):return False
        for name,digest in files.items():
            target=(p/name).resolve()
            if not target.is_relative_to(p.resolve()) or not target.is_file() or sha_file(target)!=digest:return False
        return True
    except (OSError,KeyError,ValueError,TypeError):return False
