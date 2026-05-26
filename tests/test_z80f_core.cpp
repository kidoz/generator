// Smoke test against the external z80f core (kidoz/z80f).
// Verifies the API surface generator's adapter relies on:
//   - Bus subclassing with read/write_memory, read/write_io, on_m_cycle
//   - Z80::reset, step, run_for, set_int_line, registers(), cycle_counter()
//   - Snapshot save/load round-trip
//
// This is intentionally a "does it bolt together" check, not a CPU correctness
// suite — z80f ships its own Catch2 tests upstream for that.

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>

#include <z80f/z80.hpp>
#include <z80f/bus.hpp>
#include <z80f/snapshot.hpp>

namespace {

class FlatBus final : public z80f::Bus {
public:
    std::array<std::uint8_t, 0x10000> ram{};

    std::uint8_t read_memory(std::uint16_t addr) override { return ram[addr]; }
    void write_memory(std::uint16_t addr, std::uint8_t v) override { ram[addr] = v; }
    std::uint8_t read_io(std::uint16_t) override { return 0xFF; }
    void write_io(std::uint16_t, std::uint8_t) override {}
    int on_m_cycle(std::uint16_t, int) override { return 0; }
};

}  // namespace

TEST_CASE("z80f: LD A,n; HALT runs and reports cycles", "[z80f]") {
    FlatBus bus;
    z80f::Z80 cpu(bus);

    // LD A,0x42 ; HALT
    bus.ram[0x0000] = 0x3E;
    bus.ram[0x0001] = 0x42;
    bus.ram[0x0002] = 0x76;

    cpu.reset();

    int budget = 256;
    while (!cpu.registers().halted && budget-- > 0) {
        cpu.step();
    }

    REQUIRE(cpu.registers().halted);
    REQUIRE(cpu.registers().a == 0x42);
    REQUIRE(cpu.cycle_counter() > 0);
}

TEST_CASE("z80f: snapshot round-trip preserves registers", "[z80f]") {
    FlatBus bus;
    z80f::Z80 cpu(bus);
    cpu.reset();

    // NOP so we have at least one instruction's worth of state advance.
    bus.ram[0x0000] = 0x00;
    cpu.step();

    z80f::Snapshot snap = cpu.save_snapshot();
    snap.registers.set_bc(0xCAFE);
    snap.registers.set_de(0xBEEF);
    snap.registers.pc = 0x1234;

    cpu.load_snapshot(snap);

    REQUIRE(cpu.registers().bc() == 0xCAFE);
    REQUIRE(cpu.registers().de() == 0xBEEF);
    REQUIRE(cpu.registers().pc == 0x1234);
}

TEST_CASE("z80f: INT line + IM 1 vectors to 0x0038", "[z80f]") {
    FlatBus bus;
    z80f::Z80 cpu(bus);

    // EI ; HALT  at 0x0000
    bus.ram[0x0000] = 0xED;
    bus.ram[0x0001] = 0x56;  // IM 1
    bus.ram[0x0002] = 0xFB;  // EI
    bus.ram[0x0003] = 0x76;  // HALT

    cpu.reset();
    // Run a few instructions to execute IM 1 + EI + HALT.
    for (int i = 0; i < 8 && !cpu.registers().halted; ++i) {
        cpu.step();
    }
    REQUIRE(cpu.registers().halted);
    REQUIRE(cpu.registers().iff1);

    cpu.set_int_line(true);
    cpu.step();  // service the IRQ
    cpu.set_int_line(false);

    REQUIRE(cpu.registers().pc == 0x0038);
    REQUIRE_FALSE(cpu.registers().iff1);
    REQUIRE_FALSE(cpu.registers().halted);
}
