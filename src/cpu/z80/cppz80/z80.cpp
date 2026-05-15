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
}

bool check_parity(uint8_t val) {
    val ^= val >> 4;
    val ^= val >> 2;
    val ^= val >> 1;
    return (~val) & 1;
}

void Z80::add_a(uint8_t val) {
    uint16_t res = af.h() + val;
    uint8_t res8 = static_cast<uint8_t>(res);
    bool half_carry = ((af.h() & 0x0F) + (val & 0x0F)) > 0x0F;
    bool overflow = ((af.h() ^ val ^ 0x80) & (val ^ res) & 0x80) != 0;
    
    af.set_l(0); 
    if (res & 0x100) af.set_l(af.l() | FLAG_C);
    if (res8 == 0) af.set_l(af.l() | FLAG_Z);
    if (res8 & 0x80) af.set_l(af.l() | FLAG_S);
    if (half_carry) af.set_l(af.l() | FLAG_H);
    if (overflow) af.set_l(af.l() | FLAG_PV);
    update_xy_flags(res8);
    af.set_h(res8);
}

void Z80::adc_a(uint8_t val) {
    uint8_t c = get_flag(FLAG_C) ? 1 : 0;
    uint16_t res = af.h() + val + c;
    uint8_t res8 = static_cast<uint8_t>(res);
    bool half_carry = ((af.h() & 0x0F) + (val & 0x0F) + c) > 0x0F;
    bool overflow = ((af.h() ^ val ^ 0x80) & (val ^ res) & 0x80) != 0;
    
    af.set_l(0);
    if (res & 0x100) af.set_l(af.l() | FLAG_C);
    if (res8 == 0) af.set_l(af.l() | FLAG_Z);
    if (res8 & 0x80) af.set_l(af.l() | FLAG_S);
    if (half_carry) af.set_l(af.l() | FLAG_H);
    if (overflow) af.set_l(af.l() | FLAG_PV);
    update_xy_flags(res8);
    af.set_h(res8);
}

void Z80::sub_a(uint8_t val) {
    uint16_t res = af.h() - val;
    uint8_t res8 = static_cast<uint8_t>(res);
    bool half_carry = (af.h() & 0x0F) < (val & 0x0F);
    bool overflow = ((af.h() ^ val) & (af.h() ^ res) & 0x80) != 0;
    
    af.set_l(FLAG_N);
    if (res & 0x100) af.set_l(af.l() | FLAG_C);
    if (res8 == 0) af.set_l(af.l() | FLAG_Z);
    if (res8 & 0x80) af.set_l(af.l() | FLAG_S);
    if (half_carry) af.set_l(af.l() | FLAG_H);
    if (overflow) af.set_l(af.l() | FLAG_PV);
    update_xy_flags(res8);
    af.set_h(res8);
}

void Z80::sbc_a(uint8_t val) {
    uint8_t c = get_flag(FLAG_C) ? 1 : 0;
    uint16_t res = af.h() - val - c;
    uint8_t res8 = static_cast<uint8_t>(res);
    bool half_carry = (af.h() & 0x0F) < ((val & 0x0F) + c);
    bool overflow = ((af.h() ^ val) & (af.h() ^ res) & 0x80) != 0;
    
    af.set_l(FLAG_N);
    if (res & 0x100) af.set_l(af.l() | FLAG_C);
    if (res8 == 0) af.set_l(af.l() | FLAG_Z);
    if (res8 & 0x80) af.set_l(af.l() | FLAG_S);
    if (half_carry) af.set_l(af.l() | FLAG_H);
    if (overflow) af.set_l(af.l() | FLAG_PV);
    update_xy_flags(res8);
    af.set_h(res8);
}

void Z80::and_a(uint8_t val) {
    af.set_h(af.h() & val);
    af.set_l(FLAG_H);
    if (af.h() == 0) af.set_l(af.l() | FLAG_Z);
    if (af.h() & 0x80) af.set_l(af.l() | FLAG_S);
    if (check_parity(af.h())) af.set_l(af.l() | FLAG_PV);
    update_xy_flags(af.h());
}

void Z80::xor_a(uint8_t val) {
    af.set_h(af.h() ^ val);
    af.set_l(0);
    if (af.h() == 0) af.set_l(af.l() | FLAG_Z);
    if (af.h() & 0x80) af.set_l(af.l() | FLAG_S);
    if (check_parity(af.h())) af.set_l(af.l() | FLAG_PV);
    update_xy_flags(af.h());
}

void Z80::or_a(uint8_t val) {
    af.set_h(af.h() | val);
    af.set_l(0);
    if (af.h() == 0) af.set_l(af.l() | FLAG_Z);
    if (af.h() & 0x80) af.set_l(af.l() | FLAG_S);
    if (check_parity(af.h())) af.set_l(af.l() | FLAG_PV);
    update_xy_flags(af.h());
}

void Z80::cp_a(uint8_t val) {
    uint16_t res = af.h() - val;
    uint8_t res8 = static_cast<uint8_t>(res);
    bool half_carry = (af.h() & 0x0F) < (val & 0x0F);
    bool overflow = ((af.h() ^ val) & (af.h() ^ res) & 0x80) != 0;
    
    af.set_l(FLAG_N);
    if (res & 0x100) af.set_l(af.l() | FLAG_C);
    if (res8 == 0) af.set_l(af.l() | FLAG_Z);
    if (res8 & 0x80) af.set_l(af.l() | FLAG_S);
    if (half_carry) af.set_l(af.l() | FLAG_H);
    if (overflow) af.set_l(af.l() | FLAG_PV);
    update_xy_flags(val);
}

uint8_t Z80::inc8(uint8_t val) {
    uint8_t res = val + 1;
    bool half_carry = (val & 0x0F) == 0x00;
    half_carry = (val & 0x0F) == 0x0F;
    bool overflow = val == 0x7F;
    
    af.set_l(af.l() & FLAG_C); // Preserve C
    if (res == 0) af.set_l(af.l() | FLAG_Z);
    if (res & 0x80) af.set_l(af.l() | FLAG_S);
    if (half_carry) af.set_l(af.l() | FLAG_H);
    if (overflow) af.set_l(af.l() | FLAG_PV);
    update_xy_flags(res);
    return res;
}

uint8_t Z80::dec8(uint8_t val) {
    uint8_t res = val - 1;
    bool half_carry = (val & 0x0F) == 0x00;
    bool overflow = val == 0x80;
    
    af.set_l(af.l() & FLAG_C); // Preserve C
    af.set_l(af.l() | FLAG_N);
    if (res == 0) af.set_l(af.l() | FLAG_Z);
    if (res & 0x80) af.set_l(af.l() | FLAG_S);
    if (half_carry) af.set_l(af.l() | FLAG_H);
    if (overflow) af.set_l(af.l() | FLAG_PV);
    update_xy_flags(res);
    return res;
}

void Z80::add_hl(uint16_t val) {
    uint32_t res = hl.w + val;
    bool half_carry = ((hl.w & 0x0FFF) + (val & 0x0FFF)) > 0x0FFF;
    
    af.set_l(af.l() & (FLAG_S | FLAG_Z | FLAG_PV));
    if (res & 0x10000) af.set_l(af.l() | FLAG_C);
    if (half_carry) af.set_l(af.l() | FLAG_H);
    update_xy_flags(res >> 8);
    hl.w = res;
    cycle_count += 7; // rough
}

void Z80::rlca() {
    uint8_t c = af.h() >> 7;
    af.set_h((af.h() << 1) | c);
    af.set_l((af.l() & (FLAG_S | FLAG_Z | FLAG_PV)) | (c ? FLAG_C : 0));
    update_xy_flags(af.h());
}
void Z80::rrca() {
    uint8_t c = af.h() & 1;
    af.set_h((af.h() >> 1) | (c << 7));
    af.set_l((af.l() & (FLAG_S | FLAG_Z | FLAG_PV)) | (c ? FLAG_C : 0));
    update_xy_flags(af.h());
}
void Z80::rla() {
    uint8_t c = af.h() >> 7;
    uint8_t old_c = get_flag(FLAG_C) ? 1 : 0;
    af.set_h((af.h() << 1) | old_c);
    af.set_l((af.l() & (FLAG_S | FLAG_Z | FLAG_PV)) | (c ? FLAG_C : 0));
    update_xy_flags(af.h());
}
void Z80::rra() {
    uint8_t c = af.h() & 1;
    uint8_t old_c = get_flag(FLAG_C) ? 1 : 0;
    af.set_h((af.h() >> 1) | (old_c << 7));
    af.set_l((af.l() & (FLAG_S | FLAG_Z | FLAG_PV)) | (c ? FLAG_C : 0));
    update_xy_flags(af.h());
}
void Z80::daa() {
    uint8_t a = af.h();
    uint8_t corr = 0;
    uint8_t c = get_flag(FLAG_C);
    uint8_t h = get_flag(FLAG_H);
    uint8_t n = get_flag(FLAG_N);
    if (h || ((a & 0x0F) > 9)) corr |= 0x06;
    if (c || (a > 0x99)) { corr |= 0x60; c = 1; }
    if (n) a -= corr; else a += corr;
    
    af.set_l(n ? FLAG_N : 0);
    if (c) af.set_l(af.l() | FLAG_C);
    if (a == 0) af.set_l(af.l() | FLAG_Z);
    if (a & 0x80) af.set_l(af.l() | FLAG_S);
    if (check_parity(a)) af.set_l(af.l() | FLAG_PV);
    update_xy_flags(a);
    af.set_h(a);
}
void Z80::cpl() {
    af.set_h(~af.h());
    af.set_l(af.l() | FLAG_H | FLAG_N);
    update_xy_flags(af.h());
}
void Z80::scf() {
    af.set_l((af.l() & ~(FLAG_H | FLAG_N)) | FLAG_C);
    update_xy_flags(af.h());
}
void Z80::ccf() {
    af.set_l((af.l() & ~FLAG_N) ^ FLAG_C);
    if (get_flag(FLAG_C)) af.set_l(af.l() | FLAG_H); else af.set_l(af.l() & ~FLAG_H);
    update_xy_flags(af.h());
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
    bc.set_h(bc.h() - 1);
    if (bc.h() != 0) {
        pc += d;
        cycle_count += 5;
    }
}

} // namespace generator::z80
