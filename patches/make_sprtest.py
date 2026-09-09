import struct, math, re, os, shutil
STEAM=r"C:/Program Files (x86)/Steam/steamapps/common/Half-Life/valve"
LOCAL=r"C:/Users/Oscar/Proyectos/xash3d/valve"
W=H=64
# palette: ramp negro -> verde, entrada 255 = verde puro
pal=bytearray()
for i in range(256): pal+=bytes((0,i,0))
def blob(fmt):
    px=bytearray()
    for y in range(H):
        for x in range(W):
            d=math.hypot(x-31.5,y-31.5)/32.0
            v=max(0.0,1.0-d)
            idx=int(round(v*255))
            if fmt==3 and d>0.9: idx=255      # ALPHTEST: fuera = 255 = transparente
            elif fmt==3: idx=min(idx,254)
            px.append(idx)
    return bytes(px)
def spr(fmt):
    b=b'IDSP'+struct.pack('<iiifiiifi',2,2,fmt,45.25,W,H,1,0.0,0)  # version 2, type VP_PARALLEL
    b+=struct.pack('<h',256)+bytes(pal)
    b+=struct.pack('<iiiii',0,-W//2,H//2,W,H)+blob(fmt)
    return b
names={0:'normal',1:'additive',2:'indexalpha',3:'alphtest'}
for d in (STEAM,LOCAL):
    for f,n in names.items():
        open(f"{d}/sprites/sprtest_{n}.spr",'wb').write(spr(f))
if not os.path.exists(STEAM+"/sprites/spore2.spr"):
    shutil.copy(LOCAL+"/sprites/spore2.spr",STEAM+"/sprites/spore2.spr")
# mapa: stalkyard + env_sprites delante del info_player_start (832 1104 56, yaw 180 -> mira a -X)
src=open(STEAM+"/maps/stalkyard.bsp",'rb').read()
off,ln=struct.unpack_from('<ii',src,4)
ents=src[off:off+ln].decode('latin1').rstrip('\x00')
models=['sprites/sprtest_normal.spr','sprites/sprtest_additive.spr','sprites/sprtest_indexalpha.spr','sprites/sprtest_alphtest.spr','sprites/spore2.spr','sprites/glow01.spr']
new=''
for row,(rm,z) in enumerate(((0,136),(2,72))):
    for k,m in enumerate(models):
        y=1104+(k-2.5)*56
        new+='{\n"classname" "env_sprite"\n"model" "%s"\n"origin" "704 %d %d"\n"rendermode" "%d"\n"renderamt" "255"\n"rendercolor" "0 0 0"\n"framerate" "10"\n"scale" "1"\n"spawnflags" "1"\n}\n'%(m,int(y),z,rm)
ents=ents+new+'\x00'
data=bytearray(src)
eb=ents.encode('latin1')
newoff=len(data); data+=eb
struct.pack_into('<ii',data,4,newoff,len(eb))
for d in (STEAM,LOCAL):
    open(d+"/maps/sprtest.bsp",'wb').write(data)
print("ok", len(eb), "bytes entidades;", len(models)*2, "env_sprites")
