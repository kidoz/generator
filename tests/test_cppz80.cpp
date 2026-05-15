#include <catch2/catch_test_macros.hpp>
#include <vector>
#include <cstdint>

#include "../src/cpu/z80/cppz80/z80.hpp"

using namespace generator::z80;

struct TestZ80 {
    Z80 cpu;
    std::vector<uint8_t> memory;

    TestZ80() : memory(65536, 0) {
        cpu.set_callbacks(
            [](uint16_t addr) -> uint8_t { return test_instance->memory[addr]; },
            [](uint16_t addr, uint8_t data) { test_instance->memory[addr] = data; },
            [](uint16_t port) -> uint8_t { return 0xFF; },
            [](uint16_t port, uint8_t data) {}
        );
        test_instance = this;
        cpu.reset();
    }

    ~TestZ80() {
        test_instance = nullptr;
    }

    void load_program(const std::vector<uint8_t>& prog, uint16_t start = 0) {
        for (size_t i = 0; i < prog.size(); ++i) {
            memory[start + i] = prog[i];
        }
        cpu.set_pc(start);
    }

    void step() {
        cpu.execute(4); // Execute one small step (at least)
    }

    static TestZ80* test_instance;
};

TestZ80* TestZ80::test_instance = nullptr;

TEST_CASE("Z80 8-bit Arithmetic (ADD A, r)", "[z80][cppz80]") {
    TestZ80 z;

    SECTION("ADD A, B without carry") {
        z.cpu.set_af(0x2000); // A = 0x20
        z.cpu.set_bc(0x3000); // B = 0x30
        
        // 0x80 = ADD A, B
        z.load_program({0x80});
        z.step();
        
        REQUIRE((z.cpu.get_af() >> 8) == 0x50); // A should be 0x50
        REQUIRE(z.cpu.get_flag(Z80::FLAG_C) == false);
        REQUIRE(z.cpu.get_flag(Z80::FLAG_Z) == false);
        REQUIRE(z.cpu.get_flag(Z80::FLAG_S) == false);
        REQUIRE(z.cpu.get_flag(Z80::FLAG_PV) == false);
    }

    SECTION("ADD A, A with carry") {
        z.cpu.set_af(0x8000); // A = 0x80
        
        // 0x87 = ADD A, A
        z.load_program({0x87});
        z.step();
        
        REQUIRE((z.cpu.get_af() >> 8) == 0x00); // A should be 0x00
        REQUIRE(z.cpu.get_flag(Z80::FLAG_C) == true);
        REQUIRE(z.cpu.get_flag(Z80::FLAG_Z) == true);
        REQUIRE(z.cpu.get_flag(Z80::FLAG_PV) == true); // Overflow for 0x80 + 0x80
        REQUIRE(z.cpu.get_flag(Z80::FLAG_H) == false);
    }
}

TEST_CASE("Z80 8-bit Load (LD r, r')", "[z80][cppz80]") {
    TestZ80 z;

    SECTION("LD B, C") {
        z.cpu.set_bc(0x0042); // C = 0x42
        
        // 0x41 = LD B, C
        z.load_program({0x41});
        z.step();
        
        REQUIRE((z.cpu.get_bc() >> 8) == 0x42); // B should be 0x42
        REQUIRE((z.cpu.get_bc() & 0xFF) == 0x42); // C should still be 0x42
    }
}
