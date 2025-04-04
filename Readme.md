# Xetroc 1.0

Xetroc is a digital circuit design of a minimalistic 32-bit computer for educational purposes.

![top level diagram](main.svg)

The CPU has 8 general purpose registers R0..R7, a program counter (PC), and a stack pointer (SP). 

It supports only a minimal set of instructions: 
- data processing (register-immediate): `ADDS/SUBS/MOVS Rd, #imm8`
- data processing (register-register): `ADDS/SUBS/MOVS Rd, Rn, Rm`
- memory access: `LDR/STR Rt, [SP, #imm8]`
- control flow: `Bcc #simm8` and `B #simm11`

Besides the CPU, the computer has a 64k 16-bit code ROM and a 64kB 32-bit data RAM.

The complete design is contained in the `main.circ` file, which is used with the  [logisim-evolution](https://github.com/logisim-evolution/logisim-evolution) software. Version 3.9 was used for testing.

Sample programs are contained in the `*_rom.txt`files which can be loaded into the ROM and executed in a logisim-evolution simulation.

The circuit is based on the Single Cycle Processor in https://github.com/dgsmith1988/Logisim-ARM-Processors which in turn is based on the book
[Digital Design and Computer Architecture - ARM Edition](https://pages.hmc.edu/harris/ddca/ddcaarm.html) by Sarah L. Harris and David Harris.

Signal and component names and the overall structure is very close to the book. 
The main difference is the Instruction Decoder which is for 16-bit instructions, not 32-bit instructions.
For instruction encoding and details see the Arm online documentation [16-bit Thumb instruction encoding](https://developer.arm.com/documentation/ddi0406/b/Application-Level-Architecture/Thumb-Instruction-Set-Encoding/16-bit-Thumb-instruction-encoding).

The Instruction Decoder in the Control Unit was generated from the `Instruction_Decoder_TruthTable.txt` 
truth table description which makes it easier to understand and modify. 
There are no HDL components involved.

## Limitations
The single cycle design is a simplification which is not aimed at mimicking any existing processor in its details.

- There are two separate memories RAM and ROM which is essential for a single cycle design,
- the most significant address bits are not evaluated for the ROM and the RAM,
- RAM access is 32-bit only,
- the stack pointer is fixed at RAM address 0x20000000 and cannot be modified,
- the Data Path is limited and cannot support some instructions like STR Rt, [Rn, Rm] or LDR Rt, [PC, #imm8],
- the Control Unit decodes only a subset of instruction formats.

At the moment, the goal is to keep the design simple, not to create a fully blown model. If you find a bug or have a suggestion, feel free to open an issue or submit a pull request. 
