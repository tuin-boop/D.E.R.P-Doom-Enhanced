"""Build a tiny colored-wall room for repeatable flashlight render comparisons.
Usage: python3 tests/flashlight_scene.py base.wad output.wad
Uses the base game's palette and preserves its texture definitions.
"""
import math
import struct
import sys
from pathlib import Path

def name(value):
    return value.encode('ascii').ljust(8,b'\0')

def create(base_path,output_path):
    base=Path(base_path).read_bytes()
    count,directory=struct.unpack_from('<ii',base,4)
    lumps={}
    for i in range(count):
        offset,size,label=struct.unpack_from('<ii8s',base,directory+i*16)
        lumps[label.rstrip(b'\0').decode('ascii')]=base[offset:offset+size]
    palette=lumps['PLAYPAL'][:768]
    def closest(rgb):
        return min(range(256),key=lambda i:sum((palette[i*3+c]-rgb[c])**2 for c in range(3)))
    green,gray=closest((40,180,40)),closest((120,120,120))
    def patch(index):
        column=bytes([0,64,0])+bytes([index])*64+bytes([0,255])
        return struct.pack('<hhhh',64,64,0,0)+b''.join(struct.pack('<i',264+x*len(column)) for x in range(64))+column*64
    pn=lumps['PNAMES'];np=struct.unpack_from('<i',pn)[0]
    texture=lumps['TEXTURE1'];nt=struct.unpack_from('<i',texture)[0]
    offsets=struct.unpack_from('<'+'i'*nt,texture,4)
    entries=[]
    for offset in offsets:
        patches=struct.unpack_from('<h',texture,offset+20)[0]
        entries.append(texture[offset:offset+22+10*patches])
    for label,index in [('BOUNCEG',np),('BOUNCEW',np+1)]:
        entries.append(name(label)+struct.pack('<ihhih',0,64,64,0,1)+struct.pack('<hhhhh',0,0,index,1,0))
    offset=4+4*len(entries);new_offsets=[]
    for entry in entries:
        new_offsets.append(offset);offset+=len(entry)
    new_texture=struct.pack('<i',len(entries))+struct.pack('<'+'i'*len(entries),*new_offsets)+b''.join(entries)
    points=[(-128,-128),(-128,128),(128,128),(128,-128)]
    vertices=b''.join(struct.pack('<hh',*p) for p in points)
    lines=b''.join(struct.pack('<hhhhhhh',i,(i+1)%4,1,0,0,i,-1) for i in range(4))
    sides=b''.join(struct.pack('<hh',0,0)+name('-')+name('-')+name('BOUNCEG' if i==1 else 'BOUNCEW')+struct.pack('<h',0) for i in range(4))
    segs=b''.join(struct.pack('<HHHHHH',i,(i+1)%4,int(math.atan2(points[(i+1)%4][1]-points[i][1],points[(i+1)%4][0]-points[i][0])*65536/(2*math.pi))%65536,i,0,0) for i in range(4))
    # One block spans the room, with all four lines in its collision list.
    blockmap=struct.pack('<hhhhhhhhhhh',-128,-128,1,1,5,0,0,1,2,3,-1)
    flat='FLOOR0_1'
    result=[('PNAMES',struct.pack('<i',np+2)+pn[4:]+name('BGREEN')+name('BGRAY')),('TEXTURE1',new_texture),('BGREEN',patch(green)),('BGRAY',patch(gray)),('E1M1',b''),('THINGS',struct.pack('<hhhhh',0,0,90,1,7)),('LINEDEFS',lines),('SIDEDEFS',sides),('VERTEXES',vertices),('SEGS',segs),('SSECTORS',struct.pack('<hh',4,0)),('NODES',b''),('SECTORS',struct.pack('<hh',0,128)+name(flat)+name(flat)+struct.pack('<hhh',32,0,0)),('REJECT',b'\0'),('BLOCKMAP',blockmap)]
    payload=b'';directory_data=b''
    for label,data in result:
        directory_data+=struct.pack('<ii8s',12+len(payload),len(data),name(label));payload+=data
    Path(output_path).write_bytes(b'PWAD'+struct.pack('<ii',len(result),12+len(payload))+payload+directory_data)
    print(f'Created {output_path}; green palette RGB={tuple(palette[green*3:green*3+3])}')

if __name__=='__main__':
    create(*sys.argv[1:])
