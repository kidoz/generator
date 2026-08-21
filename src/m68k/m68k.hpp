/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Cycle-accurate NMOS 68000 (bus-operation sequencer).
 *
 * Opcode encodings are cross-checked against this repository's own
 * def68k.def tables; instruction timing is transcribed from the Motorola
 * M68000 User's Manual clock tables. Cycle choices we could not fully
 * confirm are pinned to our best-known values and marked `unverified`.
 *
 * Execution model: step() runs exactly one instruction (or interrupt
 * event) as an explicit sequence of bus operations. Each bus operation is
 * a 4-clock cycle; internal cycles advance time with the bus idle. The
 * two-word prefetch queue supplies opcode/extension words; handlers
 * transcribe the published fetch/operand/refill order per form.
 *
 * Time advances through MasterClockSink so sibling clock domains (VDP,
 * Z80, audio) interleave at master-clock granularity. */

#pragma once

#include <cstdint>
#include <functional>

namespace generator {

class M68kBus;

/* Implemented by Machine: advances sibling clock domains by master ticks. */
class MasterClockSink {
public:
  virtual ~MasterClockSink() = default;
  virtual void advance_mclk(uint64_t ticks) = 0;
};

class M68k {
public:
  struct Fault {
    enum class Kind {
      None,
      UnimplementedOpcode, /* opcode outside the implemented subset */
      Halted,              /* double bus/address fault: CPU stopped */
      Stopped,             /* STOP #imm executed (interrupt recovers) */
    };
    Kind kind = Kind::None;
    uint32_t pc = 0;
    uint16_t opcode = 0;
  };

  /* Bus trace entry (tests only; recording disabled by default). */
  struct TraceEntry {
    enum class Kind {
      Read,
      Write,
      Fetch
    };
    Kind kind;
    uint32_t addr;
    int size; /* 1 or 2 bytes */
    uint16_t value;
    bool upper, lower;
    uint64_t clk;
  };

  M68k(M68kBus &bus, MasterClockSink &clock);

  void power_on_reset();

  /* One instruction, or one interrupt/stop-wakeup event. No-op while
   * halted. While Stopped: takes a pending interrupt or returns. */
  void step();

  void set_ipl(int level); /* 0..7; 7 = no request */

  const Fault &fault() const
  {
    return m_fault;
  }
  bool halted() const
  {
    return m_fault.kind == Fault::Kind::Halted;
  }
  bool stopped() const
  {
    return m_fault.kind == Fault::Kind::Stopped;
  }

  /* --- state (tests, savestate) --- */
  uint32_t d(int i) const
  {
    return m_d[i & 7];
  }
  uint32_t a(int i) const
  {
    return m_a[i & 7];
  }
  uint32_t pc() const
  {
    return m_pc;
  }
  uint16_t sr() const
  {
    return m_sr;
  }
  uint32_t usp() const
  {
    return m_usp;
  }
  uint64_t clk() const
  {
    return m_clk;
  }
  int queue_len() const
  {
    return m_queue_len;
  }

  /* Debugger/test seam: direct register injection. */
  void set_d(int i, uint32_t v)
  {
    m_d[i & 7] = v;
  }
  void set_a(int i, uint32_t v)
  {
    m_a[i & 7] = v;
  }

  void set_trace(std::function<void(const TraceEntry &)> trace);

  /* Savestate v3 payload (chunk M68K). */
  struct SavedState {
    uint32_t d[8], a[8], pc, usp, ssp;
    uint16_t sr;
    uint64_t clk;
    int queue_len;
    uint16_t queue[2];
    uint32_t prefetch_addr;
    uint8_t fault_kind;
  };
  SavedState save() const;
  void restore(const SavedState &state);

private:
  static constexpr uint16_t SR_C = 1 << 0, SR_V = 1 << 1, SR_Z = 1 << 2,
                            SR_N = 1 << 3, SR_X = 1 << 4, SR_I = 7 << 8,
                            SR_S = 1 << 13, SR_T = 1 << 15;

  bool cond(int cc4) const;

  /* clock + bus cycle engine */
  void advance_clk(int clocks);
  void trace_bus(TraceEntry::Kind kind, uint32_t addr, int size, uint16_t value,
                 bool upper, bool lower);
  uint16_t bus_read_word(uint32_t addr);
  uint8_t bus_read_byte(uint32_t addr);
  void bus_write_word(uint32_t addr, uint16_t v);
  void bus_write_byte(uint32_t addr, uint8_t v);

  /* prefetch queue */
  uint16_t fetch_stream_word();
  void prefetch_fill(int words);
  void flush_stream(uint32_t new_pc);

  /* exceptions */
  /* interrupt_level >= 0 raises the SR mask to that level on entry, after
   * the stacked SR copy is taken; -1 is a non-interrupt exception. */
  void exception(int vector, uint32_t pushed_pc, int internal_clocks,
                 int interrupt_level = -1);
  void address_error(uint32_t fault_addr);
  void interrupt_exception(int level);
  bool aborted() const
  {
    return m_abort;
  }
  void switch_supervisor(bool to_supervisor);

  /* effective addresses */
  struct Ea {
    int mode, reg;
  };
  uint32_t ea_addr(const Ea &ea, int size);
  uint32_t ea_read(const Ea &ea, int size);
  void ea_write(const Ea &ea, int size, uint32_t value);
  uint32_t read_mem(uint32_t addr, int size);
  /* low_word_first applies to -(An) long writes only (see write_mem). */
  void write_mem(uint32_t addr, int size, uint32_t value,
                 bool low_word_first = false);
  static bool ea_is_memory(const Ea &ea)
  {
    return ea.mode >= 2 && !(ea.mode == 7 && ea.reg == 4);
  }

  /* instruction execution */
  void execute(uint16_t opcode);
  void exec_move(uint16_t op);
  void exec_movea(uint16_t op);
  void exec_moveq(uint16_t op);
  void exec_lea(uint16_t op);
  void exec_alu(uint16_t op, int op_kind);
  void exec_x_op(uint16_t op, bool is_add); /* ADDX/SUBX */
  void exec_bcd(uint16_t op, bool is_add);  /* ABCD/SBCD */
  void exec_nbcd(uint16_t op);
  void exec_negx(uint16_t op);
  void exec_chk(uint16_t op);
  void exec_immediate(uint16_t op);
  void exec_addq_subq(uint16_t op);
  void exec_scc(uint16_t op);
  void exec_dbcc(uint16_t op);
  void exec_branch(uint16_t op);
  void exec_jsr_jmp(uint16_t op);
  void exec_single(uint16_t op);
  void exec_clr_neg_not_tst(uint16_t op);
  void exec_shift(uint16_t op);
  uint32_t shift_once(uint32_t v, int type, bool left, int size);
  void exec_bitop(uint16_t op);
  void exec_move_sr(uint16_t op);
  void exec_movem(uint16_t op);
  void exec_movep(uint16_t op);
  void exec_pea(uint16_t op);
  void exec_link_unlk(uint16_t op);
  void exec_mul(uint16_t op, bool is_signed);
  void exec_div(uint16_t op, bool is_signed);
  void exec_exg(uint16_t op);

  /* flags */
  static uint32_t bmask(int size);
  static uint32_t bsign(uint32_t v, int size);
  void set_zn(uint32_t res, int size);
  void flags_logic(uint32_t res, int size);
  void flags_add(uint32_t a, uint32_t b, uint32_t res, int size,
                 uint32_t carry_in = 0);
  void flags_sub(uint32_t a, uint32_t b, uint32_t res, int size,
                 uint32_t borrow_in = 0);
  void flags_cmp(uint32_t a, uint32_t b, uint32_t res, int size);

  M68kBus &m_bus;
  MasterClockSink &m_clock;

  uint32_t m_d[8]{};
  uint32_t m_a[8]{};
  uint32_t m_usp = 0;
  uint32_t m_ssp = 0;
  uint32_t m_pc = 0;
  uint16_t m_sr = SR_S | SR_I; /* supervisor, interrupts masked */
  uint64_t m_clk = 0;
  int m_refresh_acc = 0; /* clocks since the last work-RAM refresh */

  uint16_t m_queue[2]{};
  int m_queue_len = 0;
  uint32_t m_prefetch_addr = 0;

  int m_ipl = 7;         /* 7 = no request */
  uint16_t m_cur_op = 0; /* opcode being executed (fault reporting) */
  Fault m_fault{};
  bool m_abort = false;
  bool m_in_exception = false;

  std::function<void(const TraceEntry &)> m_trace;
};

}  // namespace generator
