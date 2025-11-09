import capstone

# interactive arm thumb disassembler

# Initialize Capstone in Thumb mode
md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB)
md.detail = True

offset = 0

while True:
    line = input("Enter hex halfwords (e.g. '480d 46c0'): ")
    if not line.strip():
        continue

    # Split by spaces, interpret each as 16-bit hex
    halfwords = line.strip().split()
    code_bytes = b''.join(int(hw, 16).to_bytes(2, byteorder="little") for hw in halfwords)

    # Disassemble starting at address 0
    for insn in md.disasm(code_bytes, offset):
        print("0x%04x:\t%-7s\t%s" % (insn.address, insn.mnemonic, insn.op_str))
        # note that branches show absolute target addresses, not the relative branch offsets
