"""Generate an original geometric A3 icon; no game artwork is used."""
from pathlib import Path
import struct,zlib
def png(n):
    glyphs=['01110'+'10001'+'10001'+'11111'+'10001'+'10001'+'10001','11110'+'00001'+'00001'+'01110'+'00001'+'00001'+'11110']
    raw=bytearray();scale=n/16
    for y in range(n):
        raw.append(0)
        for x in range(n):
            gx=int(x/scale)-2;gy=int(y/scale)-4;on=False
            if 0<=gy<7:
                for i,g in enumerate(glyphs):
                    xx=gx-i*7
                    if 0<=xx<5 and g[gy*5+xx]=='1':on=True
            raw+=bytes((245,248,255,255) if on else (32,71,120,255) if y<n*.85 else (231,116,48,255))
    def chunk(t,b):return struct.pack('>I',len(b))+t+b+struct.pack('>I',zlib.crc32(t+b))
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>2I5B',n,n,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(bytes(raw),9))+chunk(b'IEND',b'')
def main():
    out=Path(__file__).resolve().parents[1]/'assets';out.mkdir(exist_ok=True);sizes=[16,24,32,48,64,128,256];images=[png(n) for n in sizes];offset=6+16*len(sizes);entries=[]
    for n,b in zip(sizes,images):entries.append(struct.pack('<4B2H2I',n%256,n%256,0,0,1,32,len(b),offset));offset+=len(b)
    (out/'app.ico').write_bytes(struct.pack('<3H',0,1,len(sizes))+b''.join(entries)+b''.join(images))
    (out/'app.svg').write_text('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 128 128"><rect width="128" height="128" rx="14" fill="#204778"/><path fill="#e77430" d="M0 109h128v19H0z"/><text x="14" y="87" fill="#f5f8ff" font-family="sans-serif" font-weight="bold" font-size="68">A3</text></svg>')
if __name__=='__main__':main()
