# Offline replay of the plugin's fail-closed gate against both exes on disk.
exec(open("port3663_lib.py").read())
import struct,json
B={'3576':dict(m=old,id=(0x6AED2E85,0x13CC4000,0x137C1424),vt={'.?AVPointClimbMovementMethod@pa@@':0x05926780,'.?AVLadderClimbMovementMethod@pa@@':0x059267D8,'.?AVClientEquipSlotActorComponent@pa@@':0x0594D478,'.?AVCommonEquipSlotActorComponent@pa@@':0x05CC7960,'.?AVServerEquipSlotActorComponent@pa@@':0x05EF28B0,'.?AVClientSkillActorComponent@pa@@':0x05954400},slots=[0x761A30,0x761FA0,0x762350,0x761070,0x761480]),
   '3663':dict(m=new,id=(0x6AF5D198,0x144E9000,0x13FF2BD0),vt={'.?AVPointClimbMovementMethod@pa@@':0x059A2B48,'.?AVLadderClimbMovementMethod@pa@@':0x059A2BA0,'.?AVClientEquipSlotActorComponent@pa@@':0x059C90F8,'.?AVCommonEquipSlotActorComponent@pa@@':0x05D43420,'.?AVServerEquipSlotActorComponent@pa@@':0x05F6C518,'.?AVClientSkillActorComponent@pa@@':0x059D02F0},slots=[0x78DEE0,0x78E5F0,0x78E9A0,0x78D520,0x78D930])}
idx=[1,4,6,7,8]; out={}
for tag,b in B.items():
    m=b['m']; p=m.pe; ok=(p.FILE_HEADER.TimeDateStamp,p.OPTIONAL_HEADER.SizeOfImage,p.OPTIONAL_HEADER.CheckSum)==b['id']
    r={'identity':ok}
    for nm,vt in b['vt'].items():
        col=m.u64(vt-8)-m.IB; sig=m.u32(col); td=m.u32(col+0xC); selfr=m.u32(col+0x14)
        name=m.d[m.rva2off(td+0x10):m.rva2off(td+0x10)+len(nm)].decode()
        r[nm]=(sig==1 and selfr==col and name==nm)
    pc=b['vt']['.?AVPointClimbMovementMethod@pa@@']
    r['slots']=[m.u64(pc+8*i)-m.IB==s for i,s in zip(idx,b['slots'])]
    # cross: other build's constants must NOT pass on this exe
    out[tag]=r; print(tag,r)
assert all(v is True or (isinstance(v,list) and all(v)) for r in out.values() for v in r.values())
json.dump({k:{kk:(vv if not isinstance(vv,list) else all(vv)) for kk,vv in v.items()} for k,v in out.items()},open('gate_replay.json','w'),indent=1); print('GATE_REPLAY_OK')
