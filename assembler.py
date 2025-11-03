# pip install keystone-engine capstone

from keystone import *

ks = Ks(KS_ARCH_ARM, KS_MODE_THUMB)
encoding, _ = ks.asm("""
    movs r0, #0xee 
    movs r1, #0x93 
loop:
    subs r2, r1, #0
    beq halt
    subs r2, r0, r1
    bhi.n else   
    subs r1, r1, r0
    b.n loop
else:     
    subs r0, r0, r1
    b.n loop
halt:
    b.n halt
""")

# Group bytes into 16-bit halfwords (little-endian)
hwords = [f" 0x{encoding[i+1]:02x}{encoding[i]:02x}" for i in range(0, len(encoding), 2)]
for hword in hwords:
    print(hword, end=',')
