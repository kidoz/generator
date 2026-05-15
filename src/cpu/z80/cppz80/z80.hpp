#pragma once

#include <cstdint>

namespace generator::z80 {

struct alignas(2) RegisterPair {
    union {
        struct {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
            uint8_t l;
            uint8_t h;
#else
            uint8_t h;
            uint8_t l;
#endif
        };
        uint16_t w;
    };
};

class Z80 {
public:
    // Callbacks
    using ReadByteFn = uint8_t (*)(uint16_t addr);
    using WriteByteFn = void (*)(uint16_t addr, uint8_t data);
    using PortReadFn = uint8_t (*)(uint16_t port);
    using PortWriteFn = void (*)(uint16_t port, uint8_t data);

    Z80() = default;

    void set_callbacks(ReadByteFn rb, WriteByteFn wb, PortReadFn pr, PortWriteFn pw) {
        read_byte = rb;
        write_byte = wb;
        port_read = pr;
        port_write = pw;
    }

    void reset() {
        pc = 0;
        af.w = 0xFFFF;
        sp = 0xFFFF;
        iff1 = false;
        iff2 = false;
        im = 0;
        halted = false;
        cycle_count = 0;
    }

    void execute(int cycles) {
        int target = cycle_count + cycles;
        while (cycle_count < target) {
            if (halted) {
                cycle_count += 4;
                continue;
            }
            step();
        }
    }

    void interrupt() {
        if (iff1) {
            iff1 = false;
            iff2 = false;
            halted = false;
            cycle_count += 13; // roughly
            
            if (im == 1 || im == 0) {
                // RST 38h
                sp -= 2;
                write_byte(sp, pc >> 8);
                write_byte(sp + 1, pc & 0xFF);
                pc = 0x0038;
            } else if (im == 2) {
                // Not fully implemented for Genesis since Genesis doesn't use IM 2 much,
                // but proper implementation would read from vector bus.
                sp -= 2;
                write_byte(sp, pc >> 8);
                write_byte(sp + 1, pc & 0xFF);
                pc = 0x0038; // Default fallback for now
            }
        }
    }

    // Context syncing methods for `state.c` compatibility
    void sync_out(void* context) const;
    void sync_in(const void* context);

private:
    void step();
    uint8_t fetch() {
        uint8_t val = read_byte(pc++);
        cycle_count += 4; // basic M1
        return val;
    }

    ReadByteFn read_byte = nullptr;
    WriteByteFn write_byte = nullptr;
    PortReadFn port_read = nullptr;
    PortWriteFn port_write = nullptr;

    RegisterPair af{}, bc{}, de{}, hl{};
    RegisterPair af_prime{}, bc_prime{}, de_prime{}, hl_prime{};
    RegisterPair ix{}, iy{};
    uint16_t pc{0};
    uint16_t sp{0};
    uint8_t i{0}, r{0};

    bool iff1{false};
    bool iff2{false};
    uint8_t im{0};
    bool halted{false};

    int cycle_count{0};

public:
    enum Flags : uint8_t {
        FLAG_C  = 1 << 0,
        FLAG_N  = 1 << 1,
        FLAG_PV = 1 << 2,
        FLAG_X  = 1 << 3,
        FLAG_H  = 1 << 4,
        FLAG_Y  = 1 << 5,
        FLAG_Z  = 1 << 6,
        FLAG_S  = 1 << 7
    };

    void set_flag(Flags flag, bool value) {
        if (value) af.l |= flag;
        else af.l &= ~flag;
    }

    bool get_flag(Flags flag) const {
        return (af.l & flag) != 0;
    }

    void update_xy_flags(uint8_t result) {
        af.l = (af.l & ~(FLAG_X | FLAG_Y)) | (result & (FLAG_X | FLAG_Y));
    }

    // Accessors for testing and debugging
    uint16_t get_af() const { return af.w; }
    uint16_t get_bc() const { return bc.w; }
    uint16_t get_de() const { return de.w; }
    uint16_t get_hl() const { return hl.w; }
    uint16_t get_pc() const { return pc; }
    void set_af(uint16_t val) { af.w = val; }
    void set_bc(uint16_t val) { bc.w = val; }
    void set_de(uint16_t val) { de.w = val; }
    void set_hl(uint16_t val) { hl.w = val; }
    void set_pc(uint16_t val) { pc = val; }

    // Accessors for cycle counts (used by the bridge)
    int get_cycles() const { return cycle_count; }
    void add_cycles(int cycles) { cycle_count += cycles; }
    void reset_cycles() { cycle_count = 0; }
};

} // namespace generator::z80