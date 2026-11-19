import importlib.util, sys, json, struct, re, hashlib
def load(path):
    src=open('pe.py').read().replace("import os; EXE=os.environ.get('CD_EXE','CrimsonDesert.exe')",f"EXE={path!r}")
    m=type(sys)('pe_'+str(abs(hash(path)))); exec(compile(src,'pe.py','exec'),m.__dict__); return m
import os
old=load(os.environ['CD_EXE_3576']); new=load(os.environ['CD_EXE_3663'])
for tag,m in (('old',old),('new',new)):
    p=m.pe; print(tag,hex(p.FILE_HEADER.TimeDateStamp),hex(p.OPTIONAL_HEADER.SizeOfImage),hex(p.OPTIONAL_HEADER.CheckSum),hex(m.IB))
names=['.?AVPointClimbMovementMethod@pa@@','.?AVLadderClimbMovementMethod@pa@@','.?AVClientEquipSlotActorComponent@pa@@','.?AVCommonEquipSlotActorComponent@pa@@','.?AVServerEquipSlotActorComponent@pa@@','.?AVClientSkillActorComponent@pa@@','.?AVICharacterMovementMethod@pa@@']
res={}
for nm in names:
    vo=old.vtables(nm); vn=new.vtables(nm)
    print(nm,'old',[(hex(a),o) for a,o in vo],'new',[(hex(a),o) for a,o in vn])
    res[nm]=dict(old=[(a,o) for a,o in vo],new=[(a,o) for a,o in vn])
json.dump(res,open('vtables_3663.json','w'))
