import sys, zlib, struct
def read_ppm(p):
    d=open(p,'rb').read()
    # parse P6 header
    parts=[]; i=0
    while len(parts)<4:
        while d[i:i+1].isspace(): i+=1
        if d[i:i+1]==b'#':
            while d[i:i+1] not in (b'\n',b''): i+=1
            continue
        j=i
        while not d[j:j+1].isspace(): j+=1
        parts.append(d[i:j]); i=j
    i+=1
    w=int(parts[1]); h=int(parts[2])
    return w,h,d[i:i+w*h*3]
def write_png(p,w,h,rgb):
    raw=b''.join(b'\x00'+rgb[y*w*3:(y+1)*w*3] for y in range(h))
    def chunk(t,data):
        c=struct.pack('>I',len(data))+t+data
        return c+struct.pack('>I',zlib.crc32(t+data)&0xffffffff)
    png=b'\x89PNG\r\n\x1a\n'
    png+=chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))
    png+=chunk(b'IDAT',zlib.compress(raw,6))
    png+=chunk(b'IEND',b'')
    open(p,'wb').write(png)
for src in sys.argv[1:]:
    w,h,rgb=read_ppm(src)
    dst=src.rsplit('.',1)[0]+'.png'
    write_png(dst,w,h,rgb)
    print(dst,w,h)
