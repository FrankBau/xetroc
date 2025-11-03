# pip install keystone-engine capstone

from capstone import *

# Your .hword values (Thumb instructions)
# hwords = [ 0x2156, 0x3203, 0x3b10, 0x000c, 0x18d5, 0x1ace, 0x9401, 0x9f01, 0xd0fe, 0xe7fe ]                   # all instruction types 
# hwords = [ 0x2026, 0x212a, 0x2200, 0x000b, 0xd002, 0x1812, 0x3b01, 0xe7fb, 0xe7fe ]                           # multiply by add 38 * 42 = 1596
# hwords = [ 0x2000, 0x9000, 0x2001, 0x9001, 0x9800, 0x9901, 0x1842, 0xd402, 0x9100, 0x9201, 0xe7f8, 0xe7fe ]     # fibonacci (using RAM)
hwords = [ 0x20ee, 0x2193, 0x1e0a, 0xd005, 0x1a42, 0xd801, 0x1a09, 0xe7f9, 0x1a40, 0xe7f7, 0xe7fe ]           # gcd(238,147) = 7

# Convert to byte stream (little-endian)
code = b''.join(hword.to_bytes(2, 'little') for hword in hwords)

# Initialize Capstone in Thumb mode
md = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
md.detail = True

# Disassemble and print
for i in md.disasm(code, 0x0000):
    print("0x%04x:\t%-7s\t%s" % (i.address, i.mnemonic, i.op_str))

# note: disassembly resolves branch targets to target addresses, not offsets