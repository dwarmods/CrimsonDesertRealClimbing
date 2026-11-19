exec(open('port3732.py').read().split("names=[")[0])
import capstone,re,json,statistics
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64); md.detail=True
def sig(m,rva,nins=40):
    o=m.rva2off(rva); pat=b''; k=0
    for ins in md.disasm(m.d[o:o+nins*15],m.IB+rva):
        b=bytes(ins.bytes); mask=[True]*len(b)
        if ins.mnemonic in('call','jmp') and len(b)>=5 and b[0] in (0xe8,0xe9): mask[1:5]=[False]*4
        elif ins.mnemonic.startswith('j') and len(b)==6: mask[2:6]=[False]*4
        if 'rip' in ins.op_str and getattr(ins,'disp_offset',0): d=ins.disp_offset; mask[d:d+4]=[False]*4
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
out={}
for n in (40,25,15):
    h=find(new,sig(old,0x189c900,n))
    print('equip worker nins',n,[hex(x) for x in h][:4])
    if len(h)==1:
        s=sim(norm(old,0x189c900),norm(new,h[0])); print(' sim',round(s,3)); out['equip_condition_worker']=dict(rva_3663='0x189c900',rva_3732=hex(h[0]),sim=round(s,3))
        L=new.dis(h[0],260,stop_ret=False); print('\n'.join([l for l in L if '0x68]' in l or '0x38]' in l][:8])); break
vt={'ClientInventoryActorComponent':(0x59ca678,0x59cf760),'ClientStatusActorComponent':(0x5998b60,0x599dc40),'ClientCharacterControlActorComponent':(0x59c1660,0x59c6748),'ClientChildOnlyInGameActor':(0x5990bf0,0x59961d0),'ClientEquipSlotActorComponent':(0x59c90f8,0x59ce1d8)}
for k,(a,b) in vt.items():
    ss=[]
    for i in range(min(ns(old,a),ns(new,b))):
        fo=old.u64(a+8*i)-old.IB; fn=new.u64(b+8*i)-new.IB
        ss.append(sim(norm(old,fo,200),norm(new,fn,200)))
    ident=sum(1 for x in ss if x>0.999)
    print(f'{k}: {len(ss)} slots, identical {ident}, median sim {statistics.median(ss):.3f}, min {min(ss):.3f}')
    out[k]=dict(slots=len(ss),identical=ident,median=round(statistics.median(ss),3),min=round(min(ss),3))
json.dump(out,open('layout3732.json','w'),indent=1)
