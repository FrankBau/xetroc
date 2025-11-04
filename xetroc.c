#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

// binary literals like 0b0000 need C23
// gcc -Og -std=c23 -pedantic -Wall -Wextra xetroc.c -o xetroc.exe

typedef uint32_t word;      // 32-bit machine word
typedef uint16_t halfword;  // 16-bit half word 
typedef uint8_t byte;       // 8-bit

typedef enum {
    ALU_OP_ADD,     // ADD Rd, Rn, Rm    — Thumb format: multiple encodings (not ALU opcode field)
    ALU_OP_SUB      // SUB Rd, Rn, Rm    — Thumb format: multiple encodings (not ALU opcode field)
    // ...
} alu_op_t;

const char* alu_op_names[] = {
    "add",   // ALU_OP_ADD
    "sub",   // ALU_OP_SUB
    // ...
};

struct alu_flags_t {
    bool N;
    bool Z;
    bool C;
    bool V;
} alu_flags;

#define FLASH_BASE  0x00000000
#define FLASH_SIZE        1024
#define   RAM_BASE  0x20000000
#define   RAM_SIZE        1024

// the hardware uses absolute 32-bit byte addresses
// but the simulation uses halfword rsp. word addresses 
// relative to the memory base address
#define FLASH(addr) (flash[((addr) - FLASH_BASE) >> 1])
#define RAM(addr)   (  ram[((addr) -   RAM_BASE) >> 2])

// instruction memory, we support only halfword access here for simplicity, real hardware can be accessed in more ways.
// const halfword flash[FLASH_SIZE] = { 0x2156, 0x3203, 0x3b10, 0x000c, 0x18d5, 0x1ace, 0x9401, 0x9f01, 0xd0fe, 0xe7fe };                   // all instruction types 
// const halfword flash[FLASH_SIZE] = { 0x2026, 0x212a, 0x2200, 0x000b, 0xd002, 0x1812, 0x3b01, 0xe7fb, 0xe7fe };                           // multiply by add 38 * 42 = 1596
const halfword flash[FLASH_SIZE] = { 0x2000, 0x9000, 0x2001, 0x9001, 0x9800, 0x9901, 0x1842, 0xd402, 0x9100, 0x9201, 0xe7f8, 0xe7fe };      // fibonacci (using RAM)
// const halfword flash[FLASH_SIZE] = { 0x20ee, 0x2193, 0x1e0a, 0xd005, 0x1a42, 0xd801, 0x1a09, 0xe7f9, 0x1a40, 0xe7f7, 0xe7fe };           // gcd(238,147) = 7

// data memory, we support only word access here for simplicity, real hardware can be accessed in more ways.
word ram[RAM_SIZE];

word reg[16];   // registers

#define SP  13
#define LR  14
#define PC  15

static inline word bits_extract(word x, unsigned hi, unsigned lo) {
    assert((lo <= hi) && (hi < 32u));
    unsigned w = hi - lo + 1;
    return (x >> lo) & ((1u << w) - 1u);
}

static inline word zero_extend(word v, unsigned width) {
    assert((0u < width) && (width < 32u));
    word mask = (1u << width) - 1u;
    return v & mask;
}

static inline word sign_extend(word v, unsigned width) {
    assert((0u < width) && (width < 32u));
    word sign = 1u << (width - 1);
    word mask = (1u << width) - 1;
    return (v & sign) ? (v | ~mask) : (v & mask);
}

// update_flags: 0: none 1: NZ 2: NZCV
word alu(word a, word b, alu_op_t op, word update_flags)
{
    word result = 0;
    switch(op) {
        case ALU_OP_ADD: result = a + b; break;
        case ALU_OP_SUB: result = a - b; break;
    }

    if(update_flags > 0) {
        alu_flags.N = (result >> 31) & 1;       // negative
        alu_flags.Z = (result == 0);            // zero
    }

    if(update_flags > 1) {
        switch (op) {
            case ALU_OP_ADD: {
                alu_flags.C = (result < a);     // unsigned overflow (carry)
                word xor_ab = a ^ b;
                word xor_ar = a ^ result;
                alu_flags.V = ((~xor_ab & xor_ar) >> 31) & 1; // signed overflow
                break;
            }

            case ALU_OP_SUB: {
                alu_flags.C = (a >= b);         // no borrow
                word xor_ab = a ^ b;
                word xor_ar = a ^ result;
                alu_flags.V = ((xor_ab & xor_ar) >> 31) & 1; // signed overflow
                break;
            }
        }
    }
    return result;
}

bool should_branch(word cc, struct alu_flags_t f) {
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
    "eq",  // 0x0: equal            z == 1              (==)
    "ne",  // 0x1: not equal        z == 0              (!=)
    "hs",  // 0x2: higher or same   c == 1              (unsigned >=)
    "lo",  // 0x3: lower            c == 0              (unsigned <)
    "mi",  // 0x4: minus            n == 1              (signed < 0)
    "pl",  // 0x5: plus             n == 0              (signed >= 0)
    "vs",  // 0x6: overflow set     v == 1              (signed overflow)
    "vc",  // 0x7: overflow clear   v == 0              (signed no overflow)
    "hi",  // 0x8: higher           c == 1 && z == 0    (unsigned >)
    "ls",  // 0x9: lower or same    c == 0 || z == 1    (unsigned <=)
    "ge",  // 0xA: greater or equal n == v              (signed >=)
    "lt",  // 0xB: less than        n != v              (signed <)
    "gt",  // 0xC: greater than     z == 0 && n == v    (signed >)
    "le",  // 0xD: less or equal    z == 1 || n != v    (signed <=)
    "al",  // 0xE: always           true
    "nv"   // 0xF: never            false
};

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    // reset
    reg[PC] = FLASH_BASE;
    reg[SP] = RAM_BASE;

    for(;;) {
        // fetch
        word pc = reg[PC];                              // get current program counter value
        word ir = FLASH(pc);                            // fetch instruction to instruction register (ir)
        reg[PC] += 2;                                   // increment program counter to next instruction

        // decode and execute
        word group = bits_extract(ir, 15,12);           // decode instruction group:

        if(group == 0b0000) {                           // MOVS <Rd>,<Rm>
            word d = bits_extract(ir, 2, 0);
            word n = bits_extract(ir, 5, 3);
            word a = reg[n];
            word b = 0;
            word r = alu(a, b, ALU_OP_ADD, 1);
            reg[d] = r;
            printf("0x%04x: movs r%d, r%d\n", pc, d, n);

        } else if(group == 0b0001) {                    // ADDS/SUBS <Rd>,<Rn>,<Rm>
            word d = bits_extract(ir, 2, 0);
            word n = bits_extract(ir, 5, 3);
            word m = bits_extract(ir, 8, 6);
            alu_op_t alu_op = bits_extract(ir, 9, 9) ? ALU_OP_SUB : ALU_OP_ADD;
            word a = reg[n];
            word b = reg[m];
            word r = alu(a, b, alu_op, 2);
            reg[d] = r;
            printf("0x%04x\t%ss r%d, r%d, r%d\n", pc, alu_op_names[alu_op], d, n, m);

        } else if(group == 0b0010) {                    // MOVS <Rd>, #<imm8>
            word d = bits_extract(ir, 10, 8);
            word i = zero_extend(ir, 8);
            word a = 0;
            word b = i;
            word r = alu(a, b, ALU_OP_ADD, 1);
            reg[d] = r;
            printf("0x%04x\tmovs r%d, #%d\n", pc, d, i);

        } else if(group == 0b0011) {                    // ADDS/SUBS <Rdn>,#<imm8>
            word d = bits_extract(ir, 10, 8);
            word n = bits_extract(ir, 10, 8);
            word i = zero_extend(ir, 8);
            alu_op_t alu_op = bits_extract(ir, 11, 11) ? ALU_OP_SUB : ALU_OP_ADD;
            word a = reg[n];
            word b = i;
            word r = alu(a, b, alu_op, 2);
            reg[d] = r;
            printf("0x%04x\t%ss r%d, r%d, #%d\n", pc, alu_op_names[alu_op], d, n, i);

        } else if(group == 0b1001) {                    // LDR/STR <Rt>,[<SP>,#<imm8>]
            word t = bits_extract(ir, 10, 8);
            word i = zero_extend(ir, 8);
            bool is_ldr = bits_extract(ir, 11, 11);
            word a = reg[SP];
            word b = i << 2;                            // scale by 4 for 32-bit ldr/str
            word r = alu(a, b, ALU_OP_ADD, 0);
            if(is_ldr) {
                reg[t] = RAM(r);                        // LDR
            } else {
                RAM(r) = reg[t];                        // STR
            }
            printf("0x%04x\t%s r%d, [sp, #%d]\n", pc, is_ldr ? "ldr" : "str", t, b);

        } else if(group == 0b1101) {                    // Bcc #<simm8>
            word cc = bits_extract(ir, 11, 8);          // branch condition code
            int32_t i = sign_extend(ir, 8);             // simm8
            word a = reg[PC] + 2;                       // program counter seen in the pipeline
            word b = i << 1;                            // branch offset in bytes
            word r = alu(a, b, ALU_OP_ADD, 0);
            bool do_branch = should_branch(cc, alu_flags);
            if(do_branch) {
                reg[PC] = r;                            // execute the branch (conditionally)
            } 
            printf("0x%04x\tb%s  %+d          ; 0x%04x (%s)\n", 
                pc, Bcc_names[cc], i, r, do_branch ? "taken" : "not taken");

        } else if(group == 0b1110) {                    // B #<simm11>
            int32_t i = sign_extend(ir, 11);            // simm11
            word a = reg[PC] + 2;                       // program counter seen in the pipeline
            word b = i << 1;                            // branch offset i in halfwords to bytes
            word r = alu(a, b, ALU_OP_ADD, 0);
            if(r == pc) {
                printf("endless self loop detected -> halting simulation\n");
                return 0;
            }
            reg[PC] = r;                                // execute the branch (uncoditionally)
            printf("0x%04x\tb    %+d          ; 0x%04x\n", pc, i, r);

        } else {
            printf("illegal instruction 0x%04x\n", ir);
            return -1;  // release the blue smoke 
        }
    }
}
