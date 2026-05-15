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

    switch (opcode) {
        case 0x00: // NOP
            break;
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
