import sys, json, re
exec(open('port3663.py').read().split("names=")[0])
import capstone
def norm(m,rva,n=400):
    o=m.rva2off(rva); out=[]
    md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64)
    for ins in md.disasm(m.d[o:o+n*15],m.IB+rva):
        s=ins.mnemonic+' '+ins.op_str
        s=re.sub(r'0x[0-9a-f]{6,}','ADDR',s)   # abs/rel targets
        out.append(s)
        if ins.mnemonic in ('ret','int3') and len(out)>3: break
        if len(out)>=n: break
    return out
import difflib
def sim(a,b): return difflib.SequenceMatcher(None,a,b,autojunk=False).ratio()
def nslots(m,vt):
    n=0
    while True:
        p=m.u64(vt+8*n)
        if not (m.IB<p<m.IB+m.pe.OPTIONAL_HEADER.SizeOfImage): break
        # stop at next COL pointer (rdata)
        sec=[s for s in m.secs if s[1]<=p-m.IB<s[1]+s[2]]
        if not sec or not sec[0][0].startswith(('.text','.ecode')): break
        n+=1
    return n
pairs={'PointClimb':(0x5926780,0x59a2b48),'LadderClimb':(0x59267d8,0x59a2ba0),'ICharMove':(0x5926998,0x59a2e10),
       'ClientEquip':(0x594d478,0x59c90f8),'CommonEquip':(0x5cc7960,0x5d43420),'ServerEquip':(0x5ef28b0,0x5f6c518),'ClientSkill':(0x5954400,0x59d02f0)}
for k,(vo,vn) in pairs.items(): print(k,'slots old',nslots(old,vo),'new',nslots(new,vn))
vo,vn=pairs['PointClimb']; bo,bn=pairs['ICharMove']
out={}
for i in range(nslots(old,vo)):
    fo=old.u64(vo+8*i)-old.IB; fn=new.u64(vn+8*i)-new.IB
    a=norm(old,fo); b=norm(new,fn)
    base_o=old.u64(bo+8*i)-old.IB if i<nslots(old,bo) else None; base_n=new.u64(bn+8*i)-new.IB if i<nslots(new,bn) else None
    s=sim(a,b); print(f'slot {i:2d} old {fo:#x} ({len(a)} ins) new {fn:#x} ({len(b)} ins) sim={s:.3f} overrides_base old={fo!=base_o} new={fn!=base_n}')
    out[i]=dict(old=fo,new=fn,sim=s,len_old=len(a),len_new=len(b))
json.dump(out,open('/workspace/climbmod/exe3663/pointclimb_slots.json','w'),indent=1)
for i in (7,8):
    print('--- new slot',i); print('\n'.join(new.dis(out[i]['new'],40)))
