#pragma once

#include <cstdint>
#include <array>
#include <utility>

namespace generator::z80 {

bool check_parity(uint8_t val);

struct alignas(2) RegisterPair {
    uint16_t w{0};
    constexpr uint8_t h() const { return static_cast<uint8_t>(w >> 8); }
    constexpr void set_h(uint8_t val) { w = (w & 0x00FF) | (static_cast<uint16_t>(val) << 8); }
    constexpr uint8_t l() const { return static_cast<uint8_t>(w & 0xFF); }
    constexpr void set_l(uint8_t val) { w = (w & 0xFF00) | val; }
};

enum class Reg8 { B, C, D, E, H, L, HL_ind, A };
enum class Reg16 { BC, DE, HL, SP };
enum class Reg16PushPop { BC, DE, HL, AF };
enum class Cond { NZ, Z, NC, C, PO, PE, P, M };

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
                sp -= 2;
                write_byte(sp, pc >> 8);
                write_byte(sp + 1, pc & 0xFF);
                pc = 0x0038; // Default fallback for now
            }
        }
    }

    void sync_out(void* context) const;
    void sync_in(const void* context);

private:
    void step();

    uint16_t fetch_word() {
        uint8_t lo = fetch();
        uint8_t hi = fetch();
        return (hi << 8) | lo;
    }

    uint16_t read_word(uint16_t addr) {
        uint8_t lo = read_byte(addr);
        cycle_count += 3;
        uint8_t hi = read_byte(addr + 1);
        cycle_count += 3;
        return (hi << 8) | lo;
    }

    void write_word(uint16_t addr, uint16_t val) {
        write_byte(addr, val & 0xFF);
        cycle_count += 3;
        write_byte(addr + 1, val >> 8);
        cycle_count += 3;
    }

    void push(uint16_t val) {
        sp--;
        write_byte(sp, val >> 8);
        cycle_count += 3;
        sp--;
        write_byte(sp, val & 0xFF);
        cycle_count += 3;
    }

    uint16_t pop() {
        uint8_t lo = read_byte(sp);
        sp++;
        cycle_count += 3;
        uint8_t hi = read_byte(sp);
        sp++;
        cycle_count += 3;
        return (hi << 8) | lo;
    }

    void add_a(uint8_t val);
    void adc_a(uint8_t val);
    void sub_a(uint8_t val);
    void sbc_a(uint8_t val);
    void and_a(uint8_t val);
    void xor_a(uint8_t val);
    void or_a(uint8_t val);
    void cp_a(uint8_t val);

    uint8_t inc8(uint8_t val);
    uint8_t dec8(uint8_t val);
    
    void add_hl(uint16_t val);
    void adc_hl(uint16_t val);
    void sbc_hl(uint16_t val);

    void rlca();
    void rrca();
    void rla();
    void rra();
    void daa();
    void cpl();
    void scf();
    void ccf();

    void rlc(uint8_t& val);
    void rrc(uint8_t& val);
    void rl(uint8_t& val);
    void rr(uint8_t& val);
    void sla(uint8_t& val);
    void sra(uint8_t& val);
    void srl(uint8_t& val);

    void bit(uint8_t b, uint8_t val);
    void res(uint8_t b, uint8_t& val);
    void set(uint8_t b, uint8_t& val);

    void jr(bool cond);
    void jp(bool cond);
    void call(bool cond);
    void ret(bool cond);
    void rst(uint16_t addr);
    void djnz();
    void unimplemented();

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
        if (value) af.set_l(af.l() | flag);
        else af.set_l(af.l() & ~flag);
    }

    bool get_flag(Flags flag) const {
        return (af.l() & flag) != 0;
    }

    void update_xy_flags(uint8_t result) {
        af.set_l((af.l() & ~(FLAG_X | FLAG_Y)) | (result & (FLAG_X | FLAG_Y)));
    }

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

    int get_cycles() const { return cycle_count; }
    void add_cycles(int cycles) { cycle_count += cycles; }
    void reset_cycles() { cycle_count = 0; }

private:
    template<Reg8 R> uint8_t read_reg8() {
        if constexpr (R == Reg8::B) return bc.h();
        else if constexpr (R == Reg8::C) return bc.l();
        else if constexpr (R == Reg8::D) return de.h();
        else if constexpr (R == Reg8::E) return de.l();
        else if constexpr (R == Reg8::H) return hl.h();
        else if constexpr (R == Reg8::L) return hl.l();
        else if constexpr (R == Reg8::HL_ind) { cycle_count += 3; return read_byte(hl.w); }
        else if constexpr (R == Reg8::A) return af.h();
    }

    template<Reg8 R> void write_reg8(uint8_t val) {
        if constexpr (R == Reg8::B) bc.set_h(val);
        else if constexpr (R == Reg8::C) bc.set_l(val);
        else if constexpr (R == Reg8::D) de.set_h(val);
        else if constexpr (R == Reg8::E) de.set_l(val);
        else if constexpr (R == Reg8::H) hl.set_h(val);
        else if constexpr (R == Reg8::L) hl.set_l(val);
        else if constexpr (R == Reg8::HL_ind) { write_byte(hl.w, val); cycle_count += 3; }
        else if constexpr (R == Reg8::A) af.set_h(val);
    }

    template<Reg16 R> uint16_t read_reg16() {
        if constexpr (R == Reg16::BC) return bc.w;
        else if constexpr (R == Reg16::DE) return de.w;
        else if constexpr (R == Reg16::HL) return hl.w;
        else if constexpr (R == Reg16::SP) return sp;
    }

    template<Reg16 R> void write_reg16(uint16_t val) {
        if constexpr (R == Reg16::BC) bc.w = val;
        else if constexpr (R == Reg16::DE) de.w = val;
        else if constexpr (R == Reg16::HL) hl.w = val;
        else if constexpr (R == Reg16::SP) sp = val;
    }

    template<Reg16PushPop R> uint16_t read_reg16_pushpop() {
        if constexpr (R == Reg16PushPop::BC) return bc.w;
        else if constexpr (R == Reg16PushPop::DE) return de.w;
        else if constexpr (R == Reg16PushPop::HL) return hl.w;
        else if constexpr (R == Reg16PushPop::AF) return af.w;
    }

    template<Reg16PushPop R> void write_reg16_pushpop(uint16_t val) {
        if constexpr (R == Reg16PushPop::BC) bc.w = val;
        else if constexpr (R == Reg16PushPop::DE) de.w = val;
        else if constexpr (R == Reg16PushPop::HL) hl.w = val;
        else if constexpr (R == Reg16PushPop::AF) af.w = val;
    }

    bool check_cond(Cond c) {
        switch(c) {
            case Cond::NZ: return !get_flag(FLAG_Z);
            case Cond::Z:  return get_flag(FLAG_Z);
            case Cond::NC: return !get_flag(FLAG_C);
            case Cond::C:  return get_flag(FLAG_C);
            case Cond::PO: return !get_flag(FLAG_PV);
            case Cond::PE: return get_flag(FLAG_PV);
            case Cond::P:  return !get_flag(FLAG_S);
            case Cond::M:  return get_flag(FLAG_S);
        }
        std::unreachable();
    }

    void op_nop() {}
    template<Reg8 Dst, Reg8 Src> void op_ld_r_r() { write_reg8<Dst>(read_reg8<Src>()); }
    template<Reg8 Dst> void op_ld_r_n() { write_reg8<Dst>(fetch()); }
    template<Reg16 Dst> void op_ld_rp_nn() { write_reg16<Dst>(fetch_word()); }
    template<Reg16 Dst> void op_inc_rp() { write_reg16<Dst>(read_reg16<Dst>() + 1); cycle_count += 2; }
    template<Reg16 Dst> void op_dec_rp() { write_reg16<Dst>(read_reg16<Dst>() - 1); cycle_count += 2; }
    template<Reg8 Dst> void op_inc_r() { write_reg8<Dst>(inc8(read_reg8<Dst>())); }
    template<Reg8 Dst> void op_dec_r() { write_reg8<Dst>(dec8(read_reg8<Dst>())); }
    template<Reg16 Src> void op_add_hl_rp() { add_hl(read_reg16<Src>()); }

    template<Reg8 Src> void op_add_a_r() { add_a(read_reg8<Src>()); }
    template<Reg8 Src> void op_adc_a_r() { adc_a(read_reg8<Src>()); }
    template<Reg8 Src> void op_sub_a_r() { sub_a(read_reg8<Src>()); }
    template<Reg8 Src> void op_sbc_a_r() { sbc_a(read_reg8<Src>()); }
    template<Reg8 Src> void op_and_a_r() { and_a(read_reg8<Src>()); }
    template<Reg8 Src> void op_xor_a_r() { xor_a(read_reg8<Src>()); }
    template<Reg8 Src> void op_or_a_r() { or_a(read_reg8<Src>()); }
    template<Reg8 Src> void op_cp_a_r() { cp_a(read_reg8<Src>()); }

    template<Reg16PushPop Src> void op_push_rp2() { push(read_reg16_pushpop<Src>()); }
    template<Reg16PushPop Dst> void op_pop_rp2() { write_reg16_pushpop<Dst>(pop()); }

    template<Cond C> void op_ret_c() { ret(check_cond(C)); }
    template<Cond C> void op_jp_c() { jp(check_cond(C)); }
    template<Cond C> void op_call_c() { call(check_cond(C)); }
    template<Cond C> void op_jr_c() { jr(check_cond(C)); }
    template<uint16_t Addr> void op_rst() { rst(Addr); }

    void op_rlca() { rlca(); }
    void op_rrca() { rrca(); }
    void op_rla() { rla(); }
    void op_rra() { rra(); }
    void op_daa() { daa(); }
    void op_cpl() { cpl(); }
    void op_scf() { scf(); }
    void op_ccf() { ccf(); }
    void op_jr() { jr(true); }
    void op_jp() { jp(true); }
    void op_call() { call(true); }
    void op_ret() { ret(true); }
    void op_djnz() { djnz(); }
    void op_ex_af_af_prime() { std::swap(af.w, af_prime.w); }
    void op_halt() { halted = true; pc--; }

    void op_ld_bc_a() { write_byte(bc.w, af.h()); cycle_count += 3; }
    void op_ld_de_a() { write_byte(de.w, af.h()); cycle_count += 3; }
    void op_ld_a_bc() { af.set_h(read_byte(bc.w)); cycle_count += 3; }
    void op_ld_a_de() { af.set_h(read_byte(de.w)); cycle_count += 3; }
    void op_ld_nn_hl() { write_word(fetch_word(), hl.w); }
    void op_ld_hl_nn() { hl.w = read_word(fetch_word()); }
    void op_ld_nn_a() { write_byte(fetch_word(), af.h()); cycle_count += 3; }
    void op_ld_a_nn() { af.set_h(read_byte(fetch_word())); cycle_count += 3; }

    void op_add_a_n() { add_a(fetch()); }
    void op_adc_a_n() { adc_a(fetch()); }
    void op_sub_a_n() { sub_a(fetch()); }
    void op_sbc_a_n() { sbc_a(fetch()); }
    void op_and_a_n() { and_a(fetch()); }
    void op_xor_a_n() { xor_a(fetch()); }
    void op_or_a_n() { or_a(fetch()); }
    void op_cp_a_n() { cp_a(fetch()); }

    void op_ex_de_hl() { std::swap(de.w, hl.w); }
    void op_exx() { std::swap(bc.w, bc_prime.w); std::swap(de.w, de_prime.w); std::swap(hl.w, hl_prime.w); }
    void op_ex_sp_hl() { uint16_t tmp = read_word(sp); write_word(sp, hl.w); hl.w = tmp; cycle_count += 4; }
    void op_jp_hl() { pc = hl.w; }
    void op_ld_sp_hl() { sp = hl.w; cycle_count += 2; }
    void op_di() { iff1 = false; iff2 = false; }
    void op_ei() { iff1 = true; iff2 = true; }
    void op_out_n_a() { port_write(fetch(), af.h()); }
    void op_in_a_n() { af.set_h(port_read(fetch())); }

    void op_unimplemented() { unimplemented(); }
    void op_cb();
    void op_dd();
    void op_ed();
    void op_fd();

    using OpcodeHandler = void (Z80::*)();

    template<Reg8 R> void op_rlc_r() { uint8_t v = read_reg8<R>(); rlc(v); write_reg8<R>(v); }
    template<Reg8 R> void op_rrc_r() { uint8_t v = read_reg8<R>(); rrc(v); write_reg8<R>(v); }
    template<Reg8 R> void op_rl_r() { uint8_t v = read_reg8<R>(); rl(v); write_reg8<R>(v); }
    template<Reg8 R> void op_rr_r() { uint8_t v = read_reg8<R>(); rr(v); write_reg8<R>(v); }
    template<Reg8 R> void op_sla_r() { uint8_t v = read_reg8<R>(); sla(v); write_reg8<R>(v); }
    template<Reg8 R> void op_sra_r() { uint8_t v = read_reg8<R>(); sra(v); write_reg8<R>(v); }
    template<Reg8 R> void op_sll_r() { uint8_t v = read_reg8<R>(); uint8_t c = v >> 7; v = (v << 1) | 1; af.set_l((c ? FLAG_C : 0) | (v == 0 ? FLAG_Z : 0) | (v & FLAG_S) | (check_parity(v) ? FLAG_PV : 0)); update_xy_flags(v); write_reg8<R>(v); }
    template<Reg8 R> void op_srl_r() { uint8_t v = read_reg8<R>(); srl(v); write_reg8<R>(v); }

    template<uint8_t B, Reg8 R> void op_bit_b_r() { bit(B, read_reg8<R>()); }
    template<uint8_t B, Reg8 R> void op_res_b_r() { uint8_t v = read_reg8<R>(); res(B, v); write_reg8<R>(v); }
    template<uint8_t B, Reg8 R> void op_set_b_r() { uint8_t v = read_reg8<R>(); set(B, v); write_reg8<R>(v); }

    template <std::size_t... Is>
    static constexpr void init_cb_rot(std::array<OpcodeHandler, 256>& table, std::index_sequence<Is...>) {
        ( (table[0x00 + Is] = &Z80::op_rlc_r<static_cast<Reg8>(Is % 8)>), ... );
        ( (table[0x08 + Is] = &Z80::op_rrc_r<static_cast<Reg8>(Is % 8)>), ... );
        ( (table[0x10 + Is] = &Z80::op_rl_r<static_cast<Reg8>(Is % 8)>), ... );
        ( (table[0x18 + Is] = &Z80::op_rr_r<static_cast<Reg8>(Is % 8)>), ... );
        ( (table[0x20 + Is] = &Z80::op_sla_r<static_cast<Reg8>(Is % 8)>), ... );
        ( (table[0x28 + Is] = &Z80::op_sra_r<static_cast<Reg8>(Is % 8)>), ... );
        ( (table[0x30 + Is] = &Z80::op_sll_r<static_cast<Reg8>(Is % 8)>), ... );
        ( (table[0x38 + Is] = &Z80::op_srl_r<static_cast<Reg8>(Is % 8)>), ... );
    }

    template <std::size_t... Is>
    static constexpr void init_cb_bit(std::array<OpcodeHandler, 256>& table, std::index_sequence<Is...>) {
        ( (table[0x40 + Is] = &Z80::op_bit_b_r<static_cast<uint8_t>(Is / 8), static_cast<Reg8>(Is % 8)>), ... );
        ( (table[0x80 + Is] = &Z80::op_res_b_r<static_cast<uint8_t>(Is / 8), static_cast<Reg8>(Is % 8)>), ... );
        ( (table[0xC0 + Is] = &Z80::op_set_b_r<static_cast<uint8_t>(Is / 8), static_cast<Reg8>(Is % 8)>), ... );
    }

    static consteval std::array<OpcodeHandler, 256> build_cb_dispatch_table() {
        std::array<OpcodeHandler, 256> table{};
        for (auto& entry : table) entry = &Z80::op_unimplemented;
        init_cb_rot(table, std::make_index_sequence<8>{});
        init_cb_bit(table, std::make_index_sequence<64>{});
        return table;
    }

    static consteval std::array<OpcodeHandler, 256> build_ed_dispatch_table() {
        std::array<OpcodeHandler, 256> table{};
        for (auto& entry : table) entry = &Z80::op_unimplemented;
        // TO DO: Implement ED instructions
        return table;
    }

    static consteval std::array<OpcodeHandler, 256> build_dd_dispatch_table() {
        std::array<OpcodeHandler, 256> table{};
        for (auto& entry : table) entry = &Z80::op_unimplemented;
        // TO DO: Implement DD instructions
        return table;
    }

    static consteval std::array<OpcodeHandler, 256> build_fd_dispatch_table() {
        std::array<OpcodeHandler, 256> table{};
        for (auto& entry : table) entry = &Z80::op_unimplemented;
        // TO DO: Implement FD instructions
        return table;
    }

    template <std::size_t... Is>
    static constexpr void init_ld_r_r(std::array<OpcodeHandler, 256>& table, std::index_sequence<Is...>) {
        ( (table[0x40 + Is] = (Is == 0x36 ? &Z80::op_halt : &Z80::op_ld_r_r<static_cast<Reg8>(Is / 8), static_cast<Reg8>(Is % 8)>)), ... );
    }

    template <std::size_t... Is>
    static constexpr void init_alu(std::array<OpcodeHandler, 256>& table, std::index_sequence<Is...>) {
        ( (table[0x80 + Is] = &Z80::op_add_a_r<static_cast<Reg8>(Is)>), ... );
        ( (table[0x88 + Is] = &Z80::op_adc_a_r<static_cast<Reg8>(Is)>), ... );
        ( (table[0x90 + Is] = &Z80::op_sub_a_r<static_cast<Reg8>(Is)>), ... );
        ( (table[0x98 + Is] = &Z80::op_sbc_a_r<static_cast<Reg8>(Is)>), ... );
        ( (table[0xA0 + Is] = &Z80::op_and_a_r<static_cast<Reg8>(Is)>), ... );
        ( (table[0xA8 + Is] = &Z80::op_xor_a_r<static_cast<Reg8>(Is)>), ... );
        ( (table[0xB0 + Is] = &Z80::op_or_a_r<static_cast<Reg8>(Is)>), ... );
        ( (table[0xB8 + Is] = &Z80::op_cp_a_r<static_cast<Reg8>(Is)>), ... );
    }

    template <std::size_t... Is>
    static constexpr void init_ld_r_n(std::array<OpcodeHandler, 256>& table, std::index_sequence<Is...>) {
        ( (table[0x06 + Is * 8] = &Z80::op_ld_r_n<static_cast<Reg8>(Is)>), ... );
    }

    template <std::size_t... Is>
    static constexpr void init_inc_dec_r(std::array<OpcodeHandler, 256>& table, std::index_sequence<Is...>) {
        ( (table[0x04 + Is * 8] = &Z80::op_inc_r<static_cast<Reg8>(Is)>), ... );
        ( (table[0x05 + Is * 8] = &Z80::op_dec_r<static_cast<Reg8>(Is)>), ... );
    }

    template <std::size_t... Is>
    static constexpr void init_rp(std::array<OpcodeHandler, 256>& table, std::index_sequence<Is...>) {
        ( (table[0x01 + Is * 16] = &Z80::op_ld_rp_nn<static_cast<Reg16>(Is)>), ... );
        ( (table[0x03 + Is * 16] = &Z80::op_inc_rp<static_cast<Reg16>(Is)>), ... );
        ( (table[0x09 + Is * 16] = &Z80::op_add_hl_rp<static_cast<Reg16>(Is)>), ... );
        ( (table[0x0B + Is * 16] = &Z80::op_dec_rp<static_cast<Reg16>(Is)>), ... );
    }

    template <std::size_t... Is>
    static constexpr void init_push_pop(std::array<OpcodeHandler, 256>& table, std::index_sequence<Is...>) {
        ( (table[0xC1 + Is * 16] = &Z80::op_pop_rp2<static_cast<Reg16PushPop>(Is)>), ... );
        ( (table[0xC5 + Is * 16] = &Z80::op_push_rp2<static_cast<Reg16PushPop>(Is)>), ... );
    }

    template <std::size_t... Is>
    static constexpr void init_cond_jp(std::array<OpcodeHandler, 256>& table, std::index_sequence<Is...>) {
        ( (table[0xC0 + Is * 8] = &Z80::op_ret_c<static_cast<Cond>(Is)>), ... );
        ( (table[0xC2 + Is * 8] = &Z80::op_jp_c<static_cast<Cond>(Is)>), ... );
        ( (table[0xC4 + Is * 8] = &Z80::op_call_c<static_cast<Cond>(Is)>), ... );
    }

    template <std::size_t... Is>
    static constexpr void init_jr_c(std::array<OpcodeHandler, 256>& table, std::index_sequence<Is...>) {
        ( (table[0x20 + Is * 8] = &Z80::op_jr_c<static_cast<Cond>(Is)>), ... );
    }

    template <std::size_t... Is>
    static constexpr void init_rst(std::array<OpcodeHandler, 256>& table, std::index_sequence<Is...>) {
        ( (table[0xC7 + Is * 8] = &Z80::op_rst<static_cast<uint16_t>(Is * 8)>), ... );
    }

    static consteval std::array<OpcodeHandler, 256> build_dispatch_table() {
        std::array<OpcodeHandler, 256> table{};
        for (auto& entry : table) entry = &Z80::op_unimplemented;
        
        table[0x00] = &Z80::op_nop;
        table[0x08] = &Z80::op_ex_af_af_prime;
        table[0x10] = &Z80::op_djnz;
        table[0x18] = &Z80::op_jr;
        
        table[0x02] = &Z80::op_ld_bc_a;
        table[0x12] = &Z80::op_ld_de_a;
        table[0x0A] = &Z80::op_ld_a_bc;
        table[0x1A] = &Z80::op_ld_a_de;
        table[0x22] = &Z80::op_ld_nn_hl;
        table[0x2A] = &Z80::op_ld_hl_nn;
        table[0x32] = &Z80::op_ld_nn_a;
        table[0x3A] = &Z80::op_ld_a_nn;
        
        table[0x07] = &Z80::op_rlca;
        table[0x0F] = &Z80::op_rrca;
        table[0x17] = &Z80::op_rla;
        table[0x1F] = &Z80::op_rra;
        table[0x27] = &Z80::op_daa;
        table[0x2F] = &Z80::op_cpl;
        table[0x37] = &Z80::op_scf;
        table[0x3F] = &Z80::op_ccf;
        
        init_rp(table, std::make_index_sequence<4>{});
        init_inc_dec_r(table, std::make_index_sequence<8>{});
        init_ld_r_n(table, std::make_index_sequence<8>{});
        init_jr_c(table, std::make_index_sequence<4>{});
        
        init_ld_r_r(table, std::make_index_sequence<64>{});
        init_alu(table, std::make_index_sequence<8>{});
        
        table[0xC6] = &Z80::op_add_a_n;
        table[0xCE] = &Z80::op_adc_a_n;
        table[0xD6] = &Z80::op_sub_a_n;
        table[0xDE] = &Z80::op_sbc_a_n;
        table[0xE6] = &Z80::op_and_a_n;
        table[0xEE] = &Z80::op_xor_a_n;
        table[0xF6] = &Z80::op_or_a_n;
        table[0xFE] = &Z80::op_cp_a_n;
        
        init_cond_jp(table, std::make_index_sequence<8>{});
        init_push_pop(table, std::make_index_sequence<4>{});
        init_rst(table, std::make_index_sequence<8>{});
        
        table[0xC3] = &Z80::op_jp;
        table[0xCD] = &Z80::op_call;
        table[0xC9] = &Z80::op_ret;
        
        table[0xD3] = &Z80::op_out_n_a;
        table[0xDB] = &Z80::op_in_a_n;
        table[0xD9] = &Z80::op_exx;
        
        table[0xE3] = &Z80::op_ex_sp_hl;
        table[0xEB] = &Z80::op_ex_de_hl;
        table[0xE9] = &Z80::op_jp_hl;
        
        table[0xF3] = &Z80::op_di;
        table[0xFB] = &Z80::op_ei;
        table[0xF9] = &Z80::op_ld_sp_hl;
        
        table[0xCB] = &Z80::op_cb;
        table[0xDD] = &Z80::op_dd;
        table[0xED] = &Z80::op_ed;
        table[0xFD] = &Z80::op_fd;

        return table;
    }

    
};

inline void Z80::step() {
    uint8_t opcode = fetch();
    static constexpr std::array<OpcodeHandler, 256> dispatch_table = build_dispatch_table();
    (this->*dispatch_table[opcode])();
}

inline void Z80::op_cb() {
    uint8_t opcode = fetch();
    static constexpr std::array<OpcodeHandler, 256> cb_table = build_cb_dispatch_table();
    (this->*cb_table[opcode])();
}

inline void Z80::op_dd() {
    uint8_t opcode = fetch();
    static constexpr std::array<OpcodeHandler, 256> dd_table = build_dd_dispatch_table();
    (this->*dd_table[opcode])();
}

inline void Z80::op_ed() {
    uint8_t opcode = fetch();
    static constexpr std::array<OpcodeHandler, 256> ed_table = build_ed_dispatch_table();
    (this->*ed_table[opcode])();
}

inline void Z80::op_fd() {
    uint8_t opcode = fetch();
    static constexpr std::array<OpcodeHandler, 256> fd_table = build_fd_dispatch_table();
    (this->*fd_table[opcode])();
}

} // namespace generator::z80