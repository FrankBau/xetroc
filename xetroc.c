#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

// binary literals like 0b0000 need C23
// gcc -std=c23 -pedantic -Wall xetroc.c -o xetroc.exe


typedef uint32_t bits;   // a general type for holding bits 
typedef uint32_t word; 
typedef uint16_t halfword;
typedef  uint8_t byte;

// trace bit 0: printf decoded instructions
const bits trace = 0b1;

bits reg[16];   // registers

#define SP  14
#define PC  15

struct alu_flags_t {
    bool N;
    bool Z;
    bool C;
    bool V;
} alu_flags;

typedef enum {
    ALU_OP_AND,     // AND Rd, Rm        — Thumb ALU opcode 0000
    ALU_OP_EOR,     // EOR Rd, Rm        — Thumb ALU opcode 0001
    ALU_OP_LSL,     // LSL Rd, Rm        — Thumb ALU opcode 0010
    ALU_OP_LSR,     // LSR Rd, Rm        — Thumb ALU opcode 0011
    ALU_OP_ASR,     // ASR Rd, Rm        — Thumb ALU opcode 0100
    ALU_OP_ADC,     // ADC Rd, Rm        — Thumb ALU opcode 0101
    ALU_OP_SBC,     // SBC Rd, Rm        — Thumb ALU opcode 0110
    ALU_OP_ROR,     // ROR Rd, Rm        — Thumb ALU opcode 0111 (RRX if immediate shift == 0)
    ALU_OP_TST,     // TST Rn, Rm        — Thumb ALU opcode 1000 (flags only, no result)
    ALU_OP_NEG,     // NEG Rd, Rm        — Thumb ALU opcode 1001 (alias for RSBS Rd, Rm, #0)
    ALU_OP_CMP,     // CMP Rn, Rm        — Thumb ALU opcode 1010 (flags only, no result)
    ALU_OP_CMN,     // CMN Rn, Rm        — Thumb ALU opcode 1011 (flags only, no result)
    ALU_OP_ORR,     // ORR Rd, Rm        — Thumb ALU opcode 1100
    ALU_OP_MUL,     // MUL Rd, Rm        — Thumb ALU opcode 1101
    ALU_OP_BIC,     // BIC Rd, Rm        — Thumb ALU opcode 1110
    ALU_OP_MVN,     // MVN Rd, Rm        — Thumb ALU opcode 1111
    // Extended ops (not part of Thumb ALU 4-bit field, used in other formats):
    ALU_OP_ADD,     // ADD Rd, Rn, Rm    — Thumb format: multiple encodings (not ALU opcode field)
    ALU_OP_SUB      // SUB Rd, Rn, Rm    — Thumb format: multiple encodings (not ALU opcode field)
} alu_op_t;


const char* alu_op_names[] = {
    "and",   // ALU_OP_AND
    "eor",   // ALU_OP_EOR
    "lsl",   // ALU_OP_LSL
    "lsr",   // ALU_OP_LSR
    "asr",   // ALU_OP_ASR
    "adc",   // ALU_OP_ADC
    "sbc",   // ALU_OP_SBC
    "ror",   // ALU_OP_ROR
    "tst",   // ALU_OP_TST
    "neg",   // ALU_OP_NEG
    "cmp",   // ALU_OP_CMP
    "cmn",   // ALU_OP_CMN
    "orr",   // ALU_OP_ORR
    "mul",   // ALU_OP_MUL
    "bic",   // ALU_OP_BIC
    "mvn"    // ALU_OP_MVN
    "add",   // ALU_OP_ADD
    "sub",   // ALU_OP_SUB
};

#define FLASH_BASE  0x08000000
#define FLASH_SIZE        1024
#define   RAM_BASE  0x20000000
#define   RAM_SIZE        1024

// instruction memory, we use uint16_t here for simplicity

// const uint16_t flash[FLASH_SIZE] = { 0x2156, 0x3203, 0x3b10, 0x000c, 0x18d5, 0x1ace, 0x9401, 0x9f01, 0xd0fe, 0xe7fe };                   // all instruction types 
// const uint16_t flash[FLASH_SIZE] = { 0x2026, 0x212a, 0x2200, 0x000b, 0xd002, 0x1812, 0x3b01, 0xe7fb, 0xe7fe };                           // multiply by adding 38 * 42 = 1596
// const uint16_t flash[FLASH_SIZE] = { 0x20ee, 0x2193, 0x1e0a, 0xd005, 0x1a42, 0xd801, 0x1a09, 0xe7f9, 0x1a40, 0xe7f7, 0xe7fe };           // gcd(238,147) = 7
const uint16_t flash[FLASH_SIZE] = { 0x2000, 0x9000, 0x2001, 0x9001, 0x9800, 0x9901, 0x1842, 0xd402, 0x9100, 0x9201, 0xe7f8, 0xe7fe };   // fibonacci

// data memory, we use uint32_t here for simplicity
uint32_t ram[RAM_SIZE];

static inline uint32_t bits_extract(uint32_t x, unsigned hi, unsigned lo) {
    unsigned w = hi - lo + 1;
    if (w >= 32) return x >> lo;
    return (x >> lo) & ((1u << w) - 1u);
}

static inline int32_t sign_extend_u(uint32_t v, unsigned width) {
    assert((0u < width) && (width < 32u));
    uint32_t mask = (1u << width) - 1u;
    uint32_t raw = v & mask;
    uint32_t sign_bit = 1u << (width - 1);
    if (raw & sign_bit) {
        /* negative: fill upper bits with 1 */
        return (int32_t)(raw | ~mask);
    } else {
        return (int32_t)raw;
    }
}

/* zero-extend an unsigned value `v` that is `width` bits wide into uint32_t */
static inline uint32_t zero_extend_u(uint32_t v, unsigned width) {
    assert((0u < width) && (width < 32u));
    uint32_t mask = (1u << width) - 1u;
    return v & mask;
}

uint32_t alu(uint32_t a, uint32_t b, alu_op_t op, bool update_flags)
{
    if(op==ALU_OP_LSL || op==ALU_OP_LSR || op==ALU_OP_ASR || op==ALU_OP_ROR) {
        b = b & 0x1F; // treat shift amounts modulo 32
    }

    uint32_t result = 0;
    switch(op) {
        case ALU_OP_AND: result = a & b; break;
        case ALU_OP_EOR: result = a ^ b; break;
        case ALU_OP_LSL: result = a << b; break;
        case ALU_OP_LSR: result = a >> b; break;
        case ALU_OP_ASR: result = (int32_t)a >> b; break;
        case ALU_OP_ADC: result = a + b + alu_flags.C; break;
        case ALU_OP_SBC: result = a - b - (1 - alu_flags.C); break;
        case ALU_OP_ROR: result = (a >> b) | (a << (32 - b)); break;
        case ALU_OP_TST: result = a & b; break; // result not used
        case ALU_OP_NEG: a = 0; result = a - b; break; // == RSBS Rd, Rn, #0
        case ALU_OP_CMP: result = a - b; break; // result not used
        case ALU_OP_CMN: result = a + b; break; // result not used
        case ALU_OP_ORR: result = a | b; break;
        case ALU_OP_MUL: result = a * b; break;
        case ALU_OP_BIC: result = a & ~b; break;
        case ALU_OP_MVN: result = ~b; break;
        case ALU_OP_ADD: result = a + b; break; // alu_flags.C = 0; op = ALU_OP_ADC;
        case ALU_OP_SUB: result = a - b; break; // alu_flags.C = 1; op = ALU_OP_SBC;
    }

    // todo: special case handling
    // ROR Rd, Rm → rotates Rd by the value in Rm
    // ROR Rd, #imm → rotates Rd by an immediate
    // Special case: ROR Rd, #0 is interpreted as RRX, not a no-op

    if(update_flags) {
        alu_flags.N = (result >> 31) & 1;
        alu_flags.Z = (result == 0);

        switch(op) {
            case ALU_OP_ADD:
            case ALU_OP_ADC:
            case ALU_OP_CMN:
                alu_flags.C = (result < a);
                alu_flags.V = (((~(a ^ b)) & (a ^ result)) >> 31) & 1;
                break;

            case ALU_OP_SUB:
            case ALU_OP_SBC:
            case ALU_OP_CMP:
            case ALU_OP_NEG:
                alu_flags.C = a >= b;
                alu_flags.V = (((a ^ b) & (a ^ result)) >> 31) & 1;
                break;

            case ALU_OP_LSL:
                if(b != 0)
                    alu_flags.C = (a >> (32 - b)) & 1;
                break;

            case ALU_OP_LSR:
                if(b != 0)
                    alu_flags.C = (a >> (b - 1)) & 1;
                break;

            case ALU_OP_ASR:
                if(b != 0)
                    alu_flags.C = ((int32_t)a >> (b - 1)) & 1;
                break;

            case ALU_OP_ROR:
                if(b != 0)
                    alu_flags.C = (a >> (b - 1)) & 1;
                break;

            // All other ops: no C/V updates
            default:
                break;
        }
    }
    return result;
}

bool should_branch(uint8_t cc, struct alu_flags_t f) {
    switch (cc & 0xF) {
        case 0x0: return f.Z == 1;                        // EQ
        case 0x1: return f.Z == 0;                        // NE
        case 0x2: return f.C == 1;                        // CS/HS
        case 0x3: return f.C == 0;                        // CC/LO
        case 0x4: return f.N == 1;                        // MI
        case 0x5: return f.N == 0;                        // PL
        case 0x6: return f.V == 1;                        // VS
        case 0x7: return f.V == 0;                        // VC
        case 0x8: return (f.C == 1) && (f.Z == 0);        // HI
        case 0x9: return (f.C == 0) || (f.Z == 1);        // LS
        case 0xA: return f.N == f.V;                      // GE
        case 0xB: return f.N != f.V;                      // LT
        case 0xC: return (f.Z == 0) && (f.N == f.V);      // GT
        case 0xD: return (f.Z == 1) || (f.N != f.V);      // LE
        case 0xE: return true;                            // AL
        case 0xF: return false;                           // NV
    }
    return false;
}

const char* Bcc_names[16] = {
    "eq",  // 0x0: z == 1
    "ne",  // 0x1: z == 0
    "cs",  // 0x2: c == 1
    "cc",  // 0x3: c == 0
    "mi",  // 0x4: n == 1
    "pl",  // 0x5: n == 0
    "vs",  // 0x6: v == 1
    "vc",  // 0x7: v == 0
    "hi",  // 0x8: c == 1 && z == 0
    "ls",  // 0x9: c == 0 || z == 1
    "ge",  // 0xA: n == v
    "lt",  // 0xB: n != v
    "gt",  // 0xC: z == 0 && n == v
    "le",  // 0xD: z == 1 || n != v
    "al",  // 0xE: always
    "nv"   // 0xF: never
};

int main(int argc, char *argv[])
{
    // reset
    reg[PC] = FLASH_BASE;
    reg[SP] = RAM_BASE;

    for(;;) {
        // fetch
        bits pc = reg[PC];  // snapshot current PC
        bits addr = (pc - FLASH_BASE) >> 1; // byte address, relative to base
        bits ir = flash[addr];
        bits next_pc = pc + 2;  // default: advance to next instruction

        // decode and execute
        bits group = bits_extract(ir, 15,12); // instrcution group
        if(group == 0b0000) {         // MOVS <Rd>,<Rm>
            bits d = bits_extract(ir, 2, 0);
            bits n = bits_extract(ir, 5, 3);
            if(trace & 1) printf("movs r%d, r%d\n", d, n);
            bits a = reg[n];
            bits b = 0;
            bits r = alu(a, b, ALU_OP_ORR, true); // triggers flag update
            reg[d] = r;

        } else if(group == 0b0001) { // ADDS/SUBS <Rd>,<Rn>,<Rm>
            bits d = bits_extract(ir, 2, 0);
            bits n = bits_extract(ir, 5, 3);
            bits m = bits_extract(ir, 8, 6);
            alu_op_t alu_op = bits_extract(ir, 9, 9) ? ALU_OP_SUB : ALU_OP_ADD;
            if(trace & 1) printf("%ss r%d, r%d, r%d\n", alu_op_names[alu_op], d, n, m);
            bits a = reg[n];
            bits b = reg[m];
            bits r = alu(a, b, alu_op, true);
            reg[d] = r;

        } else if(group == 0b0010) { // MOVS <Rd>, #<imm8>
            bits d = bits_extract(ir, 10, 8);
            bits i = bits_extract(ir, 7, 0);
            if(trace & 1) printf("movs r%d, #%d\n", d, i);
            bits a = 0;
            bits b = zero_extend_u(i, 8);
            bits r = alu(a, b, ALU_OP_ORR, true); // triggers flag update
            reg[d] = r;

        } else if(group == 0b0011) { // ADDS/SUBS <Rdn>,#<imm8>
            bits d = bits_extract(ir, 10, 8);
            bits n = bits_extract(ir, 10, 8);
            bits i = bits_extract(ir,  7, 0);
            alu_op_t alu_op = bits_extract(ir, 11, 11) ? ALU_OP_SUB : ALU_OP_ADD;
            if(trace & 1) printf("%ss r%d, r%d, #%d\n", alu_op_names[alu_op], d, n, i);
            bits a = reg[n];
            bits b = zero_extend_u(i, 8);
            bits r = alu(a, b, alu_op, true);
            reg[d] = r;

        } else if(group == 0b1001) { // LDR/STR <Rt>,[<SP>,#<imm8>]
            bits t = bits_extract(ir, 10, 8);
            bits i = bits_extract(ir, 7, 0);
            bool is_ldr = bits_extract(ir, 11, 11);
            bits a = reg[SP];
            bits b = zero_extend_u(i, 8) << 2;  // scale by 4 for 32-bit ldr/str
            bits r = alu(a, b, ALU_OP_ADD, false);
            if(trace & 1) printf("%s r%d, [sp, #%d]\n", is_ldr ? "ldr" : "str", t, b);
            bits addr = (r - RAM_BASE) >> 2;    // our RAM has 32-bit elements
            if(is_ldr) {
                reg[t] = ram[addr];
            } else {
                ram[addr] = reg[t];
            }

        } else if(group == 0b1101) { // Bcc #<simm8>
            bits cc = bits_extract(ir, 11, 8);
            bits i =  bits_extract(ir, 7, 0);;
            int32_t simm8 = sign_extend_u(i, 8);
            if(trace & 1) printf("b%s %d\n", Bcc_names[cc], simm8);
            if(should_branch(cc, alu_flags)) {
                next_pc = pc + 4 + (sizeof(halfword) * simm8);
            } 

        } else if(group == 0b1110) { // B #<simm11>
            bits i =  bits_extract(ir, 10, 0);;
            int32_t simm11 = sign_extend_u(i, 11);
            if(trace & 1) printf("b %d\n", simm11);
            next_pc = pc + 4 + (sizeof(halfword) * simm11);
            if(pc == next_pc) {
                printf("endless loop detected -> halting simulation\n");
                return 0;
            }

        } else {
            printf("illegal instruction 0x%04x\n", ir);
            return -1;  // release the blue smoke 
        }

        reg[PC] = next_pc;
    }
}
