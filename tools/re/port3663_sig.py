exec(open("port3663_lib.py").read())
import capstone,re
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64); md.detail=True
def sig(m,rva,nins=40):
    o=m.rva2off(rva); pat=b''; k=0
    for ins in md.disasm(m.d[o:o+nins*15],m.IB+rva):
        b=bytes(ins.bytes); mask=[True]*len(b)
        # wildcard rel32 of call/jmp and rip-relative displacements
        if ins.mnemonic in('call','jmp') and len(b)>=5 and b[0] in (0xe8,0xe9): mask[1:5]=[False]*4
        elif ins.mnemonic.startswith('j') and len(b)==6: mask[2:6]=[False]*4
        if 'rip' in ins.op_str:
            dofs=ins.disp_offset if hasattr(ins,'disp_offset') else None
            if dofs: mask[dofs:dofs+4]=[False]*4
        for x,mk in zip(b,mask): pat+= re.escape(bytes([x])) if mk else b'.'
        k+=1
        if k>=nins: break
    return pat
def find(m,pat):
    hits=[]
    for s in m.pe.sections:
        if not (s.Characteristics & 0x20000000): continue
        ro=s.PointerToRawData; data=m.d[ro:ro+s.SizeOfRawData]
        for h in re.finditer(pat,data,re.S): hits.append(s.VirtualAddress+h.start())
    return hits
for rva,label in ((0x1837150,'equip_condition_worker'),(0x228a440,'equip_lookup_callee')):
    for n in (40,25,15):
        p=sig(old,rva,n); h=find(new,p); ho=find(old,p)
        print(label,'nins',n,'old hits',[hex(x) for x in ho][:5],'new hits',[hex(x) for x in h][:5])
        if len(h)==1 and len(ho)==1:
            print('  sim',round(sim(norm(old,rva),norm(new,h[0])),3)); break
