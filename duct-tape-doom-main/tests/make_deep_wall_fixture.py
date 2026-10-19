import struct as S
from pathlib import Path
v=[(0,0),(0,256),(256,256),(256,0),(4352,0),(4352,256),(4608,256),(4608,0)]
lines=[(i, (i//4)*4+(i+1)%4, 1,0,0,i,65535) for i in range(8)]
lumps=[('E1M1',b''),('THINGS',S.pack('<5h',128,128,0,1,7)),('LINEDEFS',b''.join(S.pack('<7H',*x) for x in lines))]
lumps += [('SIDEDEFS',b''.join(S.pack('<hh8s8s8sH',0,0,b'-',b'-',b'STARTAN3',i//4) for i in range(8))),('VERTEXES',b''.join(S.pack('<hh',*x) for x in v))]
angles=[16384,0,49152,32768]*2
lumps += [('SEGS',b''.join(S.pack('<6H',l[0],l[1],angles[i],i,0,0) for i,l in enumerate(lines))),('SSECTORS',S.pack('<4H',4,0,4,4)),('NODES',S.pack('<12h2H',2304,0,0,256,256,0,4352,4608,256,0,0,256,32769,32768)),('SECTORS',b''.join(S.pack('<hh8s8shhh',0,128,b'FLOOR0_1',b'CEIL1_1',192,0,0) for i in range(2))),('REJECT',b'\0')]
left=bottom=-128;width=39;height=5
cells=[[] for _ in range(width*height)]
for i,line in enumerate(lines):
 a,b=v[line[0]],v[line[1]]
 for y in range(max(0,(min(a[1],b[1])-bottom)//128-1),min(height,(max(a[1],b[1])-bottom)//128+2)):
  for x in range(max(0,(min(a[0],b[0])-left)//128-1),min(width,(max(a[0],b[0])-left)//128+2)):cells[y*width+x].append(i)
words=[left&65535,bottom&65535,width,height]+[0]*len(cells)
for i,c in enumerate(cells):words[4+i]=len(words);words += [0]+c+[65535]
lumps += [('BLOCKMAP',S.pack('<'+'H'*len(words),*words))]
body=bytearray(b'PWAD'+b'\0'*8);directory=[]
for name,data in lumps:directory.append((len(body),len(data),name.encode()));body+=data
offset=len(body)
for row in directory:body+=S.pack('<ii8s',*row)
S.pack_into('<ii',body,4,len(directory),offset)
Path('DeepWallTest.wad').write_bytes(body)
