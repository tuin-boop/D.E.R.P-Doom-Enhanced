"""Build an explorable, vanilla-format material gallery from an installed IWAD.
No artwork is copied: run with the same IWAD used to build it.
Usage: material_gallery.py base.wad output.wad
"""
import csv
from collections import deque
import math
import struct as S
import sys
from pathlib import Path

def label(s): return s.encode('ascii').ljust(8,b'\0')
def make(base_path, output_path, switches=False):
    base=Path(base_path).read_bytes(); count,directory=S.unpack_from('<ii',base,4)
    lumps={}; flats=[]; inside=False
    for i in range(count):
        off,size,raw=S.unpack_from('<ii8s',base,directory+i*16); name=raw.rstrip(b'\0').decode('ascii')
        lumps[name]=base[off:off+size]
        if name in ('F_START','FF_START'): inside=True
        elif name in ('F_END','FF_END'): inside=False
        elif inside and size==4096 and name not in flats: flats.append(name)
    textures=[];texture_heights={}
    for name in ('TEXTURE1','TEXTURE2'):
        if name not in lumps: continue
        data=lumps[name]; n=S.unpack_from('<i',data)[0]
        for off in S.unpack_from('<'+'i'*n,data,4):
            tex=data[off:off+8].rstrip(b'\0').decode('ascii')
            texture_heights[tex]=S.unpack_from('<h',data,off+14)[0]
            if tex not in textures and tex!='-': textures.append(tex)
    assert textures and flats
    # Put the reported fixtures first, then include every remaining definition.
    priority=['REDWALL1','DOORRED','DOORBLU','DOORYEL','DOOR1','DOOR3','BIGDOOR2','BIGDOOR4']
    textures=[x for x in priority if x in textures]+[x for x in textures if x not in priority]
    doors=[x for x in textures if x.startswith(('DOOR','BIGDOOR')) or x=='EXITDOOR']
    if switches:
        textures=sorted((x for x in textures if x.startswith(('SW1','SW2'))),key=lambda x:(x[3:],x[2]))
        flats=['FLOOR0_1'];doors=['STARTAN3']
    nrooms=max(len(textures),len(flats)); columns=8 if switches else 10; rows=math.ceil(nrooms/columns)
    # Each tile has a cross corridor, a working door, an enclosed display room,
    # and solid dividers. Nonuniform cells keep the BSP small and deterministic.
    xsteps=[32,96,64,96,32,96]; ysteps=[96,32,256,32]
    if switches:xsteps=[16,32,64,32,16,64];ysteps=[64,16,128,16]
    xs=[0];ys=[0]
    for step in xsteps*columns: xs.append(xs[-1]+step)
    for step in ysteps*rows: ys.append(ys[-1]+step)
    nx=len(xs)-1;ny=len(ys)-1
    floor='FLOOR0_1' if 'FLOOR0_1' in flats else flats[0]
    wall='STARTAN3' if 'STARTAN3' in textures else textures[0]
    if switches:wall='STARTAN3'
    sectors=[(0,0,floor,floor,0),(0,128,floor,floor,112)]
    sector_tex=[wall,wall];roomdata=[]
    for room in range(nrooms):
        tex=textures[room%len(textures)];flat=flats[room%len(flats)];ceil=flats[(room+17)%len(flats)]
        liquid=flat.startswith(('NUKAGE','FWATER','SWATER','LAVA','BLOOD','SLIME'))
        sector=len(sectors);sectors.append((-16 if liquid else 0,max(128,texture_heights[tex]),flat,ceil,96));sector_tex.append(tex)
        door=len(sectors);sectors.append((0,128 if switches else 0,floor,floor,96));sector_tex.append(doors[room%len(doors)])
        cx=room%columns;cy=room//columns
        roomdata.append((room+1,tex,flat,ceil,sector_tex[door],xs[cx*6+2]+32,ys[cy*4]+48,sector,door))
    grid=[]
    for y in range(ny):
        row=[]
        for x in range(nx):
            room=(y//4)*columns+x//6; xx=x%6;yy=y%4;sec=0
            if xx==5 or yy==0: sec=1
            elif room<nrooms:
                if yy==2 and 1<=xx<=3:sec=roomdata[room][-2]
                elif yy==1 and xx==2:sec=roomdata[room][-1]
            row.append(sec)
        grid.append(row)
    reachable={(2,0)};pending=deque(reachable)
    while pending:
        x,y=pending.popleft()
        for xx,yy in ((x-1,y),(x+1,y),(x,y-1),(x,y+1)):
            if 0<=xx<nx and 0<=yy<ny and grid[yy][xx] and (xx,yy) not in reachable:
                reachable.add((xx,yy));pending.append((xx,yy))
    assert {r[-2] for r in roomdata}<={grid[y][x] for x,y in reachable},'Unreachable room'
    assert set(textures)<={r[1] for r in roomdata}
    assert set(flats)<={r[2] for r in roomdata} and set(flats)<={r[3] for r in roomdata}
    vertices=[(x,y) for y in ys for x in xs]
    def vertex(x,y):return y*(nx+1)+x
    edges={}; lines=[]; sides=[]; segs=[]; subsectors=[]
    door_sectors={r[-1] for r in roomdata}
    def side(sec,other):
        tex=wall if switches else sector_tex[sec]
        # The moving upper face uses the door's texture on both approaches.
        face=sector_tex[other] if other in door_sectors else tex
        sides.append(S.pack('<hh8s8s8sh',0,0,label(face),label(face),label('-' if other is not None else tex),sec))
        return len(sides)-1
    for y in range(ny):
        for x in range(nx):
            sec=grid[y][x];start=len(segs)
            corners=[vertex(x,y),vertex(x,y+1),vertex(x+1,y+1),vertex(x+1,y)]
            neighbors=[grid[y][x-1] if x else None,grid[y+1][x] if y+1<ny else None,
                       grid[y][x+1] if x+1<nx else None,grid[y-1][x] if y else None]
            for i,a in enumerate(corners):
                b=corners[(i+1)%4];key=tuple(sorted((a,b)));other=neighbors[i]
                if key not in edges:
                    idx=len(lines);edges[key]=(idx,a,b)
                    special=1 if (sec in door_sectors or other in door_sectors) and sec!=other and other not in (None,0) and sec!=0 else 0
                    if switches:special=0
                    # Solid void cells have zero height and no room-facing back side.
                    flags=4 if other is not None and (sec==0)==(other==0) else 1
                    lines.append([a,b,flags,special,0,side(sec,other),-1])
                    orientation=0
                else:
                    idx,aa,bb=edges[key];orientation=1
                    lines[idx][6]=side(sec,other)
                if switches and y%4==2 and x%6==2 and i==1 and sec not in (0,1):
                    # One usable panel at the back of each alcove. No sector has
                    # tag 32000: the repeatable button changes only its artwork.
                    panel_side=lines[idx][5 if orientation==0 else 6]
                    sides[panel_side]=S.pack('<hh8s8s8sh',0,0,label(sector_tex[sec]),label('-'),label('-'),sec)
                    lines[idx][3]=138;lines[idx][4]=32000
                ax,ay=vertices[a];bx,by=vertices[b]
                angle=round(math.atan2(by-ay,bx-ax)*65536/(2*math.pi))%65536
                segs.append(S.pack('<HHHHHH',a,b,angle,idx,orientation,0))
            subsectors.append(S.pack('<HH',4,start))
    # Manual doors operate their back sector from either activation face.
    flipped=set()
    for i,line in enumerate(lines):
        if line[3]==1 and S.unpack_from('<h',sides[line[5]],28)[0] in door_sectors:
            line[0],line[1]=line[1],line[0];line[5],line[6]=line[6],line[5];flipped.add(i)
    for i,seg in enumerate(segs):
        values=list(S.unpack('<6H',seg))
        if values[3] in flipped:values[4]^=1;segs[i]=S.pack('<6H',*values)
    # Front-to-back axis-aligned BSP, one convex cell per leaf.
    nodes=[]
    def bounds(x0,y0,x1,y1):return (ys[y1],ys[y0],xs[x0],xs[x1])
    def bsp(x0,y0,x1,y1):
        if x1-x0==1 and y1-y0==1:return 0x8000+y0*nx+x0
        if x1-x0>=y1-y0 and x1-x0>1:
            mid=(x0+x1)//2;left=(x0,y0,mid,y1);right=(mid,y0,x1,y1)
            parts=[right,left];plane=(xs[mid],ys[y0],0,1)
        else:
            mid=(y0+y1)//2;parts=[(x0,y0,x1,mid),(x0,mid,x1,y1)];plane=(xs[x0],ys[mid],1,0)
        children=[bsp(*p) for p in parts]
        nodes.append(S.pack('<hhhh8hHH',*plane,*bounds(*parts[0]),*bounds(*parts[1]),*children))
        return len(nodes)-1
    bsp(0,0,nx,ny)
    # Collision block lists include lines on either side of block boundaries.
    origin=-64;bw=math.ceil((xs[-1]-origin)/128)+1;bh=math.ceil((ys[-1]-origin)/128)+1
    blocks=[[] for _ in range(bw*bh)]
    for i,line in enumerate(lines):
        if line[6]>=0 and S.unpack_from('<h',sides[line[5]],28)[0]==S.unpack_from('<h',sides[line[6]],28)[0]:continue
        a,b=vertices[line[0]],vertices[line[1]]
        for by in range(max(0,(min(a[1],b[1])-origin-1)//128),min(bh-1,(max(a[1],b[1])-origin)//128)+1):
            for bx in range(max(0,(min(a[0],b[0])-origin-1)//128),min(bw-1,(max(a[0],b[0])-origin)//128)+1):blocks[by*bw+bx].append(i)
    offsets=[];words=[];shared={}
    for block in blocks:
        key=tuple(block)
        if key not in shared:shared[key]=4+len(blocks)+len(words);words.extend([0,*block,65535])
        offsets.append(shared[key])
    assert max(offsets)<65536 and len(segs)<65536 and len(subsectors)<32768
    blockmap=S.pack('<hhHH',origin,origin,bw,bh)+S.pack('<'+'H'*len(offsets),*offsets)+S.pack('<'+'H'*len(words),*words)
    spawn=roomdata[0];things=S.pack('<hhhhh',spawn[5],spawn[6],90,1,7)
    for i,kind in enumerate(() if switches else (5,6,13,38,39,40,2001,2002,2003,2004,2006,2048,2049,2046,17)):
        things+=S.pack('<hhhhh',spawn[5]+96+i*48,48,0,kind,7)
    mapname='MAP01' if 'MAP01' in lumps else 'E1M1'
    result=[(mapname,b''),('THINGS',things),('LINEDEFS',b''.join(S.pack('<7H',*(v & 65535 for v in l)) for l in lines)),('SIDEDEFS',b''.join(sides)),
            ('VERTEXES',b''.join(S.pack('<hh',*p) for p in vertices)),('SEGS',b''.join(segs)),('SSECTORS',b''.join(subsectors)),('NODES',b''.join(nodes)),
            ('SECTORS',b''.join(S.pack('<hh8s8shhh',a,b,label(c),label(d),e,0,0) for a,b,c,d,e in sectors)),('REJECT',bytes((len(sectors)**2+7)//8)),('BLOCKMAP',blockmap)]
    result.append(('DTDGALRY',b''.join(S.pack('<HH8s8s8s8s',r[-2],r[0],label(r[1]),label(r[2]),label(r[3]),label(r[4])) for r in roomdata)))
    payload=bytearray();entries=bytearray()
    for name,data in result:entries+=S.pack('<ii8s',12+len(payload),len(data),label(name));payload+=data
    out=Path(output_path);out.parent.mkdir(parents=True,exist_ok=True)
    out.write_bytes(b'PWAD'+S.pack('<ii',len(result),12+len(payload))+payload+entries)
    with out.with_suffix('.csv').open('w',newline='') as f:
        w=csv.writer(f);w.writerow(['Room','Wall texture','Floor','Ceiling','Working door','Entrance X','Entrance Y']);w.writerows(r[:7] for r in roomdata)
    out.with_suffix('.txt').write_text(f'MATERIAL GALLERY: {nrooms} rooms, {len(textures)} wall textures, {len(flats)} floor/ceiling flats.\nUse with {Path(base_path).name}. Start a new game at {mapname}.\nRooms run left to right, then north, in rows of {columns}. Every room has a working door.\nCross corridors connect every row. CSV lists each room and entrance coordinates.\nAll stock wall definitions, flats, animated liquids and sky flats are included. Liquid display floors are recessed and safe.\nThe first rooms show REDWALL1 and colored door strips. The first corridor has keys and weapons.\nThis WAD contains only a generated map, no IWAD artwork.\n')
    if switches:
        out.with_suffix('.txt').write_text(f'SWITCH GALLERY: {nrooms} alcoves, both states of every SW1/SW2 texture in {Path(base_path).name}.\nStart a new game at {mapname}. Walk forward into each alcove and press Use at its back panel.\nButtons change state for one second, then reset. Adjacent alcoves show the paired starting states.\nEight alcoves per row, connected by open corridors. No monsters, hazards or moving doors.\nNeutral walls and dim fixed lighting make the switch glow easier to compare.\nThe CSV lists panel names and entrance coordinates; the engine displays the room label.\nTextures without lamps intentionally remain unlit. No IWAD artwork is included.\n')
    print(f'{out}: {nrooms} rooms, {len(lines)} lines, {len(subsectors)} subsectors, {len(textures)} textures, {len(flats)} flats')

if __name__=='__main__': make(*sys.argv[1:])
