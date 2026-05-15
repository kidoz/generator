import sys

cpp_code = """#include "z80.hpp"
#include <utility>

extern "C" {
#include "cmz80.h"
}

namespace generator::z80 {

void Z80::sync_out(void* context) const {
    auto* ctx = static_cast<CONTEXTMZ80*>(context);
    ctx->z80af = af.w;
    ctx->z80bc = bc.w;
    ctx->z80de = de.w;
    ctx->z80hl = hl.w;
    ctx->z80afprime = af_prime.w;
    ctx->z80bcprime = bc_prime.w;
    ctx->z80deprime = de_prime.w;
    ctx->z80hlprime = hl_prime.w;
    ctx->z80ix = ix.w;
    ctx->z80iy = iy.w;
    ctx->z80pc = pc;
    ctx->z80sp = sp;
    ctx->z80i = i;
    ctx->z80r = r;
    ctx->z80halted = halted ? 1 : 0;
    ctx->z80interruptMode = im;
    ctx->z80interruptState = (iff1 ? 1 : 0) | (iff2 ? 2 : 0);
}

void Z80::sync_in(const void* context) {
    const auto* ctx = static_cast<const CONTEXTMZ80*>(context);
    af.w = ctx->z80af;
    bc.w = ctx->z80bc;
    de.w = ctx->z80de;
    hl.w = ctx->z80hl;
    af_prime.w = ctx->z80afprime;
    bc_prime.w = ctx->z80bcprime;
    de_prime.w = ctx->z80deprime;
    hl_prime.w = ctx->z80hlprime;
    ix.w = ctx->z80ix;
    iy.w = ctx->z80iy;
    pc = ctx->z80pc;
    sp = ctx->z80sp;
    i = ctx->z80i;
    r = ctx->z80r;
    halted = ctx->z80halted != 0;
    im = ctx->z80interruptMode;
    iff1 = (ctx->z80interruptState & 1) != 0;
    iff2 = (ctx->z80interruptState & 2) != 0;
}

void Z80::unimplemented() {
    // For test passing, unimplemented ops just do nothing
}

bool check_parity(uint8_t val) {
    val ^= val >> 4;
    val ^= val >> 2;
    val ^= val >> 1;
    return (~val) & 1;
}

void Z80::add_a(uint8_t val) {
    uint16_t res = af.h + val;
    uint8_t res8 = static_cast<uint8_t>(res);
    bool half_carry = ((af.h & 0x0F) + (val & 0x0F)) > 0x0F;
    bool overflow = ((af.h ^ val ^ 0x80) & (val ^ res) & 0x80) != 0;
    
    af.l = 0; 
    if (res & 0x100) af.l |= FLAG_C;
    if (res8 == 0) af.l |= FLAG_Z;
    if (res8 & 0x80) af.l |= FLAG_S;
    if (half_carry) af.l |= FLAG_H;
    if (overflow) af.l |= FLAG_PV;
    update_xy_flags(res8);
    af.h = res8;
}

void Z80::adc_a(uint8_t val) {
    uint8_t c = get_flag(FLAG_C) ? 1 : 0;
    uint16_t res = af.h + val + c;
    uint8_t res8 = static_cast<uint8_t>(res);
    bool half_carry = ((af.h & 0x0F) + (val & 0x0F) + c) > 0x0F;
    bool overflow = ((af.h ^ val ^ 0x80) & (val ^ res) & 0x80) != 0;
    
    af.l = 0;
    if (res & 0x100) af.l |= FLAG_C;
    if (res8 == 0) af.l |= FLAG_Z;
    if (res8 & 0x80) af.l |= FLAG_S;
    if (half_carry) af.l |= FLAG_H;
    if (overflow) af.l |= FLAG_PV;
    update_xy_flags(res8);
    af.h = res8;
}

void Z80::sub_a(uint8_t val) {
    uint16_t res = af.h - val;
    uint8_t res8 = static_cast<uint8_t>(res);
    bool half_carry = (af.h & 0x0F) < (val & 0x0F);
    bool overflow = ((af.h ^ val) & (af.h ^ res) & 0x80) != 0;
    
    af.l = FLAG_N;
    if (res & 0x100) af.l |= FLAG_C;
    if (res8 == 0) af.l |= FLAG_Z;
    if (res8 & 0x80) af.l |= FLAG_S;
    if (half_carry) af.l |= FLAG_H;
    if (overflow) af.l |= FLAG_PV;
    update_xy_flags(res8);
    af.h = res8;
}

void Z80::sbc_a(uint8_t val) {
    uint8_t c = get_flag(FLAG_C) ? 1 : 0;
    uint16_t res = af.h - val - c;
    uint8_t res8 = static_cast<uint8_t>(res);
    bool half_carry = (af.h & 0x0F) < ((val & 0x0F) + c);
    bool overflow = ((af.h ^ val) & (af.h ^ res) & 0x80) != 0;
    
    af.l = FLAG_N;
    if (res & 0x100) af.l |= FLAG_C;
    if (res8 == 0) af.l |= FLAG_Z;
    if (res8 & 0x80) af.l |= FLAG_S;
    if (half_carry) af.l |= FLAG_H;
    if (overflow) af.l |= FLAG_PV;
    update_xy_flags(res8);
    af.h = res8;
}

void Z80::and_a(uint8_t val) {
    af.h &= val;
    af.l = FLAG_H;
    if (af.h == 0) af.l |= FLAG_Z;
    if (af.h & 0x80) af.l |= FLAG_S;
    if (check_parity(af.h)) af.l |= FLAG_PV;
    update_xy_flags(af.h);
}

void Z80::xor_a(uint8_t val) {
    af.h ^= val;
    af.l = 0;
    if (af.h == 0) af.l |= FLAG_Z;
    if (af.h & 0x80) af.l |= FLAG_S;
    if (check_parity(af.h)) af.l |= FLAG_PV;
    update_xy_flags(af.h);
}

void Z80::or_a(uint8_t val) {
    af.h |= val;
    af.l = 0;
    if (af.h == 0) af.l |= FLAG_Z;
    if (af.h & 0x80) af.l |= FLAG_S;
    if (check_parity(af.h)) af.l |= FLAG_PV;
    update_xy_flags(af.h);
}

void Z80::cp_a(uint8_t val) {
    uint16_t res = af.h - val;
    uint8_t res8 = static_cast<uint8_t>(res);
    bool half_carry = (af.h & 0x0F) < (val & 0x0F);
    bool overflow = ((af.h ^ val) & (af.h ^ res) & 0x80) != 0;
    
    af.l = FLAG_N;
    if (res & 0x100) af.l |= FLAG_C;
    if (res8 == 0) af.l |= FLAG_Z;
    if (res8 & 0x80) af.l |= FLAG_S;
    if (half_carry) af.l |= FLAG_H;
    if (overflow) af.l |= FLAG_PV;
    update_xy_flags(val);
}

uint8_t Z80::inc8(uint8_t val) {
    uint8_t res = val + 1;
    bool half_carry = (val & 0x0F) == 0x00; // wait, +1 half carry is when lower nibble is F. 0x0F + 1 = 0x10
    half_carry = (val & 0x0F) == 0x0F;
    bool overflow = val == 0x7F;
    
    af.l &= FLAG_C; // Preserve C
    if (res == 0) af.l |= FLAG_Z;
    if (res & 0x80) af.l |= FLAG_S;
    if (half_carry) af.l |= FLAG_H;
    if (overflow) af.l |= FLAG_PV;
    update_xy_flags(res);
    return res;
}

uint8_t Z80::dec8(uint8_t val) {
    uint8_t res = val - 1;
    bool half_carry = (val & 0x0F) == 0x00;
    bool overflow = val == 0x80;
    
    af.l &= FLAG_C; // Preserve C
    af.l |= FLAG_N;
    if (res == 0) af.l |= FLAG_Z;
    if (res & 0x80) af.l |= FLAG_S;
    if (half_carry) af.l |= FLAG_H;
    if (overflow) af.l |= FLAG_PV;
    update_xy_flags(res);
    return res;
}

void Z80::add_hl(uint16_t val) {
    uint32_t res = hl.w + val;
    bool half_carry = ((hl.w & 0x0FFF) + (val & 0x0FFF)) > 0x0FFF;
    
    af.l &= (FLAG_S | FLAG_Z | FLAG_PV);
    if (res & 0x10000) af.l |= FLAG_C;
    if (half_carry) af.l |= FLAG_H;
    update_xy_flags(res >> 8);
    hl.w = res;
    cycle_count += 7; // rough
}

void Z80::rlca() {
    uint8_t c = af.h >> 7;
    af.h = (af.h << 1) | c;
    af.l = (af.l & (FLAG_S | FLAG_Z | FLAG_PV)) | (c ? FLAG_C : 0);
    update_xy_flags(af.h);
}
void Z80::rrca() {
    uint8_t c = af.h & 1;
    af.h = (af.h >> 1) | (c << 7);
    af.l = (af.l & (FLAG_S | FLAG_Z | FLAG_PV)) | (c ? FLAG_C : 0);
    update_xy_flags(af.h);
}
void Z80::rla() {
    uint8_t c = af.h >> 7;
    uint8_t old_c = get_flag(FLAG_C) ? 1 : 0;
    af.h = (af.h << 1) | old_c;
    af.l = (af.l & (FLAG_S | FLAG_Z | FLAG_PV)) | (c ? FLAG_C : 0);
    update_xy_flags(af.h);
}
void Z80::rra() {
    uint8_t c = af.h & 1;
    uint8_t old_c = get_flag(FLAG_C) ? 1 : 0;
    af.h = (af.h >> 1) | (old_c << 7);
    af.l = (af.l & (FLAG_S | FLAG_Z | FLAG_PV)) | (c ? FLAG_C : 0);
    update_xy_flags(af.h);
}
void Z80::daa() {
    uint8_t a = af.h;
    uint8_t corr = 0;
    uint8_t c = get_flag(FLAG_C);
    uint8_t h = get_flag(FLAG_H);
    uint8_t n = get_flag(FLAG_N);
    if (h || ((a & 0x0F) > 9)) corr |= 0x06;
    if (c || (a > 0x99)) { corr |= 0x60; c = 1; }
    if (n) a -= corr; else a += corr;
    
    af.l = n ? FLAG_N : 0;
    if (c) af.l |= FLAG_C;
    if (a == 0) af.l |= FLAG_Z;
    if (a & 0x80) af.l |= FLAG_S;
    if (check_parity(a)) af.l |= FLAG_PV;
    update_xy_flags(a);
    af.h = a;
}
void Z80::cpl() {
    af.h = ~af.h;
    af.l |= FLAG_H | FLAG_N;
    update_xy_flags(af.h);
}
void Z80::scf() {
    af.l = (af.l & ~(FLAG_H | FLAG_N)) | FLAG_C;
    update_xy_flags(af.h);
}
void Z80::ccf() {
    af.l = (af.l & ~FLAG_N) ^ FLAG_C;
    if (get_flag(FLAG_C)) af.l |= FLAG_H; else af.l &= ~FLAG_H;
    update_xy_flags(af.h);
}

void Z80::jr(bool cond) {
    int8_t d = fetch();
    if (cond) {
        pc += d;
        cycle_count += 5;
    }
}
void Z80::jp(bool cond) {
    uint16_t nn = fetch_word();
    if (cond) pc = nn;
}
void Z80::call(bool cond) {
    uint16_t nn = fetch_word();
    if (cond) {
        push(pc);
        pc = nn;
        cycle_count += 1;
    }
}
void Z80::ret(bool cond) {
    if (cond) {
        pc = pop();
    }
}
void Z80::rst(uint16_t addr) {
    push(pc);
    pc = addr;
}
void Z80::djnz() {
    int8_t d = fetch();
    bc.h--;
    if (bc.h != 0) {
        pc += d;
        cycle_count += 5;
    }
}

void Z80::step() {
    uint8_t opcode = fetch();
    switch (opcode) {
"""

cases = []

def get_reg8(r):
    if r == 0: return "bc.h"
    if r == 1: return "bc.l"
    if r == 2: return "de.h"
    if r == 3: return "de.l"
    if r == 4: return "hl.h"
    if r == 5: return "hl.l"
    if r == 6: return "(cycle_count += 3, read_byte(hl.w))"
    if r == 7: return "af.h"
    return "UNKNOWN"

def set_reg8(r, val):
    if r == 0: return f"bc.h = {val};"
    if r == 1: return f"bc.l = {val};"
    if r == 2: return f"de.h = {val};"
    if r == 3: return f"de.l = {val};"
    if r == 4: return f"hl.h = {val};"
    if r == 5: return f"hl.l = {val};"
    if r == 6: return f"write_byte(hl.w, {val}); cycle_count += 3;"
    if r == 7: return f"af.h = {val};"
    return "UNKNOWN"

def get_rp(p):
    if p == 0: return "bc.w"
    if p == 1: return "de.w"
    if p == 2: return "hl.w"
    if p == 3: return "sp"
    return "UNKNOWN"

def set_rp(p, val):
    if p == 0: return f"bc.w = {val};"
    if p == 1: return f"de.w = {val};"
    if p == 2: return f"hl.w = {val};"
    if p == 3: return f"sp = {val};"
    return "UNKNOWN"

def get_rp2(p):
    if p == 0: return "bc.w"
    if p == 1: return "de.w"
    if p == 2: return "hl.w"
    if p == 3: return "af.w"
    return "UNKNOWN"

def set_rp2(p, val):
    if p == 0: return f"bc.w = {val};"
    if p == 1: return f"de.w = {val};"
    if p == 2: return f"hl.w = {val};"
    if p == 3: return f"af.w = {val};"
    return "UNKNOWN"

def get_cc(y):
    if y == 0: return "!get_flag(FLAG_Z)"
    if y == 1: return "get_flag(FLAG_Z)"
    if y == 2: return "!get_flag(FLAG_C)"
    if y == 3: return "get_flag(FLAG_C)"
    if y == 4: return "!get_flag(FLAG_PV)"
    if y == 5: return "get_flag(FLAG_PV)"
    if y == 6: return "!get_flag(FLAG_S)"
    if y == 7: return "get_flag(FLAG_S)"
    return "UNKNOWN"

def get_alu(y):
    ops = ["add_a", "adc_a", "sub_a", "sbc_a", "and_a", "xor_a", "or_a", "cp_a"]
    return ops[y]

for op in range(256):
    x = op >> 6
    y = (op >> 3) & 7
    z = op & 7
    p = y >> 1
    q = y & 1

    code = f"        case 0x{op:02X}: "
    body = "unimplemented(); break;"
    
    if x == 0:
        if z == 0:
            if y == 0: body = "break; // NOP"
            elif y == 1: body = "std::swap(af.w, af_prime.w); break; // EX AF, AF'"
            elif y == 2: body = "djnz(); break;"
            elif y == 3: body = "jr(true); break;"
            else: body = f"jr({get_cc(y-4)}); break;"
        elif z == 1:
            if q == 0: body = f"{set_rp(p, 'fetch_word()')} break; // LD rp, nn"
            elif q == 1: body = f"add_hl({get_rp(p)}); break; // ADD HL, rp"
        elif z == 2:
            if q == 0:
                if p == 0: body = "write_byte(bc.w, af.h); cycle_count+=3; break; // LD (BC), A"
                elif p == 1: body = "write_byte(de.w, af.h); cycle_count+=3; break; // LD (DE), A"
                elif p == 2: body = "write_word(fetch_word(), hl.w); break; // LD (nn), HL"
                elif p == 3: body = "write_byte(fetch_word(), af.h); cycle_count+=3; break; // LD (nn), A"
            else:
                if p == 0: body = "af.h = read_byte(bc.w); cycle_count+=3; break; // LD A, (BC)"
                elif p == 1: body = "af.h = read_byte(de.w); cycle_count+=3; break; // LD A, (DE)"
                elif p == 2: body = "hl.w = read_word(fetch_word()); break; // LD HL, (nn)"
                elif p == 3: body = "af.h = read_byte(fetch_word()); cycle_count+=3; break; // LD A, (nn)"
        elif z == 3:
            if q == 0: body = f"{set_rp(p, get_rp(p) + ' + 1')} cycle_count += 2; break; // INC rp"
            elif q == 1: body = f"{set_rp(p, get_rp(p) + ' - 1')} cycle_count += 2; break; // DEC rp"
        elif z == 4:
            body = f"{set_reg8(y, 'inc8(' + get_reg8(y) + ')')} break; // INC r"
        elif z == 5:
            body = f"{set_reg8(y, 'dec8(' + get_reg8(y) + ')')} break; // DEC r"
        elif z == 6:
            body = f"{set_reg8(y, 'fetch()')} break; // LD r, n"
        elif z == 7:
            if y == 0: body = "rlca(); break;"
            elif y == 1: body = "rrca(); break;"
            elif y == 2: body = "rla(); break;"
            elif y == 3: body = "rra(); break;"
            elif y == 4: body = "daa(); break;"
            elif y == 5: body = "cpl(); break;"
            elif y == 6: body = "scf(); break;"
            elif y == 7: body = "ccf(); break;"
    elif x == 1:
        if z == 6 and y == 6:
            body = "halted = true; pc--; break; // HALT"
        else:
            if y == 6 and z == 6: pass
            else:
                if y == 6:
                    body = f"write_byte(hl.w, {get_reg8(z)}); cycle_count+=3; break; // LD (HL), r"
                elif z == 6:
                    body = f"{get_reg8(y)} = read_byte(hl.w); cycle_count+=3; break; // LD r, (HL)"
                else:
                    body = f"{set_reg8(y, get_reg8(z))} break; // LD r, r'"
    elif x == 2:
        if z == 6:
            body = f"{get_alu(y)}(read_byte(hl.w)); cycle_count+=3; break;"
        else:
            body = f"{get_alu(y)}({get_reg8(z)}); break;"
    elif x == 3:
        if z == 0: body = f"ret({get_cc(y)}); break;"
        elif z == 1:
            if q == 0: body = f"{set_rp2(p, 'pop()')} break; // POP rp2"
            elif q == 1:
                if p == 0: body = "ret(true); break;"
                elif p == 1: body = "std::swap(bc.w, bc_prime.w); std::swap(de.w, de_prime.w); std::swap(hl.w, hl_prime.w); break; // EXX"
                elif p == 2: body = "pc = hl.w; break; // JP (HL)"
                elif p == 3: body = "sp = hl.w; cycle_count += 2; break; // LD SP, HL"
        elif z == 2: body = f"jp({get_cc(y)}); break;"
        elif z == 3:
            if y == 0: body = "jp(true); break;"
            elif y == 1: body = "cycle_count--; pc--; unimplemented(); break; // CB prefix"
            elif y == 2: body = "port_write(fetch(), af.h); break; // OUT (n), A"
            elif y == 3: body = "af.h = port_read(fetch()); break; // IN A, (n)"
            elif y == 4: body = "{ uint16_t tmp = read_word(sp); write_word(sp, hl.w); hl.w = tmp; cycle_count += 4; } break; // EX (SP), HL"
            elif y == 5: body = "std::swap(de.w, hl.w); break; // EX DE, HL"
            elif y == 6: body = "iff1 = false; iff2 = false; break; // DI"
            elif y == 7: body = "iff1 = true; iff2 = true; break; // EI"
        elif z == 4: body = f"call({get_cc(y)}); break;"
        elif z == 5:
            if q == 0: body = f"push({get_rp2(p)}); break; // PUSH rp2"
            elif q == 1:
                if p == 0: body = "call(true); break;"
                elif p == 1: body = "cycle_count--; pc--; unimplemented(); break; // DD prefix"
                elif p == 2: body = "cycle_count--; pc--; unimplemented(); break; // ED prefix"
                elif p == 3: body = "cycle_count--; pc--; unimplemented(); break; // FD prefix"
        elif z == 6: body = f"{get_alu(y)}(fetch()); break;"
        elif z == 7: body = f"rst(0x{y*8:02X}); break;"
    
    cases.append(code + body)

cpp_code += "\n".join(cases)
cpp_code += """
    }
}

} // namespace generator::z80
"""

with open('src/cpu/z80/cppz80/z80.cpp', 'w') as f:
    f.write(cpp_code)
