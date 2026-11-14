exec(open("port3663_lib.py").read())
import capstone
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64)
def calls(m,rva,n=600):
    o=m.rva2off(rva); out=[]
    for ins in md.disasm(m.d[o:o+n*15],m.IB+rva):
        if ins.mnemonic=='call' and ins.op_str.startswith('0x'): out.append(int(ins.op_str,16)-m.IB)
        if ins.mnemonic=='int3': break
    return out
a0=0x5c7ce28; b0=0x5cf8918
def search(m,vt,target,depth=3):
    res=[]
    for i in range(6):
        f=m.u64(vt+8*i)-m.IB
        stack=[(f,[(i,None)])]
        while stack:
            fn,path=stack.pop()
            if len(path)>depth: continue
            for ci,c in enumerate(calls(m,fn)):
                if c==target: res.append(path+[(fn,ci)])
                else: stack.append((c,path+[(fn,ci)]))
    return res
paths=search(old,a0,0x1837150)
print('old paths',[[ (hex(p[0]) if isinstance(p[0],int) and p[0]>100 else p[0],p[1]) for p in pp] for pp in paths][:5])
# mirror first path in new
for pp in paths[:3]:
    slot=pp[0][0]; fn=new.u64(b0+8*slot)-new.IB; fo=old.u64(a0+8*slot)-old.IB
    ok=True
    for (ofn,ci) in pp[1:]:
        co=calls(old,ofn); cn=calls(new,fn)
        if len(co)!=len(cn): print('call count differs',hex(ofn),len(co),hex(fn),len(cn))
        nxt=cn[ci]; print(f'  old {ofn:#x}[call {ci}] -> {co[ci]:#x} | new {fn:#x}[call {ci}] -> {nxt:#x} sim={sim(norm(old,co[ci]),norm(new,nxt)):.3f}')
        fn=nxt
    print('NEW equip worker candidate', hex(fn))
    print('\n'.join(new.dis(fn,30)))
