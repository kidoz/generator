/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "m68k.hpp"

#include "m68k_bus.hpp"

namespace generator {


namespace {

constexpr int kVecIllegal = 4;
constexpr int kVecTrap0 = 32;
constexpr int kAutovectorBase = 25; /* vectors 25..31 = levels 1..7 */

constexpr int kClkPerBusAccess = 4; /* one 68K bus cycle = 4 clocks */
constexpr int kMclkPerClk = 7;      /* 68K clock = master / 7 */

/* Work-RAM refresh: the arbiter takes the bus off the CPU for two clocks
 * out of every 128 to refresh the DRAM, so the CPU only ever gets 126 of
 * them. Worth about 1.6% of the machine's throughput — small per
 * instruction, but boot-time handshakes between the two CPUs are decided
 * by exactly this much drift over a few hundred thousand instructions. */
constexpr int kRefreshPeriodClk = 128;
constexpr int kRefreshStallClk = 2;

int decode_size(int bits /* op bits 7-6 */)
{
  return 1 << (bits & 3); /* 0->1, 1->2, 2->4 */
}

/* Decimal adjust of dst +/- src +/- x over byte BCD operands: returns the
 * BCD result and reports the decimal carry/borrow out. */
uint8_t bcd_adjust(uint32_t dst, uint32_t src, bool is_add, bool x, bool &carry)
{
  if (is_add) {
    uint32_t r = (dst & 0xFF) + (src & 0xFF) + (x ? 1u : 0u);
    if ((r & 0xF) > 9) {
      r += 6;
    }
    carry = r > 0x99;
    if (carry) {
      r += 0x60;
    }
    return (uint8_t)r;
  }
  /* subtract: decimal per nibble, with the low-nibble borrow carried into
   * the high digit; a high-digit borrow is the BCD carry out */
  int lo = (int)(dst & 0xF) - (int)(src & 0xF) - (x ? 1 : 0);
  int hi = (int)((dst >> 4) & 0xF) - (int)((src >> 4) & 0xF);
  if (lo < 0) {
    lo += 10;
    hi -= 1;
  }
  carry = hi < 0;
  if (carry) {
    hi += 10;
  }
  return (uint8_t)((hi << 4) | lo);
}

}  // namespace

M68k::M68k(M68kBus &bus, MasterClockSink &clock) : m_bus(bus), m_clock(clock)
{
}

void M68k::set_trace(std::function<void(const TraceEntry &)> trace)
{
  m_trace = std::move(trace);
}

void M68k::set_ipl(int level)
{
  m_ipl = level & 7;
}

/* ------------------------------------------------------------------ */
/* clock + bus engine                                                  */
/* ------------------------------------------------------------------ */

void M68k::advance_clk(int clocks)
{
  m_clk += (uint64_t)clocks;
  m_clock.advance_mclk((uint64_t)clocks * kMclkPerClk);

  m_refresh_acc += clocks;
  while (m_refresh_acc >= kRefreshPeriodClk - kRefreshStallClk) {
    m_refresh_acc -= kRefreshPeriodClk - kRefreshStallClk;
    m_clk += kRefreshStallClk;
    m_clock.advance_mclk((uint64_t)kRefreshStallClk * kMclkPerClk);
  }
}

void M68k::trace_bus(TraceEntry::Kind kind, uint32_t addr, int size,
                     uint16_t value, bool upper, bool lower)
{
  if (m_trace) {
    m_trace(TraceEntry{kind, addr, size, value, upper, lower, m_clk});
  }
}

uint16_t M68k::bus_read_word(uint32_t addr)
{
  if (addr & 1) {
    address_error(addr);
    return 0;
  }
  advance_clk(kClkPerBusAccess);
  uint16_t value = 0;
  m_bus.read(addr, true, true, &value);
  trace_bus(TraceEntry::Kind::Read, addr, 2, value, true, true);
  return value;
}

uint8_t M68k::bus_read_byte(uint32_t addr)
{
  advance_clk(kClkPerBusAccess);
  uint16_t word = 0;
  const bool upper = (addr & 1) == 0;
  m_bus.read(addr & ~1u, upper, !upper, &word);
  const uint8_t value = upper ? (uint8_t)(word >> 8) : (uint8_t)word;
  trace_bus(TraceEntry::Kind::Read, addr, 1, value, upper, !upper);
  return value;
}

void M68k::bus_write_word(uint32_t addr, uint16_t v)
{
  if (addr & 1) {
    address_error(addr);
    return;
  }
  advance_clk(kClkPerBusAccess);
  m_bus.write(addr, v, true, true);
  trace_bus(TraceEntry::Kind::Write, addr, 2, v, true, true);
}

void M68k::bus_write_byte(uint32_t addr, uint8_t v)
{
  advance_clk(kClkPerBusAccess);
  const bool upper = (addr & 1) == 0;
  m_bus.write(addr & ~1u, (uint16_t)((uint16_t)v << 8 | v), upper, !upper);
  trace_bus(TraceEntry::Kind::Write, addr, 1, v, upper, !upper);
}

/* ------------------------------------------------------------------ */
/* prefetch queue                                                      */
/* ------------------------------------------------------------------ */

uint16_t M68k::fetch_stream_word()
{
  if (m_queue_len > 0) {
    const uint16_t word = m_queue[0];
    m_queue[0] = m_queue[1];
    m_queue_len--;
    return word;
  }
  advance_clk(kClkPerBusAccess);
  uint16_t word = 0;
  m_bus.read(m_prefetch_addr, true, true, &word);
  trace_bus(TraceEntry::Kind::Fetch, m_prefetch_addr, 2, word, true, true);
  m_prefetch_addr += 2;
  return word;
}

void M68k::prefetch_fill(int words)
{
  while (m_queue_len < words && m_queue_len < 2) {
    advance_clk(kClkPerBusAccess);
    uint16_t word = 0;
    m_bus.read(m_prefetch_addr, true, true, &word);
    trace_bus(TraceEntry::Kind::Fetch, m_prefetch_addr, 2, word, true, true);
    m_queue[m_queue_len++] = word;
    m_prefetch_addr += 2;
  }
}

void M68k::flush_stream(uint32_t new_pc)
{
  m_queue_len = 0;
  m_prefetch_addr = new_pc;
}

/* ------------------------------------------------------------------ */
/* conditions + flags                                                  */
/* ------------------------------------------------------------------ */

bool M68k::cond(int cc4) const
{
  const bool n = (m_sr & SR_N) != 0, z = (m_sr & SR_Z) != 0,
             v = (m_sr & SR_V) != 0, c = (m_sr & SR_C) != 0;
  switch (cc4) {
  case 0x0:
    return true;
  case 0x1:
    return false;
  case 0x2:
    return !c && !z;
  case 0x3:
    return c || z;
  case 0x4:
    return !c;
  case 0x5:
    return c;
  case 0x6:
    return !z;
  case 0x7:
    return z;
  case 0x8:
    return !v;
  case 0x9:
    return v;
  case 0xA:
    return !n;
  case 0xB:
    return n;
  case 0xC:
    return n == v;
  case 0xD:
    return n != v;
  case 0xE:
    return !z && n == v;
  case 0xF:
    return z || n != v;
  }
  return false;
}

uint32_t M68k::bmask(int size)
{
  return size == 1 ? 0xFFu : size == 2 ? 0xFFFFu : 0xFFFFFFFFu;
}

uint32_t M68k::bsign(uint32_t v, int size)
{
  return v & (size == 1 ? 0x80u : size == 2 ? 0x8000u : 0x80000000u);
}

void M68k::set_zn(uint32_t res, int size)
{
  m_sr = (uint16_t)(m_sr & ~(SR_Z | SR_N));
  if ((res & bmask(size)) == 0) {
    m_sr |= SR_Z;
  }
  if (bsign(res, size)) {
    m_sr |= SR_N;
  }
}

void M68k::flags_logic(uint32_t res, int size)
{
  m_sr = (uint16_t)(m_sr & ~(SR_C | SR_V | SR_Z | SR_N));
  set_zn(res, size);
}

void M68k::flags_add(uint32_t a, uint32_t b, uint32_t res, int size,
                     uint32_t carry_in)
{
  const uint32_t m = bmask(size);
  const int shift = size * 8 - 1;
  const uint32_t x = a & m, y = b & m, r = res & m;
  const uint16_t clear = SR_C | SR_V | SR_Z | SR_N | SR_X;
  m_sr = (uint16_t)(m_sr & ~clear);
  if (r == 0) {
    m_sr |= SR_Z;
  }
  if (r >> shift & 1) {
    m_sr |= SR_N;
  }
  if ((uint64_t)x + y + carry_in > m) {
    m_sr |= SR_C | SR_X;
  }
  const bool sn = (x >> shift & 1) != 0, sm = (y >> shift & 1) != 0,
             sr = (r >> shift & 1) != 0;
  if (sn == sm && sn != sr) {
    m_sr |= SR_V;
  }
}

void M68k::flags_sub(uint32_t a, uint32_t b, uint32_t res, int size,
                     uint32_t borrow_in)
{
  /* res = a - b */
  const uint32_t m = bmask(size);
  const int shift = size * 8 - 1;
  const uint32_t x = a & m, y = b & m, r = res & m;
  const uint16_t clear = SR_C | SR_V | SR_Z | SR_N | SR_X;
  m_sr = (uint16_t)(m_sr & ~clear);
  if (r == 0) {
    m_sr |= SR_Z;
  }
  if (r >> shift & 1) {
    m_sr |= SR_N;
  }
  if ((uint64_t)x < (uint64_t)y + borrow_in) {
    m_sr |= SR_C | SR_X;
  }
  const bool sn = (x >> shift & 1) != 0, sm = (y >> shift & 1) != 0,
             sr = (r >> shift & 1) != 0;
  if (sn != sm && sn != sr) {
    m_sr |= SR_V;
  }
}

/* CMP, CMPA, CMPI and CMPM report the subtraction through C, V, Z and N
 * but leave X alone — X belongs to the multi-precision chain, and a
 * compare must not disturb it between an arithmetic op and its ADDX/SUBX. */
void M68k::flags_cmp(uint32_t a, uint32_t b, uint32_t res, int size)
{
  const uint16_t x = (uint16_t)(m_sr & SR_X);
  flags_sub(a, b, res, size);
  m_sr = (uint16_t)((m_sr & ~SR_X) | x);
}

/* ------------------------------------------------------------------ */
/* exceptions + stack banking                                          */
/* ------------------------------------------------------------------ */

void M68k::switch_supervisor(bool to_supervisor)
{
  const bool was = (m_sr & SR_S) != 0;
  if (was == to_supervisor) {
    return;
  }
  if (to_supervisor) {
    m_usp = m_a[7];
    m_a[7] = m_ssp;
    m_sr |= SR_S;
  } else {
    m_ssp = m_a[7];
    m_a[7] = m_usp;
    m_sr = (uint16_t)(m_sr & ~SR_S);
  }
}

void M68k::exception(int vector, uint32_t pushed_pc, int internal_clocks,
                     int interrupt_level)
{
  /* Standard 68000 exception entry: internal cycles, push PC, push SR,
   * fetch new PC from the vector. Charging: internal_clocks + 2, then
   * 4 per pushed/fetched word. unverified. */

  /* The stacked copy is taken before any of the entry-side edits: the
   * interrupt mask an interrupt raises to its own level belongs to the
   * handler, not to the code it interrupted. Raising it first would let
   * RTE restore the raised mask and leave the machine deaf to every
   * later interrupt of that level. */
  const uint16_t saved_sr = m_sr;
  if (interrupt_level >= 0) {
    m_sr = (uint16_t)((m_sr & ~SR_I) | (uint16_t)(interrupt_level << 8));
  }
  /* Entering supervisor mode banks the stack pointer: the frame below
   * belongs on the supervisor stack even when user code was interrupted. */
  switch_supervisor(true);
  m_sr = (uint16_t)(m_sr & ~SR_T);

  advance_clk(internal_clocks + 2);

  /* The 68000 lays the group-1/2 frame down out of order: PC low word
   * first, then SR, then PC high word. The resting frame is the usual
   * SR/PC pair — only the bus sequence is interleaved. */
  const uint32_t sp = m_a[7] - 6;
  bus_write_word(sp + 4, (uint16_t)pushed_pc);
  if (aborted()) {
    return;
  }
  bus_write_word(sp, saved_sr);
  if (aborted()) {
    return;
  }
  bus_write_word(sp + 2, (uint16_t)(pushed_pc >> 16));
  if (aborted()) {
    return;
  }
  m_a[7] = sp;

  const uint32_t vector_addr = (uint32_t)vector * 4;
  const uint16_t pc_hi = bus_read_word(vector_addr);
  if (aborted()) {
    return;
  }
  const uint16_t pc_lo = bus_read_word(vector_addr + 2);
  if (aborted()) {
    return;
  }
  m_pc = ((uint32_t)pc_hi << 16) | pc_lo;
  flush_stream(m_pc);
  prefetch_fill(2);
}

void M68k::address_error(uint32_t fault_addr)
{
  if (m_in_exception) {
    m_fault = Fault{Fault::Kind::Halted, m_pc, 0};
    m_abort = true;
    return;
  }
  m_in_exception = true;

  /* 68000 address-error frame: SR, PC, fault address, IR and internal
   * words. Layout per M68000UM; unverified. */
  const uint16_t saved_sr = m_sr;
  m_sr |= SR_S;

  uint32_t sp = m_a[7] - 16;
  bus_write_word(sp + 14, saved_sr);
  bus_write_word(sp + 10, (uint16_t)(m_pc >> 16));
  bus_write_word(sp + 12, (uint16_t)m_pc);
  bus_write_word(sp + 6, (uint16_t)(fault_addr >> 16));
  bus_write_word(sp + 8, (uint16_t)fault_addr);
  bus_write_word(sp + 4, 0);
  bus_write_word(sp + 2, 0);
  bus_write_word(sp, 0);
  if (aborted()) {
    m_in_exception = false;
    return;
  }
  m_a[7] = sp;

  const uint32_t vector_addr = 3 * 4;
  const uint16_t pc_hi = bus_read_word(vector_addr);
  if (aborted()) {
    m_in_exception = false;
    return;
  }
  const uint16_t pc_lo = bus_read_word(vector_addr + 2);
  if (aborted()) {
    m_in_exception = false;
    return;
  }
  m_pc = ((uint32_t)pc_hi << 16) | pc_lo;
  flush_stream(m_pc);
  prefetch_fill(2);
  m_in_exception = false;
  m_abort = true;
}

void M68k::interrupt_exception(int level)
{
  /* Autovector interrupt; IACK terminated by /VPA on the MD. The device
   * deasserts its request when acknowledged; the taken level is latched
   * into the SR mask. unverified. */
  m_ipl = 7;
  m_fault = Fault{};
  /* The acknowledge cycle releases the device's request line before the
   * handler runs; the MD terminates it with /VPA, so the vector is the
   * autovector for the level. */
  m_bus.interrupt_ack(level);
  exception(kAutovectorBase - 1 + level, m_pc, 6, level);
}

/* ------------------------------------------------------------------ */
/* effective addresses                                                 */
/* ------------------------------------------------------------------ */

uint32_t M68k::ea_addr(const Ea &ea, int size)
{
  switch (ea.mode) {
  case 2:
    return m_a[ea.reg];
  case 3:
    return m_a[ea.reg];
  case 4:
    m_a[ea.reg] -= (uint32_t)size;
    return m_a[ea.reg];
  case 5:
    return m_a[ea.reg] + (uint32_t)(int32_t)(int16_t)fetch_stream_word();
  case 6: {
    const uint16_t brief = fetch_stream_word();
    const int8_t disp = (int8_t)(brief & 0xFF);
    uint32_t index =
        (brief & 0x8000) != 0 ? m_a[(brief >> 12) & 7] : m_d[(brief >> 12) & 7];
    if ((brief & 0x800) == 0) {
      index = (uint32_t)(int32_t)(int16_t)index;
    }
    return m_a[ea.reg] + (uint32_t)(int32_t)disp + index;
  }
  case 7:
    switch (ea.reg) {
    case 0:
      return (uint32_t)(int32_t)(int16_t)fetch_stream_word();
    case 1: {
      const uint16_t hi = fetch_stream_word();
      const uint16_t lo = fetch_stream_word();
      return ((uint32_t)hi << 16) | lo;
    }
    case 2:
      return m_pc + (uint32_t)(int32_t)(int16_t)fetch_stream_word();
    case 3: {
      const uint16_t brief = fetch_stream_word();
      const int8_t disp = (int8_t)(brief & 0xFF);
      uint32_t index = (brief & 0x8000) != 0 ? m_a[(brief >> 12) & 7]
                                             : m_d[(brief >> 12) & 7];
      if ((brief & 0x800) == 0) {
        index = (uint32_t)(int32_t)(int16_t)index;
      }
      return m_pc + (uint32_t)(int32_t)disp + index;
    }
    default:
      break;
    }
    break;
  default:
    break;
  }
  /* An EA outside the mode set legal for memory operands. Report the
   * actual opcode word, not the EA encoding: diagnosing a derail needs
   * the instruction that ran, and mode 1 here stands in for several
   * reserved bit patterns. */
  m_fault = Fault{Fault::Kind::UnimplementedOpcode, m_pc, m_cur_op};
  return 0;
}

uint32_t M68k::ea_read(const Ea &ea, int size)
{
  if (ea.mode == 0) {
    return m_d[ea.reg];
  }
  if (ea.mode == 1) {
    return m_a[ea.reg];
  }
  if (ea.mode == 7 && ea.reg == 4) { /* #imm */
    if (size == 4) {
      const uint16_t hi = fetch_stream_word();
      const uint16_t lo = fetch_stream_word();
      return ((uint32_t)hi << 16) | lo;
    }
    return fetch_stream_word();
  }
  if (ea.mode == 4) { /* -(An): predecrement */
    m_a[ea.reg] -= (uint32_t)size;
    const uint32_t addr = m_a[ea.reg];
    return read_mem(addr, size);
  }
  const uint32_t addr = ea_addr(ea, size);
  if (m_fault.kind == Fault::Kind::UnimplementedOpcode || aborted()) {
    return 0;
  }
  const uint32_t value = read_mem(addr, size);
  if (ea.mode == 3) { /* (An)+ */
    m_a[ea.reg] += (uint32_t)size;
  }
  return value;
}

void M68k::ea_write(const Ea &ea, int size, uint32_t value)
{
  if (ea.mode == 0) {
    m_d[ea.reg] = size == 1   ? (m_d[ea.reg] & ~0xFFu) | (value & 0xFF)
                  : size == 2 ? (m_d[ea.reg] & ~0xFFFFu) | (value & 0xFFFF)
                              : value;
    return;
  }
  if (ea.mode == 1) {
    m_a[ea.reg] = value;
    return;
  }
  if (ea.mode == 4) {
    m_a[ea.reg] -= (uint32_t)size;
    write_mem(m_a[ea.reg], size, value, size == 4);
    return;
  }
  const uint32_t addr = ea_addr(ea, size);
  if (m_fault.kind == Fault::Kind::UnimplementedOpcode || aborted()) {
    return;
  }
  write_mem(addr, size, value, size == 4);
  if (ea.mode == 3) {
    m_a[ea.reg] += (uint32_t)size;
  }
}

uint32_t M68k::read_mem(uint32_t addr, int size)
{
  if (size == 1) {
    return bus_read_byte(addr);
  }
  if (size == 2) {
    return bus_read_word(addr);
  }
  const uint16_t hi = bus_read_word(addr);
  if (aborted()) {
    return 0;
  }
  const uint16_t lo = bus_read_word(addr + 2);
  return ((uint32_t)hi << 16) | lo;
}

/* low_word_first covers the two 68000 cases where a long reaches memory
 * low word first: a -(An) destination (the address counts down) and every
 * read-modify-write form (CLR, NEG, NOT, and the ALU ops with a memory
 * destination), whose internal sequence is read high, read low, write low,
 * write high. A plain MOVE.L to any other mode writes high word first. */
void M68k::write_mem(uint32_t addr, int size, uint32_t value,
                     bool low_word_first)
{
  if (size == 1) {
    bus_write_byte(addr, (uint8_t)value);
  } else if (size == 2) {
    bus_write_word(addr, (uint16_t)value);
  } else if (low_word_first) {
    bus_write_word(addr + 2, (uint16_t)value);
    if (!aborted()) {
      bus_write_word(addr, (uint16_t)(value >> 16));
    }
  } else {
    bus_write_word(addr, (uint16_t)(value >> 16));
    if (!aborted()) {
      bus_write_word(addr + 2, (uint16_t)value);
    }
  }
}

/* ------------------------------------------------------------------ */
/* reset + step                                                        */
/* ------------------------------------------------------------------ */

void M68k::power_on_reset()
{
  m_fault = Fault{};
  m_abort = false;
  m_in_exception = false;
  m_ipl = 7;
  m_usp = 0;
  m_ssp = 0;
  m_sr = SR_S | SR_I;

  /* Reset sequence: fetch SSP and PC from vectors 0/1 (4 fetches) plus
   * internal cycles; the absolute constant is unverified. */
  const uint16_t ssp_hi = bus_read_word(0x000000);
  const uint16_t ssp_lo = bus_read_word(0x000002);
  const uint16_t pc_hi = bus_read_word(0x000004);
  const uint16_t pc_lo = bus_read_word(0x000006);
  m_a[7] = ((uint32_t)ssp_hi << 16) | ssp_lo;
  m_ssp = m_a[7];
  m_pc = ((uint32_t)pc_hi << 16) | pc_lo;
  advance_clk(8);
  flush_stream(m_pc);
  /* Fill only the opcode word: instructions start with a one-word queue
   * (opcode in IRD); their trailing refills restore it, which is what the
   * published standalone cycle tables assume. */
  prefetch_fill(1);
}

void M68k::step()
{
  if (halted()) {
    return;
  }
  m_abort = false;

  const int request = m_ipl;
  const int mask = (m_sr & SR_I) >> 8;
  const bool interrupt_pending = request != 7 && request > mask;

  if (stopped()) {
    if (interrupt_pending) {
      interrupt_exception(request);
    }
    return;
  }
  if (interrupt_pending) {
    interrupt_exception(request);
    return;
  }

  const uint16_t opcode = fetch_stream_word();
  if (aborted()) {
    return;
  }
  m_cur_op = opcode;
  /* PC tracks the next unfetched stream word (prefetch address minus the
   * queued words): after the opcode pop it is the address following the
   * opcode; after execute() it is the next instruction's address. */
  m_pc = m_prefetch_addr - 2 * (uint32_t)m_queue_len;
  execute(opcode);
  m_pc = m_prefetch_addr - 2 * (uint32_t)m_queue_len;
}

/* ------------------------------------------------------------------ */
/* decode                                                              */
/* ------------------------------------------------------------------ */

void M68k::execute(uint16_t op)
{
  const int line = op >> 12;

  if (line <= 0x3 && line >= 0x1) {
    if ((op & 0x01C0) == 0x0040) {
      exec_movea(op);
    } else {
      exec_move(op);
    }
    return;
  }

  switch (line) {
  case 0x0:
    if ((op & 0x0100) != 0 && (op & 0x0038) == 0x0008) {
      exec_movep(op); /* MOVEP shares 0x01xx with dynamic bit ops */
      return;
    }
    /* Static bit ops occupy 0x08xx alone — bits 11-8 must read 1000. A
     * wider mask here swallows the immediate ALU forms that share the
     * line: EORI (0x0Axx), CMPI (0x0Cxx) and 0x0Exx would all execute as
     * BTST/BCHG/BCLR/BSET. */
    if ((op & 0x0100) != 0 || (op & 0xFF00) == 0x0800) {
      /* Dynamic bit ops are any line-0 opcode with bit 8 set (MOVEP,
       * which shares that bit, is already gone); the register holding the
       * bit number sits in bits 11-9, so they span 0x01xx through 0x0Fxx
       * and not 0x01xx alone. Static ones are 0x08xx. */
      exec_bitop(op);
      return;
    }
    if ((op & 0x0100) == 0) {
      exec_immediate(op); /* ORI/ANDI/SUBI/ADDI/EORI/CMPI + SR/CCR */
      return;
    }
    break;
  case 0x4:
    exec_single(op);
    return;
  case 0x5:
    if ((op & 0x00C0) == 0x00C0) {
      if ((op & 0x0038) == 0x0008) {
        exec_dbcc(op);
      } else {
        exec_scc(op);
      }
    } else {
      exec_addq_subq(op);
    }
    return;
  case 0x6:
    exec_branch(op);
    return;
  case 0x7:
    exec_moveq(op);
    return;
  case 0x8:
    exec_alu(op, 0); /* OR */
    return;
  case 0x9:
    exec_alu(op, 1); /* SUB */
    return;
  case 0xB:
    exec_alu(op, 2); /* CMP / EOR / CMPM via bit 8 */
    return;
  case 0xC:
    exec_alu(op, 3); /* AND */
    return;
  case 0xD:
    exec_alu(op, 4); /* ADD */
    return;
  case 0xE:
    exec_shift(op);
    return;
  default:
    break;
  }

  m_fault = Fault{Fault::Kind::UnimplementedOpcode, m_pc, op};
}

/* ------------------------------------------------------------------ */
/* MOVE family                                                         */
/* ------------------------------------------------------------------ */

void M68k::exec_move(uint16_t op)
{
  const int size = (op & 0x3000) == 0x1000   ? 1
                   : (op & 0x3000) == 0x3000 ? 2
                                             : 4;
  const Ea src{(op >> 3) & 7, op & 7};
  const Ea dst{(op >> 6) & 7, (op >> 9) & 7};

  if (!ea_is_memory(dst)) {
    const uint32_t value = ea_read(src, size);
    if (aborted() || m_fault.kind == Fault::Kind::UnimplementedOpcode) {
      return;
    }
    ea_write(dst, size, value);
    flags_logic(value, size);
    /* reg->reg: one trailing refill (4 clocks); mem->reg: two (12/16) */
    prefetch_fill(ea_is_memory(src) ? 2 : 1);
    return;
  }

  if (!ea_is_memory(src)) {
    const uint32_t value = ea_read(src, size);
    if (aborted() || m_fault.kind == Fault::Kind::UnimplementedOpcode) {
      return;
    }
    const uint32_t addr = ea_addr(dst, size);
    if (aborted() || m_fault.kind == Fault::Kind::UnimplementedOpcode) {
      return;
    }
    /* Predecrement is the one destination mode where the 68000 puts the
     * low word of a long on the bus first: the effective address counts
     * down, so the lower address is written second. */
    write_mem(addr, size, value, dst.mode == 4 && size == 4);
    if (dst.mode == 3) { /* (An)+: post-increment the destination */
      m_a[dst.reg] += (uint32_t)size;
    }
    flags_logic(value, size);
    prefetch_fill(1); /* write hides the trailing refill */
    return;
  }

  /* memory -> memory: interleaved access pairs (M68000UM pattern) */
  const uint32_t src_addr = ea_addr(src, size);
  if (aborted() || m_fault.kind == Fault::Kind::UnimplementedOpcode) {
    return;
  }
  const uint32_t dst_addr = ea_addr(dst, size);
  if (aborted() || m_fault.kind == Fault::Kind::UnimplementedOpcode) {
    return;
  }
  if (size == 4) {
    if (dst.mode == 4) {
      /* -(An) destination: both halves are read before the low word goes
       * out first, matching the descending write order. */
      const uint32_t value = read_mem(src_addr, 4);
      if (aborted()) {
        return;
      }
      write_mem(dst_addr, 4, value, true);
      flags_logic(value, 4);
    } else {
      const uint32_t hi = read_mem(src_addr, 2);
      if (aborted()) {
        return;
      }
      write_mem(dst_addr, 2, hi);
      if (aborted()) {
        return;
      }
      const uint32_t lo = read_mem(src_addr + 2, 2);
      if (aborted()) {
        return;
      }
      write_mem(dst_addr + 2, 2, lo);
      flags_logic((hi << 16) | lo, 4);
    }
  } else {
    const uint32_t v = read_mem(src_addr, size);
    if (aborted()) {
      return;
    }
    write_mem(dst_addr, size, v);
    flags_logic(v, size);
  }
  if (src.mode == 3) {
    m_a[src.reg] += (uint32_t)size;
  }
  if (dst.mode == 3) {
    m_a[dst.reg] += (uint32_t)size;
  }
  prefetch_fill(2);
}

void M68k::exec_movea(uint16_t op)
{
  /* MOVEA: 0x004x word (sign-extended), 0x008x long */
  const int size = (op & 0x1000) == 0x1000 ? 2 : 4;
  const Ea src{(op >> 3) & 7, op & 7};
  uint32_t value = ea_read(src, size);
  if (aborted() || m_fault.kind == Fault::Kind::UnimplementedOpcode) {
    return;
  }
  if (size == 2) {
    value = (uint32_t)(int32_t)(int16_t)value;
  }
  m_a[(op >> 9) & 7] = value;
  prefetch_fill(1);
}

void M68k::exec_moveq(uint16_t op)
{
  const uint32_t value = (uint32_t)(int32_t)(int8_t)(op & 0xFF);
  m_d[(op >> 9) & 7] = value;
  flags_logic(value, 4);
  prefetch_fill(1);
}

void M68k::exec_lea(uint16_t op)
{
  const Ea ea{(op >> 3) & 7, op & 7};
  const uint32_t addr = ea_addr(ea, 0);
  if (m_fault.kind == Fault::Kind::UnimplementedOpcode || aborted()) {
    return;
  }
  m_a[(op >> 9) & 7] = addr;
  prefetch_fill(1);
}

/* ------------------------------------------------------------------ */
/* dyadic ALU                                                          */
/* ------------------------------------------------------------------ */

void M68k::exec_alu(uint16_t op, int op_kind)
{
  /* op_kind: 0 OR, 1 SUB, 2 CMP/EOR, 3 AND, 4 ADD */
  const int opmode = (op >> 6) & 7;
  const int size = decode_size(opmode & 3);
  const Ea ea{(op >> 3) & 7, op & 7};
  const int reg = (op >> 9) & 7;

  /* Line 8: DIVU/DIVS; line C: MULU/MULS and EXG occupy opmode 3/7/5. */
  if (op_kind == 0 && ((opmode & 3) == 3)) {
    exec_div(op, (op & 0x0100) != 0);
    return;
  }
  if (op_kind == 3 && ((opmode & 3) == 3)) {
    exec_mul(op, (op & 0x0100) != 0);
    return;
  }
  /* EXG's selector is the five bits 7-3: 01000 Dx,Dy, 01001 Ax,Ay and
   * 10001 Dx,Ay. The last one lands on a different opmode, and without it
   * the instruction falls through to the memory path and resolves an
   * address register as if it were an address. */
  if (op_kind == 3 && (op & 0x0100) != 0 &&
      ((opmode == 5 && ea.mode <= 1) || (opmode == 6 && ea.mode == 1))) {
    exec_exg(op);
    return;
  }

  /* <op>A forms (opmode 3/7) */
  if ((opmode & 3) == 3) {
    const int asize = opmode == 7 ? 4 : 2;
    uint32_t value = ea_read(ea, asize);
    if (aborted() || m_fault.kind == Fault::Kind::UnimplementedOpcode) {
      return;
    }
    if (asize == 2) {
      value = (uint32_t)(int32_t)(int16_t)value;
    }
    if (op_kind == 1) {
      m_a[reg] -= value;
    } else if (op_kind == 4) {
      m_a[reg] += value;
    } else if (op_kind == 2) {
      flags_cmp(m_a[reg], value, m_a[reg] - value, 4);
    } else {
      m_fault = Fault{Fault::Kind::UnimplementedOpcode, m_pc, op};
      return;
    }
    prefetch_fill(1);
    return;
  }

  const bool to_memory = (op & 0x0100) != 0;

  /* ADDX/SUBX occupy bit-8-set mode 0/1 entirely: reg-to-reg OR/AND never
   * assemble there (their Dn,Dm form uses bit 8 clear), and mode 1 is the
   * -(Ay),-(Ax) pair — the 68000 has no postincrement X form. */
  if (to_memory && ea.mode <= 1 && (op_kind == 1 || op_kind == 4)) {
    exec_x_op(op, op_kind == 4);
    return;
  }
  /* ABCD/SBCD: byte size, mode 0/1, bit 8 set (0x8100-0x810F / 0xC100-
   * 0xC10F). OR/AND reg-to-reg at byte size with bit 8 set do not exist as
   * encodings, so recognizing the decimal pair here steals nothing. */
  if (to_memory && ea.mode <= 1 && size == 1 &&
      (op_kind == 0 || op_kind == 3)) {
    exec_bcd(op, op_kind == 3);
    return;
  }

  if (!to_memory) {
    /* <ea>,Dn — An is a legal source EA (the register's low word for
     * .B/.W); it is only invalid as a data destination. */
    const uint32_t src = ea_read(ea, size);
    if (aborted() || m_fault.kind == Fault::Kind::UnimplementedOpcode) {
      return;
    }
    const uint32_t dst = m_d[reg];
    uint32_t res = 0;
    bool writes = true;
    switch (op_kind) {
    case 0:
      res = dst | src;
      flags_logic(res, size);
      break;
    case 3:
      res = dst & src;
      flags_logic(res, size);
      break;
    case 1:
      res = dst - src;
      flags_sub(dst, src, res, size);
      break;
    case 4:
      res = dst + src;
      flags_add(dst, src, res, size);
      break;
    case 2:
      res = dst - src;
      flags_cmp(dst, src, res, size);
      writes = false;
      break;
    }
    if (writes) {
      m_d[reg] = size == 1   ? (m_d[reg] & ~0xFFu) | (res & 0xFF)
                 : size == 2 ? (m_d[reg] & ~0xFFFFu) | (res & 0xFFFF)
                             : res;
    }
    if (!ea_is_memory(ea)) {
      advance_clk(size == 4 ? 4 : 0); /* .L register op: 8 total */
      prefetch_fill(1);
    } else {
      /* mem->reg: read + one refill (8; .L 12) */
      prefetch_fill(1);
    }
    return;
  }

  /* bit 8 = 1: Dn,<ea> — or the reg,reg X forms */
  if (ea.mode == 0) {
    const uint32_t src = m_d[reg];
    const uint32_t dst = m_d[ea.reg];
    uint32_t res;
    switch (op_kind) {
    case 0:
      res = dst | src;
      flags_logic(res, size);
      break;
    case 3:
      res = dst & src;
      flags_logic(res, size);
      break;
    case 2:
      res = dst ^ src;
      flags_logic(res, size);
      break;
    default:
      m_fault = Fault{Fault::Kind::UnimplementedOpcode, m_pc, op};
      return;
    }
    m_d[ea.reg] = size == 1   ? (m_d[ea.reg] & ~0xFFu) | (res & 0xFF)
                  : size == 2 ? (m_d[ea.reg] & ~0xFFFFu) | (res & 0xFFFF)
                              : res;
    advance_clk(size == 4 ? 4 : 0);
    prefetch_fill(1);
    return;
  }
  if (ea.mode == 1 && op_kind == 2) {
    /* CMPM (0xB1x1): (Ay)+,(Ax)+ — CMP family only */
    const uint32_t src = read_mem(m_a[op & 7], size);
    if (aborted()) {
      return;
    }
    m_a[op & 7] += (uint32_t)size;
    const uint32_t dst = read_mem(m_a[reg], size);
    if (aborted()) {
      return;
    }
    m_a[reg] += (uint32_t)size;
    flags_cmp(dst, src, dst - src, size);
    prefetch_fill(1);
    return;
  }

  /* memory read-modify-write */
  const uint32_t src = m_d[reg];
  const uint32_t addr = ea_addr(ea, size);
  if (m_fault.kind == Fault::Kind::UnimplementedOpcode || aborted()) {
    return;
  }
  uint32_t dst = read_mem(addr, size);
  if (aborted()) {
    return;
  }
  uint32_t res;
  switch (op_kind) {
  case 0:
    res = dst | src;
    flags_logic(res, size);
    break;
  case 3:
    res = dst & src;
    flags_logic(res, size);
    break;
  case 2:
    res = dst ^ src;
    flags_logic(res, size);
    break;
  case 1:
    res = dst - src;
    flags_sub(dst, src, res, size);
    break;
  case 4:
    res = dst + src;
    flags_add(dst, src, res, size);
    break;
  default:
    m_fault = Fault{Fault::Kind::UnimplementedOpcode, m_pc, op};
    return;
  }
  write_mem(addr, size, res, size == 4);
  if (ea.mode == 3) {
    m_a[ea.reg] += (uint32_t)size;
  }
  prefetch_fill(1);
}

/* ------------------------------------------------------------------ */
/* extended (X) and decimal (BCD) arithmetic                           */
/* ------------------------------------------------------------------ */

void M68k::exec_x_op(uint16_t op, bool is_add)
{
  /* ADDX/SUBX: 1101/1001 xxx 1 ss 00m rrr. m=0 is Dy,Dx; m=1 is
   * -(Ay),-(Ax) only — both registers count down by the operand size.
   * Z clears on a non-zero result and otherwise holds its previous value,
   * so a multi-precision loop can test it once after the whole chain. */
  const int size = decode_size(op >> 6);
  const int rx = (op >> 9) & 7; /* Dx / -(Ax): destination */
  const int ry = op & 7;        /* Dy / -(Ay): source */
  const uint32_t x = (m_sr & SR_X) != 0 ? 1 : 0;
  const bool was_zero = (m_sr & SR_Z) != 0;

  if (((op >> 3) & 7) == 0) {
    const uint32_t src = m_d[ry];
    const uint32_t dst = m_d[rx];
    const uint32_t res = is_add ? dst + src + x : dst - src - x;
    if (is_add) {
      flags_add(dst, src, res, size, x);
    } else {
      flags_sub(dst, src, res, size, x);
    }
    m_d[rx] = size == 1   ? (m_d[rx] & ~0xFFu) | (res & 0xFF)
              : size == 2 ? (m_d[rx] & ~0xFFFFu) | (res & 0xFFFF)
                          : res;
    if ((res & bmask(size)) != 0 || !was_zero) {
      m_sr = (uint16_t)(m_sr & ~SR_Z);
    }
    advance_clk(size == 4 ? 4 : 0); /* 4/8 total with the refill; unverified */
    prefetch_fill(1);
    return;
  }

  /* -(Ay),-(Ax): fetch source, fetch destination, write back low word
   * first like every read-modify-write. unverified */
  m_a[ry] -= (uint32_t)size;
  const uint32_t src = read_mem(m_a[ry], size);
  if (aborted()) {
    return;
  }
  m_a[rx] -= (uint32_t)size;
  const uint32_t dst = read_mem(m_a[rx], size);
  if (aborted()) {
    return;
  }
  const uint32_t res = is_add ? dst + src + x : dst - src - x;
  if (is_add) {
    flags_add(dst, src, res, size, x);
  } else {
    flags_sub(dst, src, res, size, x);
  }
  if ((res & bmask(size)) != 0 || !was_zero) {
    m_sr = (uint16_t)(m_sr & ~SR_Z);
  }
  write_mem(m_a[rx], size, res, size == 4);
  advance_clk(2);
  prefetch_fill(1);
}

void M68k::exec_bcd(uint16_t op, bool is_add)
{
  /* ABCD/SBCD: 1100/1000 xxx 1 0000 R/M rrr — byte only. Decimal carry
   * always writes C and X together; N and V are undefined on the 68000
   * and are left alone. Z follows the X-family hold-on-zero rule. */
  const int rx = (op >> 9) & 7;
  const int ry = op & 7;
  const bool was_zero = (m_sr & SR_Z) != 0;
  const bool x = (m_sr & SR_X) != 0;

  uint32_t dst, res;
  bool carry = false;
  if (((op >> 3) & 7) == 0) {
    dst = m_d[rx] & 0xFF;
    res = bcd_adjust(dst, m_d[ry] & 0xFF, is_add, x, carry);
    m_d[rx] = (m_d[rx] & ~0xFFu) | res;
    advance_clk(2); /* 6 total with the refill */
    prefetch_fill(1);
  } else {
    /* -(Ay),-(Ax): one byte each way, predecrement by one. unverified */
    m_a[ry] -= 1;
    const uint32_t src = bus_read_byte(m_a[ry]);
    if (aborted()) {
      return;
    }
    m_a[rx] -= 1;
    dst = bus_read_byte(m_a[rx]);
    if (aborted()) {
      return;
    }
    res = bcd_adjust(dst, src, is_add, x, carry);
    bus_write_byte(m_a[rx], (uint8_t)res);
    advance_clk(2);
    prefetch_fill(1);
  }
  m_sr =
      (uint16_t)((m_sr & ~(SR_C | SR_X | SR_Z)) | (carry ? (SR_C | SR_X) : 0u) |
                 (res == 0 && was_zero ? SR_Z : 0u));
}

void M68k::exec_nbcd(uint16_t op)
{
  /* NBCD <ea>: 0100 1000 000 mmm rrr — zero minus the operand in decimal,
   * with X as the borrow-in. Same flag rules as SBCD. */
  const Ea ea{(op >> 3) & 7, op & 7};
  const bool was_zero = (m_sr & SR_Z) != 0;
  const bool x = (m_sr & SR_X) != 0;

  uint32_t res;
  bool carry = false;
  if (!ea_is_memory(ea)) {
    res = bcd_adjust(0, m_d[ea.reg] & 0xFF, false, x, carry);
    m_d[ea.reg] = (m_d[ea.reg] & ~0xFFu) | res;
    advance_clk(2); /* 6 total with the refill */
    prefetch_fill(1);
  } else {
    const uint32_t addr = ea_addr(ea, 1);
    if (m_fault.kind == Fault::Kind::UnimplementedOpcode || aborted()) {
      return;
    }
    const uint32_t dst = bus_read_byte(addr);
    if (aborted()) {
      return;
    }
    res = bcd_adjust(0, dst, false, x, carry);
    bus_write_byte(addr, (uint8_t)res);
    if (ea.mode == 3) { /* (An)+ */
      m_a[ea.reg] += 1;
    }
    prefetch_fill(1);
  }
  m_sr =
      (uint16_t)((m_sr & ~(SR_C | SR_X | SR_Z)) | (carry ? (SR_C | SR_X) : 0u) |
                 (res == 0 && was_zero ? SR_Z : 0u));
}

void M68k::exec_negx(uint16_t op)
{
  /* NEGX <ea>: 0100 0000 ss mmm EEE — zero minus the destination minus X,
   * with SUBX's Z rule. Same structure and timing as NEG. */
  const int size = decode_size(op >> 6);
  const Ea ea{(op >> 3) & 7, op & 7};
  const uint32_t x = (m_sr & SR_X) != 0 ? 1 : 0;
  const bool was_zero = (m_sr & SR_Z) != 0;

  if (!ea_is_memory(ea)) {
    const uint32_t dst = m_d[ea.reg];
    const uint32_t res = 0 - dst - x;
    flags_sub(0, dst, res, size, x);
    if ((res & bmask(size)) != 0 || !was_zero) {
      m_sr = (uint16_t)(m_sr & ~SR_Z);
    }
    m_d[ea.reg] = size == 1   ? (m_d[ea.reg] & ~0xFFu) | (res & 0xFF)
                  : size == 2 ? (m_d[ea.reg] & ~0xFFFFu) | (res & 0xFFFF)
                              : res;
    advance_clk(size == 4 ? 4 : 0);
    prefetch_fill(1);
    return;
  }

  const uint32_t addr = ea_addr(ea, size);
  if (m_fault.kind == Fault::Kind::UnimplementedOpcode || aborted()) {
    return;
  }
  const uint32_t dst = read_mem(addr, size);
  if (aborted()) {
    return;
  }
  const uint32_t res = 0 - dst - x;
  flags_sub(0, dst, res, size, x);
  if ((res & bmask(size)) != 0 || !was_zero) {
    m_sr = (uint16_t)(m_sr & ~SR_Z);
  }
  write_mem(addr, size, res, size == 4);
  if (ea.mode == 3) { /* (An)+ */
    m_a[ea.reg] += (uint32_t)size;
  }
  prefetch_fill(1);
}

void M68k::exec_chk(uint16_t op)
{
  /* CHK <ea>,Dn (word only): traps through vector 6 when the source is
   * negative or above the unsigned limit in Dn's low word; the stacked
   * PC is the address after CHK. C always clears; N, V and Z are
   * undefined and left alone. */
  constexpr int kVecChk = 6;
  const Ea ea{(op >> 3) & 7, op & 7};
  const uint32_t src = ea_read(ea, 2);
  if (aborted() || m_fault.kind == Fault::Kind::UnimplementedOpcode) {
    return;
  }
  const int32_t value = (int32_t)(int16_t)src;
  const uint32_t limit = m_d[(op >> 9) & 7] & 0xFFFF;
  m_sr = (uint16_t)(m_sr & ~SR_C);
  if (value < 0 || (uint32_t)value > limit) {
    exception(kVecChk, m_pc, 6);
    return;
  }
  advance_clk(6); /* 10 total with the refill; unverified */
  prefetch_fill(1);
}

/* ------------------------------------------------------------------ */
/* immediates                                                          */
/* ------------------------------------------------------------------ */

void M68k::exec_immediate(uint16_t op)
{
  /* line field (op>>9)&7: 0 ORI, 1 ANDI, 2 SUBI, 3 ADDI, 5 EORI, 6 CMPI */
  const int line_kind = (op >> 9) & 7;
  const int size = decode_size(op >> 6);
  const Ea ea{(op >> 3) & 7, op & 7};

  /* immediate to CCR/SR: fixed patterns 0x..3C (CCR) and 0x..7C (SR) */
  if ((op & 0x00FF) == 0x003C) {
    const uint16_t imm = fetch_stream_word();
    m_sr = (uint16_t)((m_sr & ~0x1F) | (imm & 0x1F));
    prefetch_fill(1);
    return;
  }
  if ((op & 0x00FF) == 0x007C) {
    if (line_kind != 0 && line_kind != 1 && line_kind != 5) {
      m_fault = Fault{Fault::Kind::UnimplementedOpcode, m_pc, op};
      return;
    }
    if ((m_sr & SR_S) == 0) {
      exception(kVecIllegal, m_pc, 4);
      return;
    }
    const uint16_t imm = fetch_stream_word();
    switch (line_kind) {
    case 0: /* ORI */
      m_sr |= imm;
      break;
    case 1: /* ANDI */
      m_sr &= imm;
      break;
    case 5: /* EORI */
      m_sr ^= imm;
      break;
    default:
      break;
    }
    advance_clk(4);
    prefetch_fill(1);
    return;
  }

  uint32_t imm;
  if (size == 1) {
    imm = (uint8_t)fetch_stream_word();
  } else if (size == 2) {
    imm = fetch_stream_word();
  } else {
    const uint16_t hi = fetch_stream_word();
    const uint16_t lo = fetch_stream_word();
    imm = ((uint32_t)hi << 16) | lo;
  }
  if (aborted()) {
    return;
  }

  if (ea.mode == 0) {
    const uint32_t dst = m_d[ea.reg];
    uint32_t res = dst;
    bool writes = true;
    switch (line_kind) {
    case 0:
      res = dst | imm;
      flags_logic(res, size);
      break;
    case 1:
      res = dst & imm;
      flags_logic(res, size);
      break;
    case 2:
      res = dst - imm;
      flags_sub(dst, imm, res, size);
      break;
    case 3:
      res = dst + imm;
      flags_add(dst, imm, res, size);
      break;
    case 5:
      res = dst ^ imm;
      flags_logic(res, size);
      break;
    case 6:
      res = dst - imm;
      flags_cmp(dst, imm, res, size);
      writes = false;
      break;
    default:
      m_fault = Fault{Fault::Kind::UnimplementedOpcode, m_pc, op};
      return;
    }
    if (writes) {
      m_d[ea.reg] = size == 1   ? (m_d[ea.reg] & ~0xFFu) | (res & 0xFF)
                    : size == 2 ? (m_d[ea.reg] & ~0xFFFFu) | (res & 0xFFFF)
                                : res;
    }
    advance_clk(size == 4 ? 12 : 6);
    prefetch_fill(1);
    return;
  }

  /* immediate to memory: read-modify-write. The EA is resolved once
   * (calling ea_addr through both ea_read and ea_write would consume
   * the extension word twice). */
  {
    const uint32_t addr = ea_addr(ea, size);
    if (m_fault.kind == Fault::Kind::UnimplementedOpcode || aborted()) {
      return;
    }
    const uint32_t dst = read_mem(addr, size);
    if (aborted()) {
      return;
    }
    uint32_t res = dst;
    bool writes = true;
    switch (line_kind) {
    case 0:
      res = dst | imm;
      flags_logic(res, size);
      break;
    case 1:
      res = dst & imm;
      flags_logic(res, size);
      break;
    case 2:
      res = dst - imm;
      flags_sub(dst, imm, res, size);
      break;
    case 3:
      res = dst + imm;
      flags_add(dst, imm, res, size);
      break;
    case 5:
      res = dst ^ imm;
      flags_logic(res, size);
      break;
    case 6:
      res = dst - imm;
      flags_cmp(dst, imm, res, size);
      writes = false;
      break;
    default:
      m_fault = Fault{Fault::Kind::UnimplementedOpcode, m_pc, op};
      return;
    }
    if (writes) {
      write_mem(addr, size, res, size == 4);
    }
    if (ea.mode == 3) { /* (An)+ post-increment */
      m_a[ea.reg] += (uint32_t)size;
    }
    prefetch_fill(1);
  }
}

/* ------------------------------------------------------------------ */
/* ADDQ/SUBQ, Scc, DBcc                                                */
/* ------------------------------------------------------------------ */

void M68k::exec_addq_subq(uint16_t op)
{
  const bool sub = (op & 0x0100) != 0;
  const int size = decode_size(op >> 6);
  const Ea ea{(op >> 3) & 7, op & 7};
  const uint32_t qty = (uint32_t)(((op >> 9) & 7) == 0 ? 8 : (op >> 9) & 7);

  if (ea.mode <= 1) {
    if (ea.mode == 0) {
      const uint32_t dst = m_d[ea.reg];
      const uint32_t res = sub ? dst - qty : dst + qty;
      if (sub) {
        flags_sub(dst, qty, res, size);
      } else {
        flags_add(dst, qty, res, size);
      }
      m_d[ea.reg] = size == 1   ? (m_d[ea.reg] & ~0xFFu) | (res & 0xFF)
                    : size == 2 ? (m_d[ea.reg] & ~0xFFFFu) | (res & 0xFFFF)
                                : res;
    } else {
      /* to An: full 32-bit, no flags */
      m_a[ea.reg] = sub ? m_a[ea.reg] - qty : m_a[ea.reg] + qty;
    }
    /* Dn: 4 clocks byte/word, 8 long. An: 8 clocks either size — the
     * address register write-back is always a full 32-bit operation. */
    advance_clk(ea.mode == 1 || size == 4 ? 4 : 0);
    prefetch_fill(1);
    return;
  }

  const uint32_t addr = ea_addr(ea, size);
  if (m_fault.kind == Fault::Kind::UnimplementedOpcode || aborted()) {
    return;
  }
  const uint32_t dst = read_mem(addr, size);
  if (aborted()) {
    return;
  }
  const uint32_t res = sub ? dst - qty : dst + qty;
  if (sub) {
    flags_sub(dst, qty, res, size);
  } else {
    flags_add(dst, qty, res, size);
  }
  write_mem(addr, size, res, size == 4);
  if (ea.mode == 3) {
    m_a[ea.reg] += (uint32_t)size;
  }
  prefetch_fill(1);
}

void M68k::exec_scc(uint16_t op)
{
  const Ea ea{(op >> 3) & 7, op & 7};
  const bool set = cond((op >> 8) & 0xF);

  if (ea.mode == 0) {
    m_d[ea.reg] = (m_d[ea.reg] & ~0xFFu) | (set ? 0xFF : 0x00);
    if (!set) {
      advance_clk(4);
    }
    prefetch_fill(1);
    return;
  }
  const uint32_t addr = ea_addr(ea, 1);
  if (m_fault.kind == Fault::Kind::UnimplementedOpcode || aborted()) {
    return;
  }
  if (!set) {
    /* false: read, then write zero (12 clocks) */
    (void)bus_read_byte(addr);
    if (aborted()) {
      return;
    }
    bus_write_byte(addr, 0);
  } else {
    bus_write_byte(addr, 0xFF);
  }
  if (ea.mode == 3) { /* (An)+ post-increment */
    m_a[ea.reg] += ea.reg == 7 ? 2U : 1U;
  }
  prefetch_fill(1);
}

void M68k::exec_dbcc(uint16_t op)
{
  const int reg = op & 7;
  const int16_t disp = (int16_t)fetch_stream_word();
  if (aborted()) {
    return;
  }
  if (cond((op >> 8) & 0xF)) {
    advance_clk(6); /* condition true: fall through; unverified */
    prefetch_fill(1);
    return;
  }
  const uint16_t count = (uint16_t)(m_d[reg] & 0xFFFF);
  const uint16_t next = (uint16_t)(count - 1);
  m_d[reg] = (m_d[reg] & ~0xFFFFu) | next;
  if (next != 0xFFFF) {
    m_pc = (m_pc + (uint32_t)(int32_t)disp) & 0xFFFFFF;
    advance_clk(2);
    flush_stream(m_pc);
    prefetch_fill(2);
    return;
  }
  advance_clk(2); /* loop exhausted; unverified */
  prefetch_fill(1);
}

/* ------------------------------------------------------------------ */
/* branches, JSR/JMP                                                   */
/* ------------------------------------------------------------------ */

void M68k::exec_branch(uint16_t op)
{
  const int cc = (op >> 8) & 0xF;
  const bool is_bsr = cc == 1;
  bool taken = is_bsr || cc == 0;
  if (!taken) {
    taken = cond(cc);
  }

  const int8_t disp8 = (int8_t)(op & 0xFF);
  const bool word_form = (op & 0xFF) == 0;
  uint32_t target = m_pc;

  if (word_form) {
    const int16_t disp16 = (int16_t)fetch_stream_word();
    if (aborted()) {
      return;
    }
    if (taken) {
      /* displacement is relative to the extension word's end */
      /* displacement relative to the extension word address */
      target = (m_pc + (uint32_t)(int32_t)disp16) & 0xFFFFFF;
    }
  } else if (taken) {
    target = (m_pc + (uint32_t)(int32_t)disp8) & 0xFFFFFF;
  }

  if (!taken) {
    /* not taken: 8 clocks for the short form, 12 for the word form (the
     * extension word is read and discarded). */
    advance_clk(4);
    prefetch_fill(1);
    return;
  }

  if (is_bsr) {
    /* Return address: the instruction after the BSR. m_pc still points at
     * the word following the opcode, so the short form returns there and
     * only the word form has to step over its displacement word. */
    const uint32_t return_pc = word_form ? m_pc + 2 : m_pc;
    uint32_t sp = m_a[7] - 4;
    bus_write_word(sp, (uint16_t)(return_pc >> 16));
    if (aborted()) {
      return;
    }
    bus_write_word(sp + 2, (uint16_t)return_pc);
    if (aborted()) {
      return;
    }
    m_a[7] = sp;
    advance_clk(word_form ? 2 : 0); /* BSR.W 18 / BSR.S 16; unverified */
  } else {
    advance_clk(word_form ? 4 : 2); /* BRA.W 12 / BRA.S 10 */
  }

  m_pc = target;
  flush_stream(m_pc);
  prefetch_fill(2);
}

void M68k::exec_jsr_jmp(uint16_t op)
{
  const bool jsr = (op & 0x0040) == 0; /* JSR 0x4E8x, JMP 0x4ECx */
  const Ea ea{(op >> 3) & 7, op & 7};
  const uint32_t target = ea_addr(ea, 0);
  if (m_fault.kind == Fault::Kind::UnimplementedOpcode || aborted()) {
    return;
  }
  if (jsr) {
    const uint32_t return_pc = m_prefetch_addr;
    uint32_t sp = m_a[7] - 4;
    bus_write_word(sp, (uint16_t)(return_pc >> 16));
    if (aborted()) {
      return;
    }
    bus_write_word(sp + 2, (uint16_t)return_pc);
    if (aborted()) {
      return;
    }
    m_a[7] = sp;
  }
  advance_clk(4); /* unverified */
  m_pc = target;
  flush_stream(m_pc);
  prefetch_fill(2);
}

/* ------------------------------------------------------------------ */
/* group 4                                                             */
/* ------------------------------------------------------------------ */

void M68k::exec_single(uint16_t op)
{
  const uint16_t sub = op & 0xFFFF;

  if (sub == 0x4E71) { /* NOP */
    prefetch_fill(1);
    return;
  }
  if (sub == 0x4E70) { /* RESET */
    advance_clk(124);
    prefetch_fill(1);
    return;
  }
  if (sub == 0x4E75) { /* RTS */
    const uint16_t pc_hi = bus_read_word(m_a[7]);
    if (aborted()) {
      return;
    }
    const uint16_t pc_lo = bus_read_word(m_a[7] + 2);
    if (aborted()) {
      return;
    }
    m_a[7] += 4;
    m_pc = ((uint32_t)pc_hi << 16) | pc_lo;
    advance_clk(4);
    flush_stream(m_pc);
    prefetch_fill(2);
    return;
  }
  if (sub == 0x4E73) { /* RTE */
    const uint16_t sr = bus_read_word(m_a[7]);
    if (aborted()) {
      return;
    }
    const uint16_t pc_hi = bus_read_word(m_a[7] + 2);
    if (aborted()) {
      return;
    }
    const uint16_t pc_lo = bus_read_word(m_a[7] + 4);
    if (aborted()) {
      return;
    }
    /* The 68000 RTE pops exactly the 6-byte interrupt frame (SR + PC);
     * the extended frames with format/vector words are a 68010+ feature.
     * Advancing by 10 ate 4 bytes of the interrupted code's stack —
     * Contra's VINT handler derail (bug 13). */
    m_a[7] += 6;
    if ((sr & SR_S) == 0) {
      switch_supervisor(false);
    }
    m_sr = sr;
    m_pc = ((uint32_t)pc_hi << 16) | pc_lo;
    flush_stream(m_pc);
    prefetch_fill(2);
    return;
  }
  if (sub == 0x4E77) { /* RTR */
    const uint16_t ccr = bus_read_word(m_a[7]);
    if (aborted()) {
      return;
    }
    const uint16_t pc_hi = bus_read_word(m_a[7] + 2);
    if (aborted()) {
      return;
    }
    const uint16_t pc_lo = bus_read_word(m_a[7] + 4);
    if (aborted()) {
      return;
    }
    m_a[7] += 6;
    m_sr = (uint16_t)((m_sr & ~0x1F) | (ccr & 0x1F));
    m_pc = ((uint32_t)pc_hi << 16) | pc_lo;
    flush_stream(m_pc);
    prefetch_fill(2);
    return;
  }
  if (sub == 0x4E72) { /* STOP #imm */
    if ((m_sr & SR_S) == 0) {
      exception(kVecIllegal, m_pc, 4);
      return;
    }
    const uint16_t imm = fetch_stream_word();
    if (aborted()) {
      return;
    }
    m_sr = imm;
    m_fault = Fault{Fault::Kind::Stopped, m_pc, op};
    return;
  }
  if (sub == 0x4E76) { /* TRAPV */
    if ((m_sr & SR_V) != 0) {
      exception(kVecTrap0 + 6, m_pc, 4);
    } else {
      prefetch_fill(1);
    }
    return;
  }
  if (sub == 0x4AFC) { /* ILLEGAL — checked before LEA, which shares bits 8-6 */
    exception(kVecIllegal, m_pc, 4);
    return;
  }
  if ((op & 0xFFF0) == 0x4E40) { /* TRAP #n */
    exception(kVecTrap0 + (op & 0xF), m_pc, 4);
    return;
  }
  if ((op & 0xFFF8) == 0x4E50 || (op & 0xFFF8) == 0x4E58) {
    exec_link_unlk(op);
    return;
  }
  if ((op & 0xFFC0) == 0x4840 && ((op >> 3) & 7) != 0) { /* PEA */
    exec_pea(op);
    return;
  }
  if (((op & 0xFF80) == 0x4880 || (op & 0xFF80) == 0x4C80) &&
      ((op >> 3) & 7) >= 2) { /* MOVEM (EXT owns mode 0) */
    exec_movem(op);
    return;
  }
  if ((op & 0xFFF8) == 0x4E60) { /* MOVE Ay,USP */
    if ((m_sr & SR_S) == 0) {
      exception(kVecIllegal, m_pc, 4);
      return;
    }
    m_usp = m_a[op & 7];
    prefetch_fill(1);
    return;
  }
  if ((op & 0xFFF8) == 0x4E68) { /* MOVE USP,Ax */
    if ((m_sr & SR_S) == 0) {
      exception(kVecIllegal, m_pc, 4);
      return;
    }
    m_a[op & 7] = m_usp;
    prefetch_fill(1);
    return;
  }
  if ((op & 0xFF80) == 0x4E80) { /* JSR 0x4E80-0x4EBF / JMP 0x4EC0+ */
    exec_jsr_jmp(op);
    return;
  }
  if ((op & 0x01C0) == 0x01C0 && (op & 0xF000) == 0x4000) { /* LEA */
    exec_lea(op);
    return;
  }
  if ((op & 0xFFF8) == 0x4840) { /* SWAP */
    const int r = op & 7;
    m_d[r] = (m_d[r] >> 16) | (m_d[r] << 16);
    flags_logic(m_d[r], 4);
    prefetch_fill(1);
    return;
  }
  if ((op & 0xFFB8) == 0x4880) { /* EXT.W / EXT.L */
    const int r = op & 7;
    if (op & 0x40) {
      m_d[r] = (uint32_t)(int32_t)(int16_t)(m_d[r] & 0xFFFF);
      set_zn(m_d[r], 4);
    } else {
      const uint16_t extended = (uint16_t)(int16_t)(int8_t)(m_d[r] & 0xFF);
      m_d[r] = (m_d[r] & 0xFFFF0000) | extended;
      set_zn(m_d[r], 2);
    }
    m_sr = (uint16_t)(m_sr & ~(SR_C | SR_V));
    prefetch_fill(1);
    return;
  }
  if ((op & 0xF000) == 0x4000 && (op & 0x01C0) == 0x00C0 &&
      (op & 0x0E00) < 0x0800) {
    /* MOVE from/to SR/CCR: 0x40C0 0x42C0 0x44C0 0x46C0 (bits 8-6 = 011,
       bits 11-9 = 0-3). The same bit-8-6 zone also holds MOVEM.L/EXT.L
       higher up, and NEG/NOT below — this check must precede them. */
    exec_move_sr(op);
    return;
  }
  if ((op & 0xFF00) == 0x4A00 && ((op >> 3) & 7) != 1) { /* TST */
    exec_clr_neg_not_tst(op);
    return;
  }
  if ((op & 0xFF00) == 0x4200 && ((op >> 3) & 7) != 1) { /* CLR */
    exec_clr_neg_not_tst(op);
    return;
  }
  if ((op & 0xFF00) == 0x4400 && ((op >> 3) & 7) != 1) { /* NEG */
    exec_clr_neg_not_tst(op);
    return;
  }
  if ((op & 0xFF00) == 0x4600 && ((op >> 3) & 7) != 1) { /* NOT */
    exec_clr_neg_not_tst(op);
    return;
  }
  if ((op & 0xFFC0) == 0x4AC0) { /* TAS */
    exec_clr_neg_not_tst(op);
    return;
  }
  if ((op & 0xF1C0) == 0x4180 && ((op >> 3) & 7) != 1) { /* CHK <ea>,Dn */
    exec_chk(op);
    return;
  }
  /* NEGX 0x4000-0x40BF; 0x40C0 upward is MOVE from SR (handled above). */
  if ((op & 0xFF00) == 0x4000 && ((op >> 3) & 7) != 1 &&
      (op & 0x00C0) != 0x00C0) {
    exec_negx(op);
    return;
  }
  if ((op & 0xFFC0) == 0x4800 && ((op >> 3) & 7) != 1) { /* NBCD */
    exec_nbcd(op);
    return;
  }

  m_fault = Fault{Fault::Kind::UnimplementedOpcode, m_pc, op};
}

void M68k::exec_clr_neg_not_tst(uint16_t op)
{
  const int size = decode_size(op >> 6);
  const Ea ea{(op >> 3) & 7, op & 7};
  const uint16_t kind = op & 0xFF00;

  if (kind == 0x4A00) { /* TST */
    const uint32_t v = ea_read(ea, size);
    if (aborted() || m_fault.kind == Fault::Kind::UnimplementedOpcode) {
      return;
    }
    flags_logic(v, size);
    prefetch_fill(ea_is_memory(ea) ? 1 : 1);
    return;
  }
  if (kind == 0x4200) { /* CLR */
    if (!ea_is_memory(ea)) {
      ea_write(ea, size, 0);
    } else {
      const uint32_t addr = ea_addr(ea, size);
      if (m_fault.kind == Fault::Kind::UnimplementedOpcode || aborted()) {
        return;
      }
      /* The 68000 performs a read cycle before writing zero. Besides
       * preserving bus-visible behaviour, this makes CLR.L memory forms
       * eight clocks slower than a plain long write; the read/write
       * pairs below cover those, and the +4 is the dead cycle every
       * read-modify-write instruction carries (published CLR.L (An)+
       * timing is 20 clocks). */
      (void)read_mem(addr, size);
      if (aborted()) {
        return;
      }
      write_mem(addr, size, 0, size == 4);
      if (ea.mode == 3) {
        m_a[ea.reg] += (uint32_t)size;
      }
    }
    m_sr = (uint16_t)(m_sr & ~(SR_C | SR_V | SR_N));
    m_sr |= SR_Z;
    prefetch_fill(1);
    return;
  }
  if ((op & 0xFFC0) == 0x4AC0) { /* TAS: MD never writes back (BROKEN_TAS) */
    uint32_t v;
    if (ea.mode == 0) {
      v = m_d[ea.reg] & 0xFF;
      m_d[ea.reg] |= 0x80;
    } else {
      const uint32_t addr = ea_addr(ea, 1);
      if (m_fault.kind == Fault::Kind::UnimplementedOpcode || aborted()) {
        return;
      }
      v = bus_read_byte(addr);
      if (aborted()) {
        return;
      }
      advance_clk(4); /* the write-back cycle the MD never performs */
    }
    set_zn(v, 1);
    m_sr = (uint16_t)(m_sr & ~(SR_C | SR_V));
    prefetch_fill(1);
    return;
  }

  uint32_t addr = 0;
  uint32_t dst = 0;
  if (ea_is_memory(ea)) {
    /* NEG and NOT are read-modify-write operations: their effective
     * address is calculated once, then used for both bus phases. Resolving
     * it again would consume displacement/index extension words twice and
     * update predecrement/postincrement address registers twice. */
    addr = ea_addr(ea, size);
    if (aborted() || m_fault.kind == Fault::Kind::UnimplementedOpcode) {
      return;
    }
    dst = read_mem(addr, size);
  } else {
    dst = ea_read(ea, size);
  }
  if (aborted() || m_fault.kind == Fault::Kind::UnimplementedOpcode) {
    return;
  }
  uint32_t res = 0;
  if (kind == 0x4400) { /* NEG */
    res = 0 - dst;
    flags_sub(0, dst, res, size);
  } else { /* NOT */
    res = ~dst;
    flags_logic(res, size);
  }
  if (ea_is_memory(ea)) {
    write_mem(addr, size, res, size == 4 && ea.mode == 4);
    if (ea.mode == 3) {
      m_a[ea.reg] += (uint32_t)size;
    }
  } else {
    ea_write(ea, size, res);
  }
  prefetch_fill(1);
}

/* ------------------------------------------------------------------ */
/* shifts / rotates                                                    */
/* ------------------------------------------------------------------ */

uint32_t M68k::shift_once(uint32_t v, int type, bool left, int size)
{
  /* type: 0 AS, 1 LS, 2 ROX, 3 RO (def68k.def bits) */
  const uint32_t m = bmask(size);
  const int shift = size * 8 - 1;
  uint32_t res = v & m;

  if (left) {
    const uint32_t top = (res >> shift) & 1;
    switch (type) {
    case 0:
      m_sr = (uint16_t)((m_sr & ~(SR_X | SR_C)) | (top ? (SR_X | SR_C) : 0));
      if (top != ((res >> (shift - 1)) & 1)) {
        m_sr |= SR_V;
      }
      res = (res << 1) & m;
      break;
    case 1:
      m_sr = (uint16_t)((m_sr & ~(SR_X | SR_C)) | (top ? (SR_X | SR_C) : 0));
      res = (res << 1) & m;
      break;
    case 2: {
      const uint32_t xin = (m_sr & SR_X) ? 1u : 0u;
      m_sr = (uint16_t)((m_sr & ~(SR_X | SR_C)) | (top ? (SR_X | SR_C) : 0));
      res = ((res << 1) & m) | xin;
      break;
    }
    case 3:
      m_sr = (uint16_t)((m_sr & ~SR_C) | (top ? SR_C : 0));
      res = ((res << 1) & m) | top;
      break;
    }
  } else {
    const uint32_t bit = res & 1;
    switch (type) {
    case 0:
      m_sr = (uint16_t)((m_sr & ~(SR_X | SR_C)) | (bit ? (SR_X | SR_C) : 0));
      res = (res >> 1) | (res & (1u << shift));
      break;
    case 1:
      m_sr = (uint16_t)((m_sr & ~(SR_X | SR_C)) | (bit ? (SR_X | SR_C) : 0));
      res >>= 1;
      break;
    case 2: {
      const uint32_t xin = (m_sr & SR_X) ? (1u << shift) : 0u;
      m_sr = (uint16_t)((m_sr & ~(SR_X | SR_C)) | (bit ? (SR_X | SR_C) : 0));
      res = (res >> 1) | xin;
      break;
    }
    case 3:
      m_sr = (uint16_t)((m_sr & ~SR_C) | (bit ? SR_C : 0));
      res = (res >> 1) | (bit ? (1u << shift) : 0u);
      break;
    }
  }
  return res;
}

void M68k::exec_shift(uint16_t op)
{
  /* Register: 1110 [count/reg:3] [dir:1] [size:2] [form:1] [type:2] [reg:3]
   * Memory:  1110 [type:3]        [dir:1] 11 mmm rrr (word, one bit) */
  /* The size field picks the form: 11 is the one-bit memory shift, any
   * other value is a register shift. Bits 5-3 cannot decide it — in the
   * register form they carry the i/r flag and the type, so reading them
   * as an EA mode sends every register-count shift, and the immediate
   * ROX/RO forms, down the memory path. */
  if (((op >> 6) & 3) == 3) {
    /* memory form: type at bits 10-9, direction at bit 8 */
    const int type = (op >> 9) & 3; /* 0 AS, 1 LS, 2 ROX, 3 RO */
    const bool left = (op & 0x0100) != 0;
    const Ea ea{(op >> 3) & 7, op & 7};
    const uint32_t addr = ea_addr(ea, 2);
    if (m_fault.kind == Fault::Kind::UnimplementedOpcode || aborted()) {
      return;
    }
    const uint16_t v = bus_read_word(addr);
    if (aborted()) {
      return;
    }
    m_sr = (uint16_t)(m_sr & ~SR_V);
    const uint32_t res = shift_once(v, type, left, 2);
    set_zn(res, 2);
    if (type != 0 && type != 3) {
      m_sr = (uint16_t)(m_sr & ~SR_V);
    }
    bus_write_word(addr, (uint16_t)res);
    prefetch_fill(1);
    return;
  }

  const bool left = (op & 0x0100) != 0;
  const bool reg_count = (op & 0x0020) != 0;
  const int type = (op >> 3) & 3;
  const int r = op & 7;
  const int size = decode_size(op >> 6);

  int count;
  if (reg_count) {
    count = m_d[(op >> 9) & 7] & 0x3F;
  } else {
    count = ((op >> 9) & 7);
    if (count == 0) {
      count = 8;
    }
  }
  if (count == 0) {
    set_zn(m_d[r], size);
    m_sr = (uint16_t)(m_sr & ~(SR_C | SR_V));
    prefetch_fill(1);
    return;
  }

  uint32_t res = m_d[r] & bmask(size);
  /* Every shift/rotate starts with V clear. ASL sets it if any step changes
   * the sign bit; the other seven forms always leave it clear. */
  m_sr = (uint16_t)(m_sr & ~SR_V);
  for (int i = 0; i < count; i++) {
    res = shift_once(res, type, left, size);
  }
  set_zn(res, size);
  if (type == 3 || type == 1) {
    m_sr = (uint16_t)(m_sr & ~SR_V);
  } else if (type == 2) {
    m_sr = (uint16_t)(m_sr & ~SR_V);
  }
  m_d[r] = size == 1   ? (m_d[r] & ~0xFFu) | (res & 0xFF)
           : size == 2 ? (m_d[r] & ~0xFFFFu) | (res & 0xFFFF)
                       : res;
  /* 1-bit immediate = 6 clocks; n-bit = 6 + 2(n-1); register = 6 + 2n
   * (charged as internal + trailing refill). unverified. */
  advance_clk(reg_count ? 2 * count : 2 * (count - 1));
  prefetch_fill(1);
}

/* ------------------------------------------------------------------ */
/* bit operations                                                      */
/* ------------------------------------------------------------------ */

void M68k::exec_bitop(uint16_t op)
{
  /* Dynamic: 0000 rrr 1 tt mmm rrr (0x01xx); static: 0000 1000 tt mmm rrr
   * (0x08xx). kind from bits 7-6: 0 BTST, 1 BCHG, 2 BCLR, 3 BSET. */
  const int kind = (op >> 6) & 3;
  const bool dynamic = (op & 0x0100) != 0;
  const Ea ea{(op >> 3) & 7, op & 7};

  /* The static form's extension word carries the bit number in its low
   * byte (bits 15-8 read as zero), so it needs no shift. */
  uint32_t bitnum = 0;
  if (dynamic) {
    bitnum = m_d[(op >> 9) & 7] & 0x3F;
  } else {
    bitnum = fetch_stream_word() & 0xFF;
  }
  if (aborted()) {
    return;
  }

  if (ea.mode == 0) {
    const uint32_t mask = 1u << (bitnum & 31);
    const bool bit_set = (m_d[ea.reg] & mask) != 0;
    /* Z <- NOT(bit): set when the tested bit is clear */
    m_sr = (uint16_t)((m_sr & ~SR_Z) | (bit_set ? 0 : SR_Z));
    switch (kind) {
    case 1:
      m_d[ea.reg] ^= mask;
      break;
    case 2:
      m_d[ea.reg] &= ~mask;
      break;
    case 3:
      m_d[ea.reg] |= mask;
      break;
    default:
      break;
    }
    if (kind != 0) {
      advance_clk(2);
    }
    prefetch_fill(1);
    return;
  }

  /* BTST Dn,#imm: the read-only bit op alone may take an immediate EA
   * (the def68k tables single it out); the mutating kinds keep the
   * illegal-EA fault. */
  if (kind == 0 && ea.mode == 7 && ea.reg == 4) {
    const uint32_t v = fetch_stream_word() & 0xFF;
    if (aborted()) {
      return;
    }
    m_sr = (uint16_t)((m_sr & ~SR_Z) |
                      ((v & (1u << (bitnum & 7))) != 0 ? 0 : SR_Z));
    prefetch_fill(1);
    return;
  }

  /* memory forms: byte, bit 0-7 */
  const uint32_t addr = ea_addr(ea, 1);
  if (m_fault.kind == Fault::Kind::UnimplementedOpcode || aborted()) {
    return;
  }
  const uint8_t v = bus_read_byte(addr);
  if (aborted()) {
    return;
  }
  const uint8_t mask = (uint8_t)(1u << (bitnum & 7));
  const bool bit_set = (v & mask) != 0;
  /* Z <- NOT(bit): set when the tested bit is clear */
  m_sr = (uint16_t)((m_sr & ~SR_Z) | (bit_set ? 0 : SR_Z));
  if (kind == 0) {
    prefetch_fill(2);
    return;
  }
  uint8_t res = v;
  switch (kind) {
  case 1:
    res = v ^ mask;
    break;
  case 2:
    res = v & (uint8_t)~mask;
    break;
  case 3:
    res = v | mask;
    break;
  }
  bus_write_byte(addr, res);
  prefetch_fill(1);
}

/* ------------------------------------------------------------------ */
/* MOVE SR/CCR                                                         */
/* ------------------------------------------------------------------ */

void M68k::exec_move_sr(uint16_t op)
{
  /* 0x40C0 from SR, 0x42C0 from CCR, 0x44C0 to CCR, 0x46C0 to SR */
  const int form = (op >> 9) & 3;
  const Ea ea{(op >> 3) & 7, op & 7};

  if (form == 0 || form == 1) {
    const uint16_t value = form == 0 ? m_sr : (uint16_t)(m_sr & 0x1F);
    if (ea.mode == 0) {
      m_d[ea.reg] = (m_d[ea.reg] & ~0xFFFFu) | value;
      prefetch_fill(1);
      return;
    }
    const uint32_t addr = ea_addr(ea, 2);
    if (m_fault.kind == Fault::Kind::UnimplementedOpcode || aborted()) {
      return;
    }
    bus_write_word(addr, value);
    prefetch_fill(1);
    return;
  }
  if (form == 2) { /* to CCR */
    const uint32_t v = ea_read(ea, 2);
    if (aborted() || m_fault.kind == Fault::Kind::UnimplementedOpcode) {
      return;
    }
    m_sr = (uint16_t)((m_sr & ~0x1F) | (v & 0x1F));
    prefetch_fill(1);
    return;
  }
  /* to SR */
  if ((m_sr & SR_S) == 0) {
    exception(kVecIllegal, m_pc, 4);
    return;
  }
  const uint32_t v = ea_read(ea, 2);
  if (aborted() || m_fault.kind == Fault::Kind::UnimplementedOpcode) {
    return;
  }
  const bool new_s = (v & SR_S) != 0;
  switch_supervisor(new_s);
  m_sr = (uint16_t)v;
  advance_clk(4);
  prefetch_fill(1);
}


void M68k::exec_movem(uint16_t op)
{
  /* MOVEM: 0100 1100/1000 1s mmm EEE with a register-list mask word.
   * reg->mem 0x4880 (.W) / 0x48C0 (.L); mem->reg 0x4C80 / 0x4CC0.
   * -(An) transfers registers in descending order (A7 first); (An)+ and
   * control modes in ascending order. .W loads sign-extend. Cycles are
   * charged per transferred word via the bus engine plus internal
   * cycles; unverified. */
  const bool to_memory = (op & 0x0400) == 0;
  const bool long_size = (op & 0x0040) != 0;
  const int size = long_size ? 4 : 2;
  const uint16_t mask = fetch_stream_word();
  if (aborted()) {
    return;
  }
  const Ea ea{(op >> 3) & 7, op & 7};
  if (ea.mode <= 1 || (ea.mode == 7 && ea.reg == 4)) {
    m_fault = Fault{Fault::Kind::UnimplementedOpcode, m_pc, op};
    return;
  }

  if (to_memory) {
    if (ea.mode == 4) {
      /* Predecrement reverses the mask: bit 0 selects A7, bit 15 selects
       * D0. Walking the registers from A7 down to D0 while the address
       * steps down leaves the usual D0..A7 ascending layout in memory,
       * which is what the (An)+ restore below reads back. */
      const uint32_t initial_base = m_a[ea.reg];
      for (int i = 15; i >= 0; i--) {
        if ((mask & (1u << (15 - i))) == 0) {
          continue;
        }
        m_a[ea.reg] -= (uint32_t)size;
        const uint32_t addr = m_a[ea.reg];
        /* On a 68000, including the effective-address register stores its
         * value from before the MOVEM, not the value after this transfer's
         * predecrement. Games use that value to link packed structures. */
        const uint32_t value = i < 8             ? m_d[i]
                               : i - 8 == ea.reg ? initial_base
                                                 : m_a[i - 8];
        write_mem(addr, size, size == 2 ? value & 0xFFFF : value, size == 4);
        if (aborted()) {
          return;
        }
      }
    } else {
      const uint32_t base = ea_addr(ea, size);
      if (aborted() || m_fault.kind == Fault::Kind::UnimplementedOpcode) {
        return;
      }
      uint32_t offset = 0;
      for (int i = 0; i < 16; i++) {
        if ((mask & (1u << i)) == 0) {
          continue;
        }
        const uint32_t value = i < 8 ? m_d[i] : m_a[i - 8];
        write_mem(base + offset, size, size == 2 ? value & 0xFFFF : value);
        if (aborted()) {
          return;
        }
        offset += (uint32_t)size;
      }
      if (ea.mode == 3) {
        m_a[ea.reg] = base + offset;
      }
    }
    advance_clk(4);
    prefetch_fill(1);
    return;
  }

  /* memory to registers */
  if (ea.mode == 4) {
    m_fault = Fault{Fault::Kind::UnimplementedOpcode, m_pc, op};
    return;
  }
  const uint32_t base = ea_addr(ea, size);
  if (aborted() || m_fault.kind == Fault::Kind::UnimplementedOpcode) {
    return;
  }
  uint32_t offset = 0;
  for (int i = 0; i < 16; i++) {
    if ((mask & (1u << i)) == 0) {
      continue;
    }
    uint32_t value = read_mem(base + offset, size);
    if (aborted()) {
      return;
    }
    if (size == 2) {
      value = (uint32_t)(int32_t)(int16_t)value; /* sign extend */
    }
    if (i < 8) {
      m_d[i] = value;
    } else {
      m_a[i - 8] = value;
    }
    offset += (uint32_t)size;
  }
  if (ea.mode == 3) {
    m_a[ea.reg] = base + offset;
  }
  advance_clk(4);
  prefetch_fill(1);
}

void M68k::exec_movep(uint16_t op)
{
  /* MOVEP: 0000 rrr 1 os 001 EEE. Bits 7-6: 00 MR.W, 01 MR.L, 10 RM.W,
   * 11 RM.L. The register's high byte lands at the lowest address and
   * the address steps by 2 (one byte lane per transfer); this is how
   * GEMS-style drivers reach the YM2612 from the 68K side. */
  const int opra = (op >> 6) & 3;
  const bool to_memory = opra >= 2;
  const int size = (opra & 1) != 0 ? 4 : 2;
  const Ea ea{5, op & 7}; /* (d16,An) */
  const uint32_t addr = ea_addr(ea, size);
  if (aborted() || m_fault.kind == Fault::Kind::UnimplementedOpcode) {
    return;
  }

  const int r = (op >> 9) & 7;
  if (to_memory) {
    for (int i = 0; i < size; i++) {
      const int shift = (size - 1 - i) * 8;
      bus_write_byte(addr + 2 * i, (uint8_t)(m_d[r] >> shift));
      if (aborted()) {
        return;
      }
    }
  } else {
    uint32_t value = 0;
    for (int i = 0; i < size; i++) {
      value = (value << 8) | bus_read_byte(addr + 2 * i);
      if (aborted()) {
        return;
      }
    }
    if (size == 2) {
      m_d[r] = (m_d[r] & ~0xFFFFu) | (value & 0xFFFF);
    } else {
      m_d[r] = value;
    }
  }
  advance_clk(8); /* pipeline fill; unverified */
  prefetch_fill(1);
}

void M68k::exec_pea(uint16_t op)
{
  /* PEA: 0100 1000 01 eee EEE (control EAs). */
  const Ea ea{(op >> 3) & 7, op & 7};
  const uint32_t addr = ea_addr(ea, 0);
  if (aborted() || m_fault.kind == Fault::Kind::UnimplementedOpcode) {
    return;
  }
  advance_clk(4); /* unverified */
  uint32_t sp = m_a[7] - 4;
  bus_write_word(sp, (uint16_t)(addr >> 16));
  if (aborted()) {
    return;
  }
  bus_write_word(sp + 2, (uint16_t)addr);
  if (aborted()) {
    return;
  }
  m_a[7] = sp;
  prefetch_fill(1);
}

void M68k::exec_link_unlk(uint16_t op)
{
  if ((op & 0xFFF8) == 0x4E50) { /* LINK An,#disp */
    const int r = op & 7;
    const int16_t disp = (int16_t)fetch_stream_word();
    if (aborted()) {
      return;
    }
    uint32_t sp = m_a[7] - 4;
    bus_write_word(sp, (uint16_t)(m_a[r] >> 16));
    if (aborted()) {
      return;
    }
    bus_write_word(sp + 2, (uint16_t)m_a[r]);
    if (aborted()) {
      return;
    }
    m_a[7] = sp;
    m_a[r] = sp;
    m_a[7] = (uint32_t)((int64_t)(int32_t)sp + disp);
    advance_clk(4);
    prefetch_fill(1);
    return;
  }
  /* UNLK An */
  const int r = op & 7;
  m_a[7] = m_a[r];
  const uint16_t hi = bus_read_word(m_a[7]);
  if (aborted()) {
    return;
  }
  const uint16_t lo = bus_read_word(m_a[7] + 2);
  if (aborted()) {
    return;
  }
  m_a[7] += 4;
  m_a[r] = ((uint32_t)hi << 16) | lo;
  advance_clk(4);
  prefetch_fill(1);
}

void M68k::exec_mul(uint16_t op, bool is_signed)
{
  /* MULU 1100 nnn0 11 eee EEE; MULS 1100 nnn1 11 eee EEE. */
  const Ea ea{(op >> 3) & 7, op & 7};
  const uint32_t src = ea_read(ea, 2);
  if (aborted() || m_fault.kind == Fault::Kind::UnimplementedOpcode) {
    return;
  }
  const int r = (op >> 9) & 7;
  const uint16_t dst = (uint16_t)(m_d[r] & 0xFFFF);
  uint32_t result;
  if (is_signed) {
    result = (uint32_t)((int32_t)(int16_t)src * (int32_t)(int16_t)dst);
  } else {
    result = (src & 0xFFFF) * dst;
  }
  m_d[r] = result;
  set_zn(result, 4);
  m_sr = (uint16_t)(m_sr & ~(SR_V | SR_C));
  /* 38 + 2n with n the significant-bit span of the source; MULS adds a
   * sign-correction term. unverified. */
  int bits = 0;
  uint32_t m = is_signed ? (uint32_t)(int32_t)(int16_t)src : src & 0xFFFF;
  while (m != 0) {
    bits++;
    m >>= 1;
  }
  advance_clk(38 + 2 * bits + (is_signed ? 2 : 0));
  prefetch_fill(1);
}

void M68k::exec_div(uint16_t op, bool is_signed)
{
  /* DIVU 1000 nnn0 11 eee EEE; DIVS 1000 nnn1 11 eee EEE. Divide by
   * zero traps through vector 5; overflow sets V and leaves Dn alone. */
  constexpr int kVecZeroDivide = 5;
  const Ea ea{(op >> 3) & 7, op & 7};
  const uint32_t src = ea_read(ea, 2);
  if (aborted() || m_fault.kind == Fault::Kind::UnimplementedOpcode) {
    return;
  }
  const int r = (op >> 9) & 7;
  const uint32_t dst = m_d[r];

  if ((src & 0xFFFF) == 0) {
    exception(kVecZeroDivide, m_pc, 4);
    return;
  }

  bool overflow = false;
  uint32_t quotient = 0, remainder = 0;
  if (is_signed) {
    const int32_t dividend = (int32_t)dst;
    const int32_t divisor = (int32_t)(int16_t)src;
    const int64_t q = (int64_t)dividend / divisor;
    const int64_t rem = (int64_t)dividend % divisor;
    if (q > 32767 || q < -32768) {
      overflow = true;
    } else {
      quotient = (uint32_t)((int32_t)q) & 0xFFFF;
      remainder = (uint32_t)((int32_t)rem) & 0xFFFF;
    }
  } else {
    const uint32_t dividend = dst;
    const uint32_t divisor = src & 0xFFFF;
    if (divisor != 0 && dividend / divisor > 0xFFFF) {
      overflow = true;
    } else if (divisor != 0) {
      quotient = dividend / divisor;
      remainder = dividend % divisor;
    }
  }

  if (overflow) {
    m_sr = (uint16_t)(m_sr & ~(SR_C | SR_N | SR_Z)) | SR_V;
    advance_clk(8);
    prefetch_fill(1);
    return;
  }

  m_d[r] = (remainder << 16) | quotient;
  m_sr = (uint16_t)(m_sr & ~(SR_V | SR_C | SR_N | SR_Z));
  if (quotient == 0) {
    m_sr |= SR_Z;
  }
  if (quotient & 0x8000) {
    m_sr |= SR_N;
  }
  advance_clk(is_signed ? 156 : 140); /* unverified */
  prefetch_fill(1);
}

void M68k::exec_exg(uint16_t op)
{
  /* EXG: 1100 xxx 1 <selector:5> yyy, selector in bits 7-3. */
  const int rx = (op >> 9) & 7;
  const int ry = op & 7;
  switch ((op >> 3) & 0x1F) {
  case 0x08: { /* Dx,Dy */
    const uint32_t t = m_d[rx];
    m_d[rx] = m_d[ry];
    m_d[ry] = t;
    break;
  }
  case 0x09: { /* Ax,Ay */
    const uint32_t t = m_a[rx];
    m_a[rx] = m_a[ry];
    m_a[ry] = t;
    break;
  }
  default: { /* 0x11: Dx,Ay */
    const uint32_t t = m_d[rx];
    m_d[rx] = m_a[ry];
    m_a[ry] = t;
    break;
  }
  }
  advance_clk(2);
  prefetch_fill(1);
}

/* ------------------------------------------------------------------ */
/* savestate                                                           */
/* ------------------------------------------------------------------ */

M68k::SavedState M68k::save() const
{
  SavedState s{};
  for (int i = 0; i < 8; i++) {
    s.d[i] = m_d[i];
    s.a[i] = m_a[i];
  }
  s.pc = m_pc;
  s.usp = m_usp;
  s.ssp = m_ssp;
  s.sr = m_sr;
  s.clk = m_clk;
  s.queue_len = m_queue_len;
  s.queue[0] = m_queue[0];
  s.queue[1] = m_queue[1];
  s.prefetch_addr = m_prefetch_addr;
  s.fault_kind = (uint8_t)m_fault.kind;
  return s;
}

void M68k::restore(const SavedState &state)
{
  for (int i = 0; i < 8; i++) {
    m_d[i] = state.d[i];
    m_a[i] = state.a[i];
  }
  m_pc = state.pc;
  m_usp = state.usp;
  m_ssp = state.ssp;
  m_sr = state.sr;
  m_clk = state.clk;
  m_queue_len = state.queue_len;
  m_queue[0] = state.queue[0];
  m_queue[1] = state.queue[1];
  m_prefetch_addr = state.prefetch_addr;
  m_fault = Fault{static_cast<Fault::Kind>(state.fault_kind), 0, 0};
  m_abort = false;
  m_in_exception = false;
  m_ipl = 7;
}

}  // namespace generator
