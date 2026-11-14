exec(open("port3663_lib.py").read())
nm='.?AVAICondition_CheckEquipSlotNameItem@pa@@'
vo=old.vtables(nm); vn=new.vtables(nm); print('old',[hex(a) for a,_ in vo],'new',[hex(a) for a,_ in vn])
a0=vo[0][0]; b0=vn[0][0]
for i in range(min(ns(old,a0),ns(new,b0))):
    fo=old.u64(a0+8*i)-old.IB; fn=new.u64(b0+8*i)-new.IB
    s=sim(norm(old,fo),norm(new,fn)); flag=' <== old worker' if fo==0x1837150 else ''
    print(i,hex(fo),hex(fn),round(s,3),flag)
print('\n'.join(old.dis(0x1837150,30)))
