# Regenerates research/static_findings.json from the exe (needs pe.py, capstone, pefile).
import json
from pe import *
out={'exe_sha256':'c94ad6c3ea54970a6d2da7a2719753210a7728a91698ed3405dd9125470ed98f',
     'TimeDateStamp':hex(pe.FILE_HEADER.TimeDateStamp),'SizeOfImage':hex(pe.OPTIONAL_HEADER.SizeOfImage),
     'CheckSum':hex(pe.OPTIONAL_HEADER.CheckSum),'vtables':{}}
for nm in ['.?AVPointClimbMovementMethod@pa@@','.?AVLadderClimbMovementMethod@pa@@','.?AVICharacterMovementMethod@pa@@',
           '.?AVClientEquipSlotActorComponent@pa@@','.?AVCommonEquipSlotActorComponent@pa@@',
           '.?AVServerEquipSlotActorComponent@pa@@','.?AVClientSkillActorComponent@pa@@',
           '.?AVAICondition_CheckEquipSlotNameItem@pa@@']:
    v=vtables(nm)
    out['vtables'][nm]=[{'rva':hex(a),'slots':[hex(x) for x in vfuncs(a,12)]} for a,_ in v]
out['equip_condition_worker']={'rva':'0x1837150','reads':'actor->[+0x68]->[+0x38] then calls 0x228a440'}
json.dump(out,open('static_findings.json','w'),indent=1); print('ok')
