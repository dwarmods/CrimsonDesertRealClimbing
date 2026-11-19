import os
import importlib.util, sys, json, struct, re, hashlib
def load(path):
    src=open('pe.py').read().replace("EXE='/workspace/exe/CrimsonDesert.exe'",f"EXE={path!r}")
    m=type(sys)('pe_'+str(abs(hash(path)))); exec(compile(src,'pe.py','exec'),m.__dict__); return m
old=load(os.environ.get('CD_EXE_OLD','CrimsonDesert_3576.exe')); new=load(os.environ.get('CD_EXE_NEW','CrimsonDesert_3663.exe'))
for tag,m in (('old',old),('new',new)):
    p=m.pe; print(tag,hex(p.FILE_HEADER.TimeDateStamp),hex(p.OPTIONAL_HEADER.SizeOfImage),hex(p.OPTIONAL_HEADER.CheckSum),hex(m.IB))
names=['.?AVPointClimbMovementMethod@pa@@','.?AVLadderClimbMovementMethod@pa@@','.?AVClientEquipSlotActorComponent@pa@@','.?AVCommonEquipSlotActorComponent@pa@@','.?AVServerEquipSlotActorComponent@pa@@','.?AVClientSkillActorComponent@pa@@','.?AVICharacterMovementMethod@pa@@']
res={}
for nm in names:
    vo=old.vtables(nm); vn=new.vtables(nm)
    print(nm,'old',[(hex(a),o) for a,o in vo],'new',[(hex(a),o) for a,o in vn])
    res[nm]=dict(old=[(a,o) for a,o in vo],new=[(a,o) for a,o in vn])
json.dump(res,open('vtables_3663.json','w'))
