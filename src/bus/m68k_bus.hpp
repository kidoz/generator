/* SPDX-License-Identifier: GPL-2.0-or-later */
/* 68K-side bus for the emulated machine.
 *
 * Address decode follows the MD memory map and the YM7101 port map
 * (data 0-3, control 4-7, HV 8-F, PSG 10-17, test 18-1F within
 * 0xC00000-0xDFFFFF, mirrored every 0x20).
 *
 * The bus owns nothing: ROM/RAM are borrowed buffers owned by Machine, and
 * device regions forward through BusDevices (implemented by Machine). Every
 * read/write returns the wait states the region inserts so the CPU stalls
 * on DTACK; none are inserted yet — the mechanism exists for arbiter
 * behaviour (VDP DMA stealing, Z80 window contention).
 *
 * Open bus: unmapped reads return the last value driven on the data bus
 * (16-bit latch updated on every completed transfer). */

#pragma once

#include <cstddef>
#include <cstdint>

namespace generator {

/* Region forwards for VDP, IO and Z80-space devices. `upper`/`lower` are
 * the UDS/LDS strobes of the access. */
class BusDevices {
public:
  virtual ~BusDevices() = default;

  virtual uint16_t vdp_read(uint32_t addr, bool upper, bool lower) = 0;
  virtual void vdp_write(uint32_t addr, uint16_t data, bool upper,
                         bool lower) = 0;
  virtual uint16_t io_read(uint32_t addr, bool upper, bool lower) = 0;
  virtual void io_write(uint32_t addr, uint16_t data, bool upper,
                        bool lower) = 0;
  /* Z80 address space window (0xA00000-0xA0FFFF) with the Z80 CPU itself
     not yet in the build; includes Z80 RAM, YM registers, bank latch and
     the 68K bank window. */
  virtual uint16_t z80_read(uint32_t addr, bool upper, bool lower) = 0;
  virtual void z80_write(uint32_t addr, uint16_t data, bool upper,
                         bool lower) = 0;

  /* Interrupt acknowledge cycle. The 68000 runs one when it takes an
     interrupt, and it is what releases the requesting device's line: the
     VDP holds VINT asserted for the whole of vertical blanking, so
     without an acknowledge the handler is re-entered on every RTE until
     the field ends. */
  virtual void interrupt_ack(int level) = 0;
};

class M68kBus {
public:
  explicit M68kBus(BusDevices &devices);

  void attach_rom(const uint8_t *rom, std::size_t size);
  void attach_ram(uint8_t *ram /* 65536 bytes, borrowed */);
  /* SRAM: enabled when the cart header declares a save area.
   * start/size are the 68K address range, write-protected per the
   * header's SRAM info bits. */
  void enable_sram(uint8_t *sram, uint32_t start, uint32_t size, bool writable);
  void disable_sram();
  void reset();

  /* One bus transfer. Returns wait states inserted before DTACK. Word
     accesses must be even (the CPU raises address errors before reaching
     the bus). Byte access selects the byte through the strobes. */
  int read(uint32_t addr, bool upper, bool lower, uint16_t *out);
  int write(uint32_t addr, uint16_t data, bool upper, bool lower);

  /* Interrupt acknowledge cycle (no data transfer on this board: the MD
     terminates it with /VPA and autovectors). */
  void interrupt_ack(int level)
  {
    m_devices.interrupt_ack(level);
  }

  uint16_t open_bus_value() const
  {
    return m_data_bus;
  }

private:
  BusDevices &m_devices;
  const uint8_t *m_rom = nullptr;
  std::size_t m_rom_size = 0;
  uint8_t *m_ram = nullptr;
  uint16_t m_data_bus = 0;
  /* SRAM (borrowed — the Machine owns the buffer) */
  uint8_t *m_sram = nullptr;
  uint32_t m_sram_start = 0;
  uint32_t m_sram_end = 0;
  bool m_sram_writable = false;
};

}  // namespace generator
