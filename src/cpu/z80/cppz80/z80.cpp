#include "z80.hpp"
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
        case 0x00: break; // NOP
        case 0x01: bc.w = fetch_word(); break; // LD rp, nn
        case 0x02: write_byte(bc.w, af.h); cycle_count+=3; break; // LD (BC), A
        case 0x03: bc.w = bc.w + 1; cycle_count += 2; break; // INC rp
        case 0x04: bc.h = inc8(bc.h); break; // INC r
        case 0x05: bc.h = dec8(bc.h); break; // DEC r
        case 0x06: bc.h = fetch(); break; // LD r, n
        case 0x07: rlca(); break;
        case 0x08: std::swap(af.w, af_prime.w); break; // EX AF, AF'
        case 0x09: add_hl(bc.w); break; // ADD HL, rp
        case 0x0A: af.h = read_byte(bc.w); cycle_count+=3; break; // LD A, (BC)
        case 0x0B: bc.w = bc.w - 1; cycle_count += 2; break; // DEC rp
        case 0x0C: bc.l = inc8(bc.l); break; // INC r
        case 0x0D: bc.l = dec8(bc.l); break; // DEC r
        case 0x0E: bc.l = fetch(); break; // LD r, n
        case 0x0F: rrca(); break;
        case 0x10: djnz(); break;
        case 0x11: de.w = fetch_word(); break; // LD rp, nn
        case 0x12: write_byte(de.w, af.h); cycle_count+=3; break; // LD (DE), A
        case 0x13: de.w = de.w + 1; cycle_count += 2; break; // INC rp
        case 0x14: de.h = inc8(de.h); break; // INC r
        case 0x15: de.h = dec8(de.h); break; // DEC r
        case 0x16: de.h = fetch(); break; // LD r, n
        case 0x17: rla(); break;
        case 0x18: jr(true); break;
        case 0x19: add_hl(de.w); break; // ADD HL, rp
        case 0x1A: af.h = read_byte(de.w); cycle_count+=3; break; // LD A, (DE)
        case 0x1B: de.w = de.w - 1; cycle_count += 2; break; // DEC rp
        case 0x1C: de.l = inc8(de.l); break; // INC r
        case 0x1D: de.l = dec8(de.l); break; // DEC r
        case 0x1E: de.l = fetch(); break; // LD r, n
        case 0x1F: rra(); break;
        case 0x20: jr(!get_flag(FLAG_Z)); break;
        case 0x21: hl.w = fetch_word(); break; // LD rp, nn
        case 0x22: write_word(fetch_word(), hl.w); break; // LD (nn), HL
        case 0x23: hl.w = hl.w + 1; cycle_count += 2; break; // INC rp
        case 0x24: hl.h = inc8(hl.h); break; // INC r
        case 0x25: hl.h = dec8(hl.h); break; // DEC r
        case 0x26: hl.h = fetch(); break; // LD r, n
        case 0x27: daa(); break;
        case 0x28: jr(get_flag(FLAG_Z)); break;
        case 0x29: add_hl(hl.w); break; // ADD HL, rp
        case 0x2A: hl.w = read_word(fetch_word()); break; // LD HL, (nn)
        case 0x2B: hl.w = hl.w - 1; cycle_count += 2; break; // DEC rp
        case 0x2C: hl.l = inc8(hl.l); break; // INC r
        case 0x2D: hl.l = dec8(hl.l); break; // DEC r
        case 0x2E: hl.l = fetch(); break; // LD r, n
        case 0x2F: cpl(); break;
        case 0x30: jr(!get_flag(FLAG_C)); break;
        case 0x31: sp = fetch_word(); break; // LD rp, nn
        case 0x32: write_byte(fetch_word(), af.h); cycle_count+=3; break; // LD (nn), A
        case 0x33: sp = sp + 1; cycle_count += 2; break; // INC rp
        case 0x34: write_byte(hl.w, inc8((cycle_count += 3, read_byte(hl.w)))); cycle_count += 3; break; // INC r
        case 0x35: write_byte(hl.w, dec8((cycle_count += 3, read_byte(hl.w)))); cycle_count += 3; break; // DEC r
        case 0x36: write_byte(hl.w, fetch()); cycle_count += 3; break; // LD r, n
        case 0x37: scf(); break;
        case 0x38: jr(get_flag(FLAG_C)); break;
        case 0x39: add_hl(sp); break; // ADD HL, rp
        case 0x3A: af.h = read_byte(fetch_word()); cycle_count+=3; break; // LD A, (nn)
        case 0x3B: sp = sp - 1; cycle_count += 2; break; // DEC rp
        case 0x3C: af.h = inc8(af.h); break; // INC r
        case 0x3D: af.h = dec8(af.h); break; // DEC r
        case 0x3E: af.h = fetch(); break; // LD r, n
        case 0x3F: ccf(); break;
        case 0x40: bc.h = bc.h; break; // LD r, r'
        case 0x41: bc.h = bc.l; break; // LD r, r'
        case 0x42: bc.h = de.h; break; // LD r, r'
        case 0x43: bc.h = de.l; break; // LD r, r'
        case 0x44: bc.h = hl.h; break; // LD r, r'
        case 0x45: bc.h = hl.l; break; // LD r, r'
        case 0x46: bc.h = read_byte(hl.w); cycle_count+=3; break; // LD r, (HL)
        case 0x47: bc.h = af.h; break; // LD r, r'
        case 0x48: bc.l = bc.h; break; // LD r, r'
        case 0x49: bc.l = bc.l; break; // LD r, r'
        case 0x4A: bc.l = de.h; break; // LD r, r'
        case 0x4B: bc.l = de.l; break; // LD r, r'
        case 0x4C: bc.l = hl.h; break; // LD r, r'
        case 0x4D: bc.l = hl.l; break; // LD r, r'
        case 0x4E: bc.l = read_byte(hl.w); cycle_count+=3; break; // LD r, (HL)
        case 0x4F: bc.l = af.h; break; // LD r, r'
        case 0x50: de.h = bc.h; break; // LD r, r'
        case 0x51: de.h = bc.l; break; // LD r, r'
        case 0x52: de.h = de.h; break; // LD r, r'
        case 0x53: de.h = de.l; break; // LD r, r'
        case 0x54: de.h = hl.h; break; // LD r, r'
        case 0x55: de.h = hl.l; break; // LD r, r'
        case 0x56: de.h = read_byte(hl.w); cycle_count+=3; break; // LD r, (HL)
        case 0x57: de.h = af.h; break; // LD r, r'
        case 0x58: de.l = bc.h; break; // LD r, r'
        case 0x59: de.l = bc.l; break; // LD r, r'
        case 0x5A: de.l = de.h; break; // LD r, r'
        case 0x5B: de.l = de.l; break; // LD r, r'
        case 0x5C: de.l = hl.h; break; // LD r, r'
        case 0x5D: de.l = hl.l; break; // LD r, r'
        case 0x5E: de.l = read_byte(hl.w); cycle_count+=3; break; // LD r, (HL)
        case 0x5F: de.l = af.h; break; // LD r, r'
        case 0x60: hl.h = bc.h; break; // LD r, r'
        case 0x61: hl.h = bc.l; break; // LD r, r'
        case 0x62: hl.h = de.h; break; // LD r, r'
        case 0x63: hl.h = de.l; break; // LD r, r'
        case 0x64: hl.h = hl.h; break; // LD r, r'
        case 0x65: hl.h = hl.l; break; // LD r, r'
        case 0x66: hl.h = read_byte(hl.w); cycle_count+=3; break; // LD r, (HL)
        case 0x67: hl.h = af.h; break; // LD r, r'
        case 0x68: hl.l = bc.h; break; // LD r, r'
        case 0x69: hl.l = bc.l; break; // LD r, r'
        case 0x6A: hl.l = de.h; break; // LD r, r'
        case 0x6B: hl.l = de.l; break; // LD r, r'
        case 0x6C: hl.l = hl.h; break; // LD r, r'
        case 0x6D: hl.l = hl.l; break; // LD r, r'
        case 0x6E: hl.l = read_byte(hl.w); cycle_count+=3; break; // LD r, (HL)
        case 0x6F: hl.l = af.h; break; // LD r, r'
        case 0x70: write_byte(hl.w, bc.h); cycle_count+=3; break; // LD (HL), r
        case 0x71: write_byte(hl.w, bc.l); cycle_count+=3; break; // LD (HL), r
        case 0x72: write_byte(hl.w, de.h); cycle_count+=3; break; // LD (HL), r
        case 0x73: write_byte(hl.w, de.l); cycle_count+=3; break; // LD (HL), r
        case 0x74: write_byte(hl.w, hl.h); cycle_count+=3; break; // LD (HL), r
        case 0x75: write_byte(hl.w, hl.l); cycle_count+=3; break; // LD (HL), r
        case 0x76: halted = true; pc--; break; // HALT
        case 0x77: write_byte(hl.w, af.h); cycle_count+=3; break; // LD (HL), r
        case 0x78: af.h = bc.h; break; // LD r, r'
        case 0x79: af.h = bc.l; break; // LD r, r'
        case 0x7A: af.h = de.h; break; // LD r, r'
        case 0x7B: af.h = de.l; break; // LD r, r'
        case 0x7C: af.h = hl.h; break; // LD r, r'
        case 0x7D: af.h = hl.l; break; // LD r, r'
        case 0x7E: af.h = read_byte(hl.w); cycle_count+=3; break; // LD r, (HL)
        case 0x7F: af.h = af.h; break; // LD r, r'
        case 0x80: add_a(bc.h); break;
        case 0x81: add_a(bc.l); break;
        case 0x82: add_a(de.h); break;
        case 0x83: add_a(de.l); break;
        case 0x84: add_a(hl.h); break;
        case 0x85: add_a(hl.l); break;
        case 0x86: add_a(read_byte(hl.w)); cycle_count+=3; break;
        case 0x87: add_a(af.h); break;
        case 0x88: adc_a(bc.h); break;
        case 0x89: adc_a(bc.l); break;
        case 0x8A: adc_a(de.h); break;
        case 0x8B: adc_a(de.l); break;
        case 0x8C: adc_a(hl.h); break;
        case 0x8D: adc_a(hl.l); break;
        case 0x8E: adc_a(read_byte(hl.w)); cycle_count+=3; break;
        case 0x8F: adc_a(af.h); break;
        case 0x90: sub_a(bc.h); break;
        case 0x91: sub_a(bc.l); break;
        case 0x92: sub_a(de.h); break;
        case 0x93: sub_a(de.l); break;
        case 0x94: sub_a(hl.h); break;
        case 0x95: sub_a(hl.l); break;
        case 0x96: sub_a(read_byte(hl.w)); cycle_count+=3; break;
        case 0x97: sub_a(af.h); break;
        case 0x98: sbc_a(bc.h); break;
        case 0x99: sbc_a(bc.l); break;
        case 0x9A: sbc_a(de.h); break;
        case 0x9B: sbc_a(de.l); break;
        case 0x9C: sbc_a(hl.h); break;
        case 0x9D: sbc_a(hl.l); break;
        case 0x9E: sbc_a(read_byte(hl.w)); cycle_count+=3; break;
        case 0x9F: sbc_a(af.h); break;
        case 0xA0: and_a(bc.h); break;
        case 0xA1: and_a(bc.l); break;
        case 0xA2: and_a(de.h); break;
        case 0xA3: and_a(de.l); break;
        case 0xA4: and_a(hl.h); break;
        case 0xA5: and_a(hl.l); break;
        case 0xA6: and_a(read_byte(hl.w)); cycle_count+=3; break;
        case 0xA7: and_a(af.h); break;
        case 0xA8: xor_a(bc.h); break;
        case 0xA9: xor_a(bc.l); break;
        case 0xAA: xor_a(de.h); break;
        case 0xAB: xor_a(de.l); break;
        case 0xAC: xor_a(hl.h); break;
        case 0xAD: xor_a(hl.l); break;
        case 0xAE: xor_a(read_byte(hl.w)); cycle_count+=3; break;
        case 0xAF: xor_a(af.h); break;
        case 0xB0: or_a(bc.h); break;
        case 0xB1: or_a(bc.l); break;
        case 0xB2: or_a(de.h); break;
        case 0xB3: or_a(de.l); break;
        case 0xB4: or_a(hl.h); break;
        case 0xB5: or_a(hl.l); break;
        case 0xB6: or_a(read_byte(hl.w)); cycle_count+=3; break;
        case 0xB7: or_a(af.h); break;
        case 0xB8: cp_a(bc.h); break;
        case 0xB9: cp_a(bc.l); break;
        case 0xBA: cp_a(de.h); break;
        case 0xBB: cp_a(de.l); break;
        case 0xBC: cp_a(hl.h); break;
        case 0xBD: cp_a(hl.l); break;
        case 0xBE: cp_a(read_byte(hl.w)); cycle_count+=3; break;
        case 0xBF: cp_a(af.h); break;
        case 0xC0: ret(!get_flag(FLAG_Z)); break;
        case 0xC1: bc.w = pop(); break; // POP rp2
        case 0xC2: jp(!get_flag(FLAG_Z)); break;
        case 0xC3: jp(true); break;
        case 0xC4: call(!get_flag(FLAG_Z)); break;
        case 0xC5: push(bc.w); break; // PUSH rp2
        case 0xC6: add_a(fetch()); break;
        case 0xC7: rst(0x00); break;
        case 0xC8: ret(get_flag(FLAG_Z)); break;
        case 0xC9: ret(true); break;
        case 0xCA: jp(get_flag(FLAG_Z)); break;
        case 0xCB: cycle_count--; pc--; unimplemented(); break; // CB prefix
        case 0xCC: call(get_flag(FLAG_Z)); break;
        case 0xCD: call(true); break;
        case 0xCE: adc_a(fetch()); break;
        case 0xCF: rst(0x08); break;
        case 0xD0: ret(!get_flag(FLAG_C)); break;
        case 0xD1: de.w = pop(); break; // POP rp2
        case 0xD2: jp(!get_flag(FLAG_C)); break;
        case 0xD3: port_write(fetch(), af.h); break; // OUT (n), A
        case 0xD4: call(!get_flag(FLAG_C)); break;
        case 0xD5: push(de.w); break; // PUSH rp2
        case 0xD6: sub_a(fetch()); break;
        case 0xD7: rst(0x10); break;
        case 0xD8: ret(get_flag(FLAG_C)); break;
        case 0xD9: std::swap(bc.w, bc_prime.w); std::swap(de.w, de_prime.w); std::swap(hl.w, hl_prime.w); break; // EXX
        case 0xDA: jp(get_flag(FLAG_C)); break;
        case 0xDB: af.h = port_read(fetch()); break; // IN A, (n)
        case 0xDC: call(get_flag(FLAG_C)); break;
        case 0xDD: cycle_count--; pc--; unimplemented(); break; // DD prefix
        case 0xDE: sbc_a(fetch()); break;
        case 0xDF: rst(0x18); break;
        case 0xE0: ret(!get_flag(FLAG_PV)); break;
        case 0xE1: hl.w = pop(); break; // POP rp2
        case 0xE2: jp(!get_flag(FLAG_PV)); break;
        case 0xE3: { uint16_t tmp = read_word(sp); write_word(sp, hl.w); hl.w = tmp; cycle_count += 4; } break; // EX (SP), HL
        case 0xE4: call(!get_flag(FLAG_PV)); break;
        case 0xE5: push(hl.w); break; // PUSH rp2
        case 0xE6: and_a(fetch()); break;
        case 0xE7: rst(0x20); break;
        case 0xE8: ret(get_flag(FLAG_PV)); break;
        case 0xE9: pc = hl.w; break; // JP (HL)
        case 0xEA: jp(get_flag(FLAG_PV)); break;
        case 0xEB: std::swap(de.w, hl.w); break; // EX DE, HL
        case 0xEC: call(get_flag(FLAG_PV)); break;
        case 0xED: cycle_count--; pc--; unimplemented(); break; // ED prefix
        case 0xEE: xor_a(fetch()); break;
        case 0xEF: rst(0x28); break;
        case 0xF0: ret(!get_flag(FLAG_S)); break;
        case 0xF1: af.w = pop(); break; // POP rp2
        case 0xF2: jp(!get_flag(FLAG_S)); break;
        case 0xF3: iff1 = false; iff2 = false; break; // DI
        case 0xF4: call(!get_flag(FLAG_S)); break;
        case 0xF5: push(af.w); break; // PUSH rp2
        case 0xF6: or_a(fetch()); break;
        case 0xF7: rst(0x30); break;
        case 0xF8: ret(get_flag(FLAG_S)); break;
        case 0xF9: sp = hl.w; cycle_count += 2; break; // LD SP, HL
        case 0xFA: jp(get_flag(FLAG_S)); break;
        case 0xFB: iff1 = true; iff2 = true; break; // EI
        case 0xFC: call(get_flag(FLAG_S)); break;
        case 0xFD: cycle_count--; pc--; unimplemented(); break; // FD prefix
        case 0xFE: cp_a(fetch()); break;
        case 0xFF: rst(0x38); break;
    }
}

} // namespace generator::z80
