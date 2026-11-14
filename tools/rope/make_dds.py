# PNG -> DDS (DX10 header, full mip chain): albedo/orm BC7_UNORM, normal BC5_UNORM (RG, OpenGL +Y). Validates by decoding.
import struct,sys,glob,os,json,numpy as np,etcpak,texture2ddecoder as t2d
from PIL import Image
T='/workspace/climbmod/rope_slice/textures'
def dds(path,w,h,mips,fmt,blobs):
    hdr=struct.pack('<4sI6I44x8I5I','DDS '.encode(),124,0x1|0x2|0x4|0x1000|0x80000|0x20000,h,w,len(blobs[0]),0,mips,32,0x4,int.from_bytes(b'DX10','little'),0,0,0,0,0,0x1000|0x400000|0x8,0,0,0,0)
    dx10=struct.pack('<5I',fmt,3,0,1,0)
    open(path,'wb').write(hdr+dx10+b''.join(blobs))
def encode(png,kind):
    im=Image.open(png).convert('RGBA');w,h=im.size;blobs=[];errs=[];lvl=im
    while True:
        a=np.asarray(lvl);raw=a.tobytes()
        if kind=='normal':
            b=etcpak.compress_bc5(raw,lvl.width,lvl.height);dec=np.frombuffer(t2d.decode_bc5(b,lvl.width,lvl.height),np.uint8).reshape(lvl.height,lvl.width,4)[...,[2,1,0,3]];e=np.abs(dec[...,:2].astype(int)-a[...,:2]).mean()
        else:
            b=etcpak.compress_bc7(raw,lvl.width,lvl.height);dec=np.frombuffer(t2d.decode_bc7(b,lvl.width,lvl.height),np.uint8).reshape(lvl.height,lvl.width,4)[...,[2,1,0,3]];e=np.abs(dec[...,:3].astype(int)-a[...,:3]).mean()
        blobs.append(b);errs.append(float(e))
        if lvl.width<=4 or lvl.height<=4:break
        lvl=lvl.resize((lvl.width//2,lvl.height//2),Image.LANCZOS)
    out=png[:-4]+'.dds';dds(out,w,h,len(blobs),83 if kind=='normal' else 98,blobs)
    return dict(file=os.path.basename(out),format='BC5_UNORM' if kind=='normal' else 'BC7_UNORM',size=[w,h],mips=len(blobs),bytes=os.path.getsize(out),mean_abs_error_mip0=round(errs[0],3))
res=[]
for n in sys.argv[1:]:
    for t in('albedo','normal','orm'):res.append(encode(f'{T}/{n}_{t}.png','normal' if t=='normal' else 'color'));print(res[-1])
json.dump(res,open(f'{T}/DDS_REPORT.json','w'),indent=1)
