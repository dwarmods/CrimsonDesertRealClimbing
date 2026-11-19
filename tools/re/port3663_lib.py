import sys,re,difflib,json
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
def execsec(m,p):
    r=p-m.IB
    for s in m.pe.sections:
        if s.VirtualAddress<=r<s.VirtualAddress+max(s.Misc_VirtualSize,s.SizeOfRawData): return bool(s.Characteristics & 0x20000000)
    return False
def ns(m,vt):
    n=0
    while execsec(m,m.u64(vt+8*n)): n+=1
    return n
