import sys,re,difflib,json
exec(open('port3663_slots.py').read().split("pairs=")[0])
def execsec(m,p):
    r=p-m.IB
    for s in m.pe.sections:
        if s.VirtualAddress<=r<s.VirtualAddress+max(s.Misc_VirtualSize,s.SizeOfRawData): return bool(s.Characteristics & 0x20000000)
    return False
def ns(m,vt):
    n=0
    while execsec(m,m.u64(vt+8*n)): n+=1
    return n
pairs={'PointClimb':(0x5926780,0x59a2b48),'LadderClimb':(0x59267d8,0x59a2ba0),'ICharMove':(0x5926998,0x59a2e10),'ClientEquip':(0x594d478,0x59c90f8),'CommonEquip':(0x5cc7960,0x5d43420),'ServerEquip':(0x5ef28b0,0x5f6c518),'ClientSkill':(0x5954400,0x59d02f0)}
for k,(a,b) in pairs.items(): print(k,'slots old',ns(old,a),'new',ns(new,b))
vo,vn=pairs['PointClimb'];bo,bn=pairs['ICharMove']
for i in range(10):
    print(i,'overrides base: old',old.u64(vo+8*i)!=old.u64(bo+8*i),'new',new.u64(vn+8*i)!=new.u64(bn+8*i))
for i in (7,8,1):
    a=norm(old,old.u64(vo+8*i)-old.IB); b=norm(new,new.u64(vn+8*i)-new.IB)
    d=[l for l in difflib.unified_diff(a,b,lineterm='',n=0) if not l.startswith('@@')]
    print(f'=== slot {i} diff lines {len(d)}'); print('\n'.join(d[:60]))
