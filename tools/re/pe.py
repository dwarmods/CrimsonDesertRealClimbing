import pefile,struct,re,capstone
EXE='/workspace/exe/CrimsonDesert.exe'
d=open(EXE,'rb').read()
pe=pefile.PE(EXE,fast_load=True)
IB=pe.OPTIONAL_HEADER.ImageBase
secs=[(s.Name.rstrip(b'\0').decode(),s.VirtualAddress,max(s.Misc_VirtualSize,s.SizeOfRawData),s.PointerToRawData,s.SizeOfRawData) for s in pe.sections]
def off2rva(o):
    for n,va,vs,ro,rs in secs:
        if ro<=o<ro+rs: return va+o-ro
def rva2off(r):
    for n,va,vs,ro,rs in secs:
        if va<=r<va+rs: return ro+r-va
def u32(r): return struct.unpack_from('<I',d,rva2off(r))[0]
def u64(r): return struct.unpack_from('<Q',d,rva2off(r))[0]
def vtables(name):
    """return list of vtable RVAs for RTTI decorated name"""
    out=[]
    for m in re.finditer(re.escape(name.encode())+b'\0',d):
        td=off2rva(m.start())-0x10
        pat=struct.pack('<I',td)
        for c in re.finditer(re.escape(pat),d):
            colo=c.start()-0x0C
            if struct.unpack_from('<I',d,colo)[0]!=1: continue
            colr=off2rva(colo)
            for v in re.finditer(re.escape(struct.pack('<Q',IB+colr)),d):
                out.append((off2rva(v.start())+8, struct.unpack_from('<I',d,colo+4)[0]))
    return out
def vfuncs(vt,n=40):
    r=[]
    for i in range(n):
        p=u64(vt+8*i)
        if not (IB<p<IB+0x14000000): break
        rv=p-IB
        if i>0 and any(rv==x for x in []): break
        r.append(rv)
    return r
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64); md.detail=False
def dis(rva,n=60,stop_ret=True):
    o=rva2off(rva); out=[]
    for ins in md.disasm(d[o:o+n*15],IB+rva):
        out.append(f'{ins.address-IB:08X}: {ins.mnemonic} {ins.op_str}')
        if len(out)>=n or (stop_ret and ins.mnemonic in('ret','int3') ): break
    return out
