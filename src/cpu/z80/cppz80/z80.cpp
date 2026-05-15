#include "z80.hpp"

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
    // State of IFF1/IFF2 is encoded in z80interruptState. 
    // Normally iff1 is bit 0, iff2 is bit 1. 
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

void Z80::step() {
    uint8_t opcode = fetch();

    auto add_a = [this](uint8_t val) {
        uint16_t res = af.h + val;
        uint8_t res8 = static_cast<uint8_t>(res);
        bool half_carry = ((af.h & 0x0F) + (val & 0x0F)) > 0x0F;
        bool overflow = ((af.h ^ val ^ 0x80) & (val ^ res) & 0x80) != 0;
        
        af.l = 0; // Clears N flag as well
        if (res & 0x100) af.l |= FLAG_C;
        if (res8 == 0) af.l |= FLAG_Z;
        if (res8 & 0x80) af.l |= FLAG_S;
        if (half_carry) af.l |= FLAG_H;
        if (overflow) af.l |= FLAG_PV;
        update_xy_flags(res8);
        
        af.h = res8;
    };

    switch (opcode) {
        case 0x00: // NOP
            break;
        case 0x40: bc.h = bc.h; break; // LD B, B
        case 0x41: bc.h = bc.l; break; // LD B, C
        case 0x42: bc.h = de.h; break; // LD B, D
        case 0x43: bc.h = de.l; break; // LD B, E
        case 0x44: bc.h = hl.h; break; // LD B, H
        case 0x45: bc.h = hl.l; break; // LD B, L
        case 0x47: bc.h = af.h; break; // LD B, A

        case 0x80: add_a(bc.h); break; // ADD A, B
        case 0x81: add_a(bc.l); break; // ADD A, C
        case 0x82: add_a(de.h); break; // ADD A, D
        case 0x83: add_a(de.l); break; // ADD A, E
        case 0x84: add_a(hl.h); break; // ADD A, H
        case 0x85: add_a(hl.l); break; // ADD A, L
        case 0x87: add_a(af.h); break; // ADD A, A

        case 0xC3: { // JP nn
            uint8_t lo = fetch();
            uint8_t hi = fetch();
            pc = (hi << 8) | lo;
            break;
        }
        case 0x76: // HALT
            halted = true;
            pc--; // Halt actually spins on itself in real z80, but this is an abstraction
            break;
        case 0xED: {
            uint8_t ext = fetch();
            switch (ext) {
                case 0x56: // IM 1
                    im = 1;
                    break;
                // Add more ED opcodes as needed
                default:
                    // Unimplemented
                    break;
            }
            break;
        }
        case 0xF3: // DI
            iff1 = false;
            iff2 = false;
            break;
        case 0xFB: // EI
            iff1 = true;
            iff2 = true;
            break;
        default:
            // Minimal skeleton for now. Unknown opcodes will just consume 4 cycles via fetch().
            break;
    }
}

} // namespace generator::z80
