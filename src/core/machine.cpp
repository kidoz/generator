/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "machine.hpp"

#include "state_v3.hpp"

#include "gensoundp.h" /* SOUND_SAMPLERATE */
#include "uiplot_cram.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <utility>

namespace generator {

namespace {

/* The board's power-on reset generator divides the master clock down, so
 * both CPUs stay in reset far longer than the external reset pulse: the
 * 68K's /RESET and /HALT release together 693,132 master clocks after
 * power-on, roughly three quarters of a field. Everything else — the VDP
 * counters above all — is already running by then, and the phase that
 * leaves between the CPU's code and the raster is what boot-time races
 * in game code are written against. */
constexpr uint64_t kResetHoldMclk = 693132;

/* Master clocks in one second of emulated time: 3420 per line, 262 lines
 * at 60 fields NTSC and 312 at 50 PAL. Deriving the audio rate from the
 * same numbers the VDP counts keeps the sample stream locked to the
 * emulated field rather than to the wall clock. */
constexpr uint64_t kMclkPerSecondNtsc = 3420ULL * 262 * 60;
constexpr uint64_t kMclkPerSecondPal = 3420ULL * 312 * 50;

bool has_rom_signature(std::span<const uint8_t> image, size_t offset)
{
  if (offset + 4 > image.size()) {
    return false;
  }
  const auto *signature = image.data() + offset;
  return std::memcmp(signature, "SEGA", 4) == 0 ||
         std::memcmp(signature, "ESAG", 4) == 0;
}

std::expected<std::vector<uint8_t>, std::string>
normalize_rom(std::span<const uint8_t> image)
{
  if (image.size() < 0x200) {
    return std::unexpected("ROM image is too small");
  }
  if (image[0] == 'P' && image[1] == 'K' &&
      ((image[2] == 0x03 && image[3] == 0x04) ||
       (image[2] == 0x05 && image[3] == 0x06) ||
       (image[2] == 0x07 && image[3] == 0x08))) {
    return std::unexpected(
        "ZIP archives are not supported; extract the ROM before loading");
  }

  const bool smd_header = image.size() > 10 && image[8] == 0xAA &&
                          image[9] == 0xBB && image[10] == 0x06;
  const bool smd_signature = image.size() > 0x2281 && image[0x280] == 'E' &&
                             image[0x281] == 'A' && image[0x2280] == 'S' &&
                             image[0x2281] == 'G';

  if (smd_header || smd_signature) {
    constexpr size_t kHeaderSize = 512;
    constexpr size_t kBlockSize = 16384;
    constexpr size_t kHalfBlock = kBlockSize / 2;
    if (image.size() < kHeaderSize + kBlockSize ||
        (image.size() - kHeaderSize) % kBlockSize != 0) {
      return std::unexpected("SMD ROM image is corrupt");
    }

    std::vector<uint8_t> rom(image.size() - kHeaderSize);
    const size_t blocks = rom.size() / kBlockSize;
    for (size_t block = 0; block < blocks; block++) {
      const size_t source = kHeaderSize + block * kBlockSize;
      const size_t target = block * kBlockSize;
      for (size_t byte = 0; byte < kHalfBlock; byte++) {
        rom[target + byte * 2] = image[source + kHalfBlock + byte];
        rom[target + byte * 2 + 1] = image[source + byte];
      }
    }
    return rom;
  }

  size_t offset = 0;
  if (image.size() > 0x300 && has_rom_signature(image, 0x300) &&
      !has_rom_signature(image, 0x100)) {
    offset = 512;
  }
  std::vector<uint8_t> rom(image.begin() + (std::ptrdiff_t)offset, image.end());

  if (has_rom_signature(rom, 0x100) &&
      std::memcmp(rom.data() + 0x100, "ESAG", 4) == 0) {
    for (size_t byte = 0; byte + 1 < rom.size(); byte += 2) {
      std::swap(rom[byte], rom[byte + 1]);
    }
  }
  return rom;
}

uint64_t fnv1a64(const uint8_t *data, size_t len)
{
  uint64_t h = 0xcbf29ce484222325ULL;
  for (size_t i = 0; i < len; i++) {
    h ^= data[i];
    h *= 0x100000001b3ULL;
  }
  return h;
}

void header_text(char *out, size_t out_size, const uint8_t *rom, size_t field,
                 size_t field_size)
{
  size_t end = field_size;
  while (end > 0 &&
         (rom[field + end - 1] == ' ' || rom[field + end - 1] == 0)) {
    end--;
  }
  size_t n = std::min(end, out_size - 1);
  std::memcpy(out, rom + field, n);
  out[n] = '\0';
}

}  // namespace

Machine::Machine(std::unique_ptr<IAudioBackend> audio,
                 std::unique_ptr<IVideoBackend> video,
                 std::shared_ptr<ILogger> logger)
    : m_audio(std::move(audio)), m_video(std::move(video)),
      m_logger(std::move(logger)), m_bus(*this), m_cpu(m_bus, *this),
      m_z80bus(), m_z80(m_z80bus)
{
  m_z80bus.attach_68k(&m_bus);
  /* DMA source reads go through the real 68K bus (no direct pointers):
   * ROM, RAM mirrors and open bus all route naturally. */
  m_vdp.set_dma_reader([this](uint32_t addr) {
    uint16_t word = 0;
    m_bus.read(addr, true, true, &word);
    return word;
  });
  m_blank_line.assign(Vdp::kLineBufferWidth, 0);
}

Machine::~Machine() = default;

std::expected<void, std::string> Machine::load_rom(std::string_view filename)
{
  std::ifstream in{std::string(filename), std::ios::binary};
  if (!in) {
    return std::unexpected("cannot open ROM file: " + std::string(filename));
  }
  std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());

  auto result = load_rom_mem(data);
  if (!result) {
    return result;
  }

  const std::string name(filename);
  const size_t slash = name.find_last_of("/\\");
  std::snprintf(m_leafname.data(), m_leafname.size(), "%s",
                slash == std::string::npos ? name.c_str()
                                           : name.c_str() + slash + 1);
  return {};
}

std::expected<void, std::string>
Machine::load_rom_mem(std::span<const uint8_t> rom_data)
{
  auto normalized = normalize_rom(rom_data);
  if (!normalized) {
    return std::unexpected(normalized.error());
  }

  /* Detach borrowed storage before either vector can reallocate. The new
   * cartridge starts from a cold machine on the next frame. */
  m_bus.attach_rom(nullptr, 0);
  m_bus.disable_sram();
  m_sram.clear();
  m_rom = std::move(*normalized);
  parse_rom_header();
  setup_sram();
  m_powered = false;
  m_halted = false;
  m_mclk_total = 0;
  m_frames = 0;
  return {};
}

void Machine::unload_rom()
{
  m_bus.attach_rom(nullptr, 0);
  m_bus.disable_sram();
  m_sram.clear();
  m_rom.clear();
  m_cartinfo = {};
  m_leafname = {};
  m_powered = false;
  m_halted = false;
}

bool Machine::rom_loaded() const
{
  return !m_rom.empty();
}

void Machine::power_on()
{
  m_mclk_total = 0;
  m_bus.reset();
  m_bus.attach_rom(m_rom.data(), m_rom.size());
  m_bus.attach_ram(m_ram.data());
  m_ram.fill(0);
  m_z80bus.reset();
  m_io_ctrl = {};
  m_z80_busreq = false;
  m_z80_resume_delay = 0;
  m_z80.power_on_reset(); /* held from power-on; the ROM's boot code
                             releases it through 0xA11200 */
  m_vdp.reset(m_pal);
  m_halted = false;
  m_frames = 0;
  m_audio_acc = 0;
  m_audio_left.clear();
  m_audio_right.clear();

  /* Every other domain ticks through the reset hold; only then does the
     CPU fetch its vectors. */
  advance_mclk(kResetHoldMclk);
  m_cpu.power_on_reset();
  m_powered = true;
}

void Machine::reset()
{
  m_powered = false;
  if (rom_loaded()) {
    power_on();
  }
  m_frames = 0;
}

void Machine::soft_reset()
{
  reset();
}

void Machine::run_frame()
{
  if (!rom_loaded() || m_halted) {
    /* Still emit a field so backends observe time passing. */
    finish_frame_video();
    m_frames++;
    return;
  }
  if (!m_powered) {
    power_on();
  }

  const uint64_t frame_start = m_vdp.frame_index();
  while (m_vdp.frame_index() == frame_start) {
    if (m_cpu.halted()) {
      halt("68K halted (double fault)");
      break;
    }
    if (m_cpu.fault().kind == M68k::Fault::Kind::UnimplementedOpcode) {
      char text[96];
      std::snprintf(text, sizeof(text), "68K unimplemented opcode %04X at %06X",
                    m_cpu.fault().opcode, m_cpu.fault().pc);
      halt(text);
      break;
    }
    if (m_cpu.stopped()) {
      /* STOP: hold until an interrupt or the field ends. */
      const uint64_t before = m_vdp.frame_index();
      advance_mclk(64);
      if (m_vdp.frame_index() != before) {
        break;
      }
      continue;
    }
    m_cpu.step();

    /* Completed DMA stole the 68K bus: advance every other domain by
     * the debt while the CPU's own clock stands still (the halt). */
    const uint64_t debt = m_vdp.take_dma_debt();
    if (debt != 0) {
      advance_mclk(debt);
    }
  }

  finish_frame_video();
  m_frames++;
}

void Machine::finish_frame_video()
{
  /* Publish the CRAM snapshot for uiplot's palette cache */
  uiplot_set_cram(m_vdp.cram(), m_vdp.cram_dirty());

  const int height = (int)m_vdp.visible_lines();
  /* Only the cells the VDP is actually displaying: the line buffer is
   * always allocated for H40, so handing the backend its full width pads
   * an H32 field with 64 columns of backdrop and leaves the picture
   * sitting against the left edge. */
  const std::size_t width = m_vdp.visible_width();
  for (int line = 0; line < height; line++) {
    m_video->render_line(line, std::span<const uint8_t>(
                                   m_vdp.line_pixels((uint32_t)line), width));
  }
  m_video->present_field();

  flush_audio();
}

void Machine::mix_audio_sample()
{
  const int32_t psg = m_z80bus.psg().output();
  const int32_t left = m_z80bus.ym().sample_left() + psg;
  const int32_t right = m_z80bus.ym().sample_right() + psg;
  m_audio_left.push_back((uint16_t)(int16_t)std::clamp(left, -32768, 32767));
  m_audio_right.push_back((uint16_t)(int16_t)std::clamp(right, -32768, 32767));
}

void Machine::flush_audio()
{
  /* A field with no emulation behind it (no cartridge, or the machine
   * halted) still owes the sound hardware its samples, or the device
   * underruns and the UI pacing loop free-runs. */
  const std::size_t owed = (std::size_t)SOUND_SAMPLERATE / framerate();
  while (m_audio_left.size() < owed) {
    m_audio_left.push_back(0);
    m_audio_right.push_back(0);
  }

  m_audio->output_samples(m_audio_left, m_audio_right);
  m_audio_left.clear();
  m_audio_right.clear();
}

void Machine::halt(const char *why)
{
  if (!m_halted) {
    m_logger->log(LogLevel::Critical, why);
    m_halted = true;
  }
}

/* ------------------------------------------------------------------ */
/* MasterClockSink                                                     */
/* ------------------------------------------------------------------ */

void Machine::advance_mclk(uint64_t ticks)
{
  m_mclk_total += ticks;

  /* One output sample per SOUND_SAMPLERATE-th of an emulated second. The
   * accumulator carries the remainder so the rate stays exact over a
   * field instead of drifting. */
  const uint64_t per_second = m_pal ? kMclkPerSecondPal : kMclkPerSecondNtsc;
  m_audio_acc += ticks * (uint64_t)SOUND_SAMPLERATE;
  while (m_audio_acc >= per_second) {
    m_audio_acc -= per_second;
    mix_audio_sample();
  }
  const int ipl = m_vdp.advance_mclk(ticks);
  m_cpu.set_ipl(ipl);
  /* The FM chip's /IRQ pin is not routed on this board — the sound CPU's
   * only interrupt source is the VDP. Drivers poll the FM status register
   * for timer overflows instead, so the chip still tracks its own IRQ
   * line but nothing consumes it. */
  m_z80bus.ym().advance_mclk(ticks);
  m_z80bus.psg().advance_mclk(ticks);
  m_z80.set_int(m_vdp.zint());

  /* BUSACK resume latency: the Z80 stays frozen for a few cycles
   * after the bus is returned before it actually executes */
  if (m_z80_resume_delay > 0) {
    const uint32_t consume =
        (uint32_t)std::min<uint64_t>(m_z80_resume_delay, ticks);
    m_z80_resume_delay -= consume;
    if (m_z80_resume_delay > 0) {
      return; /* still in the sync window */
    }
    ticks -= consume;
    if (ticks == 0) {
      return;
    }
  }
  m_z80.advance_mclk(ticks, m_z80_busreq);
}

/* ------------------------------------------------------------------ */
/* BusDevices: VDP, IO, Z80 space                                      */
/* ------------------------------------------------------------------ */

uint16_t Machine::vdp_read(uint32_t addr, bool, bool)
{
  return m_vdp.port_read(addr);
}

void Machine::vdp_write(uint32_t addr, uint16_t data, bool, bool lower)
{
  /* The PSG sits in the VDP's address window at the odd byte of the
   * $10-$17 block. It is a chip of its own — it hangs off the low data
   * byte and never reaches the VDP, so an even-byte write there is lost. */
  const uint32_t port = (addr & 0x1C) >> 2;
  if (port == 4 || port == 5) {
    if (lower) {
      m_z80bus.psg().write((uint8_t)data);
    }
    return;
  }
  m_vdp.port_write(addr, data);
}

uint16_t Machine::io_read(uint32_t addr, bool, bool)
{
  const uint32_t page = addr & 0x1F00;
  if (page == 0x0000) {
    /* IO data/version registers, index by (addr >> 1) & 7:
     * 0 version, 1 port A data, 2 port B data, 3 port C data,
     * 4 ctrl A, 5 ctrl B, 6 ctrl C. Version bits: bit0=1,
     * bit5=disk, bit6=PAL, bit7=Japan. */
    switch ((addr >> 1) & 7) {
    case 0:
      /* Version register. It describes the *console*, not the cartridge:
       * bit 7 is 0 for a domestic (Japanese) machine and 1 for an
       * overseas one, bit 6 is 0 NTSC / 1 PAL, bit 5 reads 1 while no
       * expansion (Mega CD) is attached, and the low nibble is the
       * hardware revision. Region-locked games branch on bits 7-6, so
       * reporting the cartridge's own flags here sends a Japan-only
       * cartridge down its foreign-machine path. */
      return (uint16_t)(0x21 | (m_pal ? 0x40 : 0) | (m_domestic ? 0x00 : 0x80));
    case 1: {
      /* port A data: 3-button pad protocol
       * bits: 0=up 1=down 2=left 3=right 4=TL(start) 5=TR 6=TH
       * lines are active-low (0 = pressed) */
      const InputState &in = m_input[0];
      uint8_t v = 0x7F; /* all released */
      if (in.up)
        v &= ~0x01;
      if (in.down)
        v &= ~0x02;
      if (in.left)
        v &= ~0x04;
      if (in.right)
        v &= ~0x08;
      if (in.start)
        v &= ~0x10;
      return v;
    }
    case 2: {
      /* port B data */
      const InputState &in = m_input[1];
      uint8_t v = 0x7F;
      if (in.up)
        v &= ~0x01;
      if (in.down)
        v &= ~0x02;
      if (in.left)
        v &= ~0x04;
      if (in.right)
        v &= ~0x08;
      if (in.start)
        v &= ~0x10;
      return v;
    }
    case 3:
      return 0x00;
    default:
      return m_io_ctrl[((addr >> 1) & 7) - 4];
    }
  }
  if (page == 0x1100) {
    /* BUSACK (bit 0): 0 = Z80 halted, bus granted to the 68K; 1 = Z80
       running. The phase-1 grant is immediate once the request line is
       set. Games poll this bit for 0 after writing 0x0100. */
    return m_z80_busreq ? 0x0000 : 0x0001;
  }
  return 0;
}

void Machine::io_write(uint32_t addr, uint16_t data, bool, bool)
{
  const uint32_t page = addr & 0x1F00;
  if (page == 0x0000 && ((addr >> 1) & 7) >= 4) {
    m_io_ctrl[((addr >> 1) & 7) - 4] = (uint8_t)(data & 0xFF);
  } else if (page == 0x1100) {
    const bool was_requested = m_z80_busreq;
    m_z80_busreq = (data & 0x0100) != 0;
    if (was_requested && !m_z80_busreq) {
      /* release: the Z80 resumes after ~3 68K cycles of sync delay */
      m_z80_resume_delay = 3 * 7;
    }
  } else if (page == 0x1200) {
    /* ZRESET: bit 8 CLEAR holds the Z80 in reset (games write 0x0000 to
       assert, 0x0100 to release). */
    m_z80.reset_line((data & 0x0100) == 0);
  }
}

uint16_t Machine::z80_read(uint32_t addr, bool upper, bool lower)
{
  /* Mailbox protocols have the 68K peek Z80 RAM while the Z80 runs (no
   * BUSREQ), so reads are always served; the read-contention timing is
   * an arbiter-phase refinement. */
  return m_z80bus.read_word_68k(addr & 0xFFFF);
}

void Machine::z80_write(uint32_t addr, uint16_t data, bool upper, bool lower)
{
  m_z80bus.write_word_68k(addr & 0xFFFF, data, upper, lower);
}

void Machine::interrupt_ack(int level)
{
  /* The VDP is the only interrupt source on this board. */
  m_vdp.acknowledge_int(level);
}

/* ------------------------------------------------------------------ */
/* savestate v3                                                        */
/* ------------------------------------------------------------------ */

int Machine::save_state(const char *filename)
{
  if (filename == nullptr || m_rom.empty()) {
    return -1;
  }

  /* Keep the legacy BORD identifier stable across the Board-to-Machine class
   * rename so existing v3 states remain readable. */
  StateChunk board_chunk{fourcc("BORD"), {}};
  {
    ChunkWriter w;
    w.u32(m_frames);
    w.u64(m_mclk_total);
    w.u8(m_pal ? 1 : 0);
    w.u8(m_halted ? 1 : 0);
    board_chunk.data = w.take_payload();
  }

  StateChunk rom_chunk{fourcc("ROMI"), {}};
  {
    ChunkWriter w;
    w.u64(fnv1a64(m_rom.data(), m_rom.size()));
    w.u32((uint32_t)m_rom.size());
    rom_chunk.data = w.take_payload();
  }

  StateChunk cpu_chunk{fourcc("M68K"), {}};
  {
    const M68k::SavedState cpu = m_cpu.save();
    ChunkWriter w;
    for (int i = 0; i < 8; i++) {
      w.u32(cpu.d[i]);
    }
    for (int i = 0; i < 8; i++) {
      w.u32(cpu.a[i]);
    }
    w.u32(cpu.pc);
    w.u32(cpu.usp);
    w.u32(cpu.ssp);
    w.u16(cpu.sr);
    w.u64(cpu.clk);
    w.u8((uint8_t)cpu.queue_len);
    w.u16(cpu.queue[0]);
    w.u16(cpu.queue[1]);
    w.u32(cpu.prefetch_addr);
    w.u8(cpu.fault_kind);
    cpu_chunk.data = w.take_payload();
  }

  StateChunk ram_chunk{fourcc("W68K"), {}};
  {
    ChunkWriter w;
    w.bytes(m_ram);
    ram_chunk.data = w.take_payload();
  }

  StateChunk zram_chunk{fourcc("WZ80"), {}};
  {
    ChunkWriter w;
    w.bytes(m_z80bus.ram());
    zram_chunk.data = w.take_payload();
  }

  StateChunk z80_chunk{fourcc("Z80C"), {}};
  {
    const z80f::Snapshot snap = m_z80.save();
    ChunkWriter w;
    const z80f::Registers &r = snap.registers;
    w.u8(r.a);
    w.u8(r.flags.bits);
    w.u8(r.b);
    w.u8(r.c);
    w.u8(r.d);
    w.u8(r.e);
    w.u8(r.h);
    w.u8(r.l);
    w.u8(r.a_alt);
    w.u8(r.f_alt);
    w.u8(r.b_alt);
    w.u8(r.c_alt);
    w.u8(r.d_alt);
    w.u8(r.e_alt);
    w.u8(r.h_alt);
    w.u8(r.l_alt);
    w.u16(r.ix);
    w.u16(r.iy);
    w.u16(r.sp);
    w.u16(r.pc);
    w.u16(r.wz);
    w.u8(r.i);
    w.u8(r.r);
    w.u8((uint8_t)(r.iff1 ? 1 : 0));
    w.u8((uint8_t)(r.iff2 ? 1 : 0));
    w.u8(r.im);
    w.u8((uint8_t)(r.halted ? 1 : 0));
    w.u8((uint8_t)(r.ei_pending ? 1 : 0));
    w.u64(snap.cycle_counter);
    w.u64(snap.base_cycle_counter);
    w.u8(snap.nmi_line ? 1 : 0);
    w.u8(snap.nmi_pending ? 1 : 0);
    w.u8(snap.int_line ? 1 : 0);
    w.u8(snap.int_pulse_pending ? 1 : 0);
    w.u64(m_z80.t_states());
    z80_chunk.data = w.take_payload();
  }

  const StateChunk chunks[] = {std::move(board_chunk), std::move(rom_chunk),
                               std::move(cpu_chunk),   std::move(ram_chunk),
                               std::move(zram_chunk),  std::move(z80_chunk)};
  const std::vector<uint8_t> blob = StateV3::serialize(chunks);

  std::ofstream out{filename, std::ios::binary};
  if (!out) {
    return -1;
  }
  out.write(reinterpret_cast<const char *>(blob.data()),
            (std::streamsize)blob.size());
  return out ? 0 : -1;
}

int Machine::load_state(const char *filename)
{
  if (filename == nullptr || m_rom.empty()) {
    return -1;
  }

  std::ifstream in{filename, std::ios::binary};
  if (!in) {
    return -1;
  }
  std::vector<uint8_t> blob((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
  auto chunks = StateV3::deserialize(blob);
  if (!chunks) {
    return -1;
  }

  bool board_seen = false, rom_matches = false, cpu_seen = false;
  for (const StateChunk &chunk : *chunks) {
    if (chunk.id == fourcc("BORD")) {
      ChunkReader r{chunk.data};
      m_frames = r.u32();
      m_mclk_total = r.u64();
      m_pal = r.u8() != 0;
      m_halted = r.u8() != 0;
      board_seen = r.ok();
    } else if (chunk.id == fourcc("ROMI")) {
      ChunkReader r{chunk.data};
      const uint64_t rom_hash = r.u64();
      const uint32_t rom_size = r.u32();
      rom_matches = r.ok() && rom_size == m_rom.size() &&
                    rom_hash == fnv1a64(m_rom.data(), m_rom.size());
    } else if (chunk.id == fourcc("M68K")) {
      M68k::SavedState cpu{};
      ChunkReader r{chunk.data};
      for (int i = 0; i < 8; i++) {
        cpu.d[i] = r.u32();
      }
      for (int i = 0; i < 8; i++) {
        cpu.a[i] = r.u32();
      }
      cpu.pc = r.u32();
      cpu.usp = r.u32();
      cpu.ssp = r.u32();
      cpu.sr = r.u16();
      cpu.clk = r.u64();
      cpu.queue_len = r.u8();
      cpu.queue[0] = r.u16();
      cpu.queue[1] = r.u16();
      cpu.prefetch_addr = r.u32();
      cpu.fault_kind = r.u8();
      cpu_seen = r.ok();
      if (cpu_seen) {
        m_cpu.restore(cpu);
      }
    } else if (chunk.id == fourcc("W68K")) {
      ChunkReader r{chunk.data};
      const std::span<const uint8_t> data = r.bytes(m_ram.size());
      if (r.ok()) {
        std::memcpy(m_ram.data(), data.data(), m_ram.size());
      }
    } else if (chunk.id == fourcc("WZ80")) {
      ChunkReader r{chunk.data};
      const std::span<const uint8_t> data = r.bytes(m_z80bus.ram().size());
      if (r.ok()) {
        std::memcpy(m_z80bus.ram().data(), data.data(), m_z80bus.ram().size());
      }
    } else if (chunk.id == fourcc("Z80C")) {
      z80f::Snapshot snap;
      z80f::Registers &r0 = snap.registers;
      ChunkReader r{chunk.data};
      r0.a = r.u8();
      r0.flags.bits = r.u8();
      r0.b = r.u8();
      r0.c = r.u8();
      r0.d = r.u8();
      r0.e = r.u8();
      r0.h = r.u8();
      r0.l = r.u8();
      r0.a_alt = r.u8();
      r0.f_alt = r.u8();
      r0.b_alt = r.u8();
      r0.c_alt = r.u8();
      r0.d_alt = r.u8();
      r0.e_alt = r.u8();
      r0.h_alt = r.u8();
      r0.l_alt = r.u8();
      r0.ix = r.u16();
      r0.iy = r.u16();
      r0.sp = r.u16();
      r0.pc = r.u16();
      r0.wz = r.u16();
      r0.i = r.u8();
      r0.r = r.u8();
      r0.iff1 = r.u8() != 0;
      r0.iff2 = r.u8() != 0;
      r0.im = r.u8();
      r0.halted = r.u8() != 0;
      r0.ei_pending = r.u8() != 0;
      snap.cycle_counter = r.u64();
      snap.base_cycle_counter = r.u64();
      snap.nmi_line = r.u8() != 0;
      snap.nmi_pending = r.u8() != 0;
      snap.int_line = r.u8() != 0;
      snap.int_pulse_pending = r.u8() != 0;
      const uint64_t tstates = r.u64();
      if (r.ok()) {
        m_z80.restore(snap);
        m_z80.restore_tstates(tstates);
      }
    }
    /* Unknown chunk ids are skipped: forward compatibility within v3. */
  }

  if (!board_seen || !rom_matches || !cpu_seen) {
    return -1;
  }
  m_powered = true;
  m_bus.reset();
  m_bus.attach_rom(m_rom.data(), m_rom.size());
  m_bus.attach_ram(m_ram.data());
  return 0;
}

/* ------------------------------------------------------------------ */
/* misc API                                                            */
/* ------------------------------------------------------------------ */

void Machine::set_input(int player, unsigned int up, unsigned int down,
                        unsigned int left, unsigned int right,
                        unsigned int start, unsigned int a, unsigned int b,
                        unsigned int c)
{
  (void)player;
  (void)up;
  (void)down;
  (void)left;
  (void)right;
  (void)start;
  (void)a;
  (void)b;
  (void)c; /* controller ports land with the IO phase */
}

void Machine::set_video_mode(int pal, int autodetect)
{
  m_autodetect = autodetect != 0;
  if (!m_autodetect) {
    m_pal = pal != 0;
    m_vdp.reset(m_pal);
  }
}

int Machine::video_mode() const
{
  return m_pal ? 1 : 0;
}

unsigned int Machine::framerate() const
{
  return m_pal ? 50 : 60;
}

void Machine::screen_size(int *width, int *height) const
{
  /* Whatever the VDP is actually displaying: register 12 bit 0 picks the
   * 40- or 32-cell width, register 1 bit 3 the 28- or 30-cell height. */
  if (width != nullptr) {
    *width = (int)m_vdp.visible_width();
  }
  if (height != nullptr) {
    *height = (int)m_vdp.visible_lines();
  }
}

const t_cartinfo *Machine::rom_info() const
{
  return &m_cartinfo;
}

const char *Machine::rom_name() const
{
  return m_leafname.data();
}

void Machine::set_debug(int enabled)
{
  m_debug = enabled;
}

void Machine::set_loglevel(int level)
{
  m_loglevel = level;
}

unsigned int Machine::frame_count() const
{
  return m_frames;
}

void Machine::setup_sram()
{
  m_bus.disable_sram();
  m_sram.clear();

  /* External-memory header: "RA" at $1B0, type at $1B2, then inclusive
   * start/end addresses at $1B4/$1B8. */
  if (m_rom.size() < 0x1BC || m_rom[0x1B0] != 'R' || m_rom[0x1B1] != 'A') {
    return;
  }
  const uint8_t sram_type = m_rom[0x1B2];
  if ((sram_type & 0x48) != 0x48) { /* not battery-backed SRAM */
    return;
  }
  const uint32_t start = (uint32_t)m_rom[0x1B4] << 24 |
                         (uint32_t)m_rom[0x1B5] << 16 |
                         (uint32_t)m_rom[0x1B6] << 8 | m_rom[0x1B7];
  const uint32_t end = (uint32_t)m_rom[0x1B8] << 24 |
                       (uint32_t)m_rom[0x1B9] << 16 |
                       (uint32_t)m_rom[0x1BA] << 8 | m_rom[0x1BB];
  if (end >= start && (uint64_t)end - start < 0x10000) {
    const uint32_t size = end - start + 1;
    m_sram.assign(size, 0);
    m_bus.enable_sram(m_sram.data(), start, size, true);
    m_logger->log(LogLevel::Verbose, "SRAM enabled");
  }
}

int Machine::save_sram(const char *filename)
{
  if (m_sram.empty()) {
    return -1;
  }
  std::ofstream out{filename, std::ios::binary};
  if (!out) {
    return -1;
  }
  out.write(reinterpret_cast<const char *>(m_sram.data()),
            (std::streamsize)m_sram.size());
  return out ? 0 : -1;
}

int Machine::load_sram(const char *filename)
{
  if (m_sram.empty()) {
    return -1;
  }
  std::ifstream in{filename, std::ios::binary};
  if (!in) {
    return -1;
  }
  in.read(reinterpret_cast<char *>(m_sram.data()),
          (std::streamsize)m_sram.size());
  return in ? 0 : -1;
}

void Machine::parse_rom_header()
{
  const uint8_t *rom = m_rom.data();
  m_cartinfo = {};

  header_text(m_cartinfo.console, sizeof(m_cartinfo.console), rom, 0x100, 16);
  header_text(m_cartinfo.copyright, sizeof(m_cartinfo.copyright), rom, 0x110,
              16);
  header_text(m_cartinfo.name_domestic, sizeof(m_cartinfo.name_domestic), rom,
              0x120, 48);
  header_text(m_cartinfo.name_overseas, sizeof(m_cartinfo.name_overseas), rom,
              0x150, 48);

  if (rom[0x180] == 'G' && rom[0x181] == 'M') {
    m_cartinfo.prodtype = pt_game;
  } else if (rom[0x180] == 'A' && rom[0x181] == 'I') {
    m_cartinfo.prodtype = pt_education;
  } else {
    m_cartinfo.prodtype = pt_unknown;
  }

  header_text(m_cartinfo.version, sizeof(m_cartinfo.version), rom, 0x182, 12);
  m_cartinfo.checksum = (uint16_t)((uint16_t)rom[0x18E] << 8 | rom[0x18F]);
  header_text(m_cartinfo.memo, sizeof(m_cartinfo.memo), rom, 0x1C8, 28);

  for (size_t i = 0x1F0; i < 0x1FF; i++) {
    if (rom[i] == 'J') {
      m_cartinfo.flag_japan = 1;
    }
    if (rom[i] == 'U') {
      m_cartinfo.flag_usa = 1;
    }
    if (rom[i] == 'E') {
      m_cartinfo.flag_europe = 1;
    }
  }
  if (rom[0x1F0] >= '1' && rom[0x1F0] <= '9') {
    m_cartinfo.hardware = rom[0x1F0] - '0';
  } else if (rom[0x1F0] >= 'A' && rom[0x1F0] <= 'F') {
    m_cartinfo.hardware = rom[0x1F0] - 'A' + 10;
  }

  char *p = m_cartinfo.country;
  for (size_t i = 0x1F0;
       i < 0x200 && p < m_cartinfo.country + sizeof(m_cartinfo.country) - 1;
       i++) {
    if (rom[i] != 0 && rom[i] != 32) {
      *p++ = (char)rom[i];
    }
  }
  *p = '\0';

  /* Region: Europe-only headers boot as PAL; anything else as NTSC.
   * Japan-only headers boot on a domestic machine. */
  m_pal = m_cartinfo.flag_europe != 0 && m_cartinfo.flag_japan == 0 &&
          m_cartinfo.flag_usa == 0;
  m_domestic = m_cartinfo.flag_japan != 0 && m_cartinfo.flag_usa == 0 &&
               m_cartinfo.flag_europe == 0;
}

}  // namespace generator
