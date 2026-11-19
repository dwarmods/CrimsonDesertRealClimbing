# Offline replay of the plugin's fail-closed gate: parses src/build_info.h and checks
# every BuildDef against the matching exe on disk (paths via env CD_EXE_<build>).
import os, re, sys, json
exec(open('port3663.py').read().split("old=load")[0])
H=open('../../plugin/src/build_info.h').read()
idx=[1,4,6,7,8]; out={}
defs=re.findall(r"inline constexpr BuildDef (k\d+) \{(.*?)\n    \};",H,re.S)
for name,body in defs:
    nums=[int(x,16) for x in re.findall(r"0x([0-9A-Fa-f]+)u?",body)]; rtti=re.findall(r'"(\.\?AV[^"]+)"',body)
    path=os.environ.get('CD_EXE_'+name[1:], f'CrimsonDesert_{name[1:]}.exe')
    m=load(path); p=m.pe
    r={'identity':(p.FILE_HEADER.TimeDateStamp,p.OPTIONAL_HEADER.SizeOfImage,p.OPTIONAL_HEADER.CheckSum)==tuple(nums[:3])}
    vts=nums[3:9]
    for nm,vt in zip(rtti,vts):
        col=m.u64(vt-8)-m.IB; td=m.u32(col+0xC)
        r[nm]=(m.u32(col)==1 and m.u32(col+0x14)==col and m.d[m.rva2off(td+0x10):m.rva2off(td+0x10)+len(nm)].decode()==nm)
    pc=vts[0]; r['slots']=all(m.u64(pc+8*i)-m.IB==s for i,s in zip(idx,nums[9:14]))
    out[name]=r; print(name,r)
assert all(v for r in out.values() for v in r.values()); json.dump(out,open('gate_replay.json','w'),indent=1); print('GATE_REPLAY_OK')
