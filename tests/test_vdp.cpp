// Characterization tests for the VDP control/data port behavior: register
// write decoding, the two-word address/code latch, write-FIFO accounting,
// and the DMA fill/copy engines. These pin the *current* behavior of
// generator::Vdp (src/video/vdp.cpp) so the C++ rewrite phases (and any
// later timing work) land behind a regression net.
//
// vdp.cpp is compiled directly into this test (the established per-chip
// pattern); the few emulator globals it references (68K regs/ROM/RAM, the
// event freeze hook, ui_err, logging) are stubbed below.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdlib>
#include <cstring>

extern "C" {
#include "generator.h"
#include "cpu68k.h"
#include "event.h"
#include "ui.h"
}

#include "vdp.hpp"

using generator::vdp;

// The single Vdp instance is defined by vdp.cpp, which this test compiles
// directly (see tests/meson.build).

// --- storage and stubs referenced by vdp.cpp ---

static uint8 ram_storage[0x10000];
static uint8 rom_storage[0x200000];

// Save-state plumbing stubs (vdp_save_state is not exercised here).
extern "C" {
void state_transfer8(const char *, const char *, uint8, uint8 *, uint32) {}
void state_transfer16(const char *, const char *, uint8, uint16 *, uint32) {}
void state_transfer32(const char *, const char *, uint8, uint32 *, uint32) {}
}

extern "C" {
uint8 *cpu68k_ram = ram_storage;
uint8 *cpu68k_rom = rom_storage;
unsigned int cpu68k_clocks = 0;
t_regs regs;
unsigned int gen_loglevel = 0;

void event_freeze(unsigned int bytes) { (void)bytes; }
void event_freeze_clocks(unsigned int clocks) { (void)clocks; }
void ui_err(const char *msg, ...) {
  fprintf(stderr, "ui_err: %s\n", msg);
  exit(1);
}
}

namespace {

void reset_chip() {
  std::memset(&ram_storage, 0, sizeof(ram_storage));
  std::memset(&rom_storage, 0, sizeof(rom_storage));
  std::memset(&regs, 0, sizeof(regs));
  vdp.vdp_reset();
}

// VDP register write: 1000 0rrr vvvv vvvv
void write_reg(uint8 reg, uint8 val) { vdp.vdp_storectrl(0x8000 | reg << 8 | val); }

// Two-word address/code latch. code_bits supplies the CD bits (from the
// first word's top two bits) and dma selects the CD5 DMA request bit in the
// second word.
void latch_address(uint16 addr, uint16 code_bits, bool dma) {
  vdp.vdp_storectrl(code_bits << 14 | (addr & 0x3fff));
  vdp.vdp_storectrl(dma ? 0x80 : 0x00);
}

} // namespace

TEST_CASE("VDP register writes decode and bounds-check", "[vdp]")
{
  reset_chip();

  write_reg(12, 0x81);
  REQUIRE(vdp.vdp_reg[12] == 0x81);
  write_reg(0, 0x04);
  REQUIRE(vdp.vdp_reg[0] == 0x04);
  write_reg(24, 0x55);
  REQUIRE(vdp.vdp_reg[24] == 0x55);

  // Registers above 24 do not exist and must be ignored.
  write_reg(25, 0xAA);
  write_reg(31, 0xBB);
  REQUIRE(vdp.vdp_reg[25] == 0);

  // A register write terminates any pending address latch.
  latch_address(0x1234, 0, false);
  write_reg(1, 0x04);
  vdp.vdp_storectrl(0x2222); // starts a new first word, not a latch complete
  REQUIRE(vdp.vdp_ctrlflag == 1);
}

TEST_CASE("VDP two-word address/code latch matches hardware bit layout", "[vdp]")
{
  reset_chip();

  // first = 01aa aaaa aaaa aaaa, second = 0_CD5 00 cd 00
  vdp.vdp_storectrl(0x4123); // code bits 01, addr low 0x0123
  REQUIRE(vdp.vdp_ctrlflag == 1);
  REQUIRE(vdp.vdp_first == 0x4123);
  vdp.vdp_storectrl(0x0008); // code bits (>>2)&0xC = 0
  REQUIRE(vdp.vdp_ctrlflag == 0);
  REQUIRE(vdp.vdp_code == 1); // cd_vram_store
  REQUIRE(vdp.vdp_address == 0x0123);

  // Address top bits come from the second word's low two bits (<<14).
  vdp.vdp_storectrl(0x0055);
  vdp.vdp_storectrl(0x0003); // code (0x0003 >> 2) & 0xC == 0, addr bits 11
  REQUIRE(vdp.vdp_ctrlflag == 0);
  REQUIRE(vdp.vdp_code == 0);
  REQUIRE(vdp.vdp_address == ((0x0055 & 0x3fff) | 0xc000));

  // CRAM store code 3: first-word bits 11, no code bits in the second word.
  vdp.vdp_storectrl(0xC000);
  vdp.vdp_storectrl(0x0000);
  REQUIRE(vdp.vdp_code == 3); // cd_cram_store

  // Second-word code bits (bits 2-3) OR into the first-word bits.
  vdp.vdp_storectrl(0x4000);
  vdp.vdp_storectrl(0x0010); // (0x0010 >> 2) & 0xC = 4
  REQUIRE(vdp.vdp_code == 5); // cd_vsram_store
}

TEST_CASE("VDP write FIFO counts entries and reports status bits", "[vdp]")
{
  reset_chip();

  // After reset the FIFO is empty: status bit 9 set, bit 8 clear.
  REQUIRE(vdp.vdp_fifo_count == 0);
  REQUIRE(vdp.vdp_fifoempty == 1);
  REQUIRE(vdp.vdp_fifofull == 0);

  latch_address(0x0000, 1, false); // code 1 = VRAM store, addr 0

  // Four VRAM writes fill the FIFO.
  for (int i = 0; i < 3; ++i) {
    vdp.vdp_storedata(0x1100 + i);
    REQUIRE(vdp.vdp_fifoempty == 0);
    REQUIRE(vdp.vdp_fifofull == 0);
  }
  vdp.vdp_storedata(0x1103);
  REQUIRE(vdp.vdp_fifo_count == 4);
  REQUIRE(vdp.vdp_fifofull == 1);

  // Status register exposes fifo full (bit 8) / empty (bit 9).
  const uint16 st = vdp.vdp_status();
  REQUIRE(((st >> 8) & 1) == 1);
  REQUIRE(((st >> 9) & 1) == 0);

  // Draining is bounded: over-drain clamps at zero.
  vdp.vdp_fifo_drain(2);
  REQUIRE(vdp.vdp_fifo_count == 2);
  REQUIRE(vdp.vdp_fifofull == 0);
  vdp.vdp_fifo_drain(99);
  REQUIRE(vdp.vdp_fifo_count == 0);
  REQUIRE(vdp.vdp_fifoempty == 1);
}

TEST_CASE("VDP DMA fill writes the fill byte at byte-swapped addresses", "[vdp]")
{
  reset_chip();

  write_reg(1, 0x10);  // DMA enable (bit 4)
  write_reg(15, 0x02); // address increment 2
  write_reg(19, 0x04); // length lo = 4 words
  write_reg(20, 0x00);
  write_reg(23, 0x80); // DMA mode 2 = VRAM fill

  // Latch destination 0 with code 1 (VRAM store) and the DMA request bit.
  latch_address(0x0000, 1, true);
  REQUIRE(vdp.vdp_dmabusy == 1);

  vdp.vdp_storedata(0xAB00); // high byte becomes the fill byte

  // The direct word write lands first (LOCENDIAN16 store puts 0xAB at byte
  // 0), then the address has advanced by the increment (2) and the fill
  // loop writes address^1 from there: bytes 3, 5, 7, 9.
  REQUIRE(vdp.vdp_vram[0] == 0xAB);
  REQUIRE(vdp.vdp_vram[1] == 0x00);
  for (int i = 0; i < 4; ++i)
    REQUIRE(vdp.vdp_vram[3 + 2 * i] == 0xAB);
  REQUIRE(vdp.vdp_vram[8] == 0x00); // even bytes past the word write stay clear
  REQUIRE(vdp.vdp_vram[11] == 0x00); // past the filled range

  // Length registers clear and the busy accounting advances.
  REQUIRE(vdp.vdp_reg[19] == 0);
  REQUIRE(vdp.vdp_reg[20] == 0);
  REQUIRE(vdp.vdp_dmabytes == 4 + 1);
}

TEST_CASE("VDP DMA copy duplicates VRAM through the source registers", "[vdp]")
{
  reset_chip();

  // Seed a source pattern at 0x0010.
  for (int i = 0; i < 8; ++i)
    vdp.vdp_vram[0x10 + i] = static_cast<uint8_t>(0xF0 + i);

  write_reg(1, 0x10);  // DMA enable
  write_reg(15, 0x01); // increment 1
  write_reg(19, 0x08); // length 8 bytes
  write_reg(20, 0x00);
  write_reg(21, 0x10); // source lo
  write_reg(22, 0x00);
  write_reg(23, 0xC0); // DMA mode 3 = VRAM copy

  // Destination 0x0020, code 1, DMA request: the copy runs immediately.
  latch_address(0x0020, 1, true);

  for (int i = 0; i < 8; ++i)
    REQUIRE(vdp.vdp_vram[0x20 + i] == static_cast<uint8_t>(0xF0 + i));

  // Source registers advance by the copied length; length clears;
  // dmabytes tracks length*2 (copy counts double per the timing model).
  REQUIRE(vdp.vdp_reg[21] == 0x18);
  REQUIRE(vdp.vdp_reg[19] == 0);
  REQUIRE(vdp.vdp_dmabytes == 8 * 2);
}
