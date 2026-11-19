# Port 3663 -> 3732: RTTI vtables + PointClimb slot matching (read-only, offline).
import sys, json, re, difflib
exec(open('port3663.py').read().split("old=load")[0])
import os
old=load(os.environ['CD_EXE_3663']); new=load(os.environ['CD_EXE_3732'])
for tag,m in (('3663',old),('3732',new)):
    p=m.pe; print(tag,hex(p.FILE_HEADER.TimeDateStamp),hex(p.OPTIONAL_HEADER.SizeOfImage),hex(p.OPTIONAL_HEADER.CheckSum),hex(m.IB))
import capstone
def norm(m,rva,n=400):
    o=m.rva2off(rva); out=[]
    md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64)
    for ins in md.disasm(m.d[o:o+n*15],m.IB+rva):
        s=ins.mnemonic+' '+ins.op_str
        s=re.sub(r'0x[0-9a-f]{6,}','ADDR',s); out.append(s)
        if ins.mnemonic in ('ret','int3') and len(out)>3: break
        if len(out)>=n: break
    return out
def sim(a,b): return difflib.SequenceMatcher(None,a,b,autojunk=False).ratio()
def execsec(m,p):
    r=p-m.IB
    for s in m.pe.sections:
        if s.VirtualAddress<=r<s.VirtualAddress+max(s.Misc_VirtualSize,s.SizeOfRawData): return bool(s.Characteristics & 0x20000000)
    return False
def ns(m,vt):
    n=0
    while execsec(m,m.u64(vt+8*n)): n+=1
    return n
names=['.?AVPointClimbMovementMethod@pa@@','.?AVLadderClimbMovementMethod@pa@@','.?AVClientEquipSlotActorComponent@pa@@','.?AVCommonEquipSlotActorComponent@pa@@','.?AVServerEquipSlotActorComponent@pa@@','.?AVClientSkillActorComponent@pa@@','.?AVICharacterMovementMethod@pa@@','.?AVClientInventoryActorComponent@pa@@','.?AVClientStatusActorComponent@pa@@','.?AVClientCharacterControlActorComponent@pa@@','.?AVClientChildOnlyInGameActor@pa@@']
vt={}
for nm in names:
    vo=old.vtables(nm); vn=new.vtables(nm)
    vt[nm]=dict(old=[hex(a) for a,o in vo if o==0],new=[hex(a) for a,o in vn if o==0],all_new=[(hex(a),o) for a,o in vn])
    print(nm,'3663',vt[nm]['old'],'3732',vt[nm]['all_new'],'slots',[ns(old,int(a,16)) for a in vt[nm]['old']],[ns(new,int(a,16)) for a in vt[nm]['new']])
pc_o=int(vt[names[0]]['old'][0],16); pc_n=int(vt[names[0]]['new'][0],16)
bm_o=int(vt[names[6]]['old'][0],16); bm_n=int(vt[names[6]]['new'][0],16)
slots={}
for i in range(ns(old,pc_o)):
    fo=old.u64(pc_o+8*i)-old.IB; fn=new.u64(pc_n+8*i)-new.IB
    s=sim(norm(old,fo),norm(new,fn))
    ovo=fo!=old.u64(bm_o+8*i)-old.IB if i<ns(old,bm_o) else True; ovn=fn!=new.u64(bm_n+8*i)-new.IB if i<ns(new,bm_n) else True
    slots[i]=dict(rva_3663=hex(fo),rva_3732=hex(fn),sim=round(s,3),override_3663=ovo,override_3732=ovn)
    print(f'slot {i:2d} 3663 {fo:#x} 3732 {fn:#x} sim={s:.3f} override {ovo}/{ovn}')
json.dump(dict(vtables=vt,pointclimb_slots=slots),open('port3732.json','w'),indent=1)
for i in (7,8):
    print('--- 3732 slot',i); print('\n'.join(new.dis(int(slots[i]['rva_3732'],16),30)))
