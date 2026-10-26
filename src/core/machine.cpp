/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "machine.hpp"

#include "state_v3.hpp"

#include "gensoundp.h" /* SOUND_SAMPLERATE */
#include "uiplot_cram.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <utility>

#if !defined(_WIN32)
#include <fcntl.h>
#include <unistd.h>
#endif

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

/* Six-button pad TH idle reset. The pad lets go of its toggle counter
 * when TH stays high for more than ~1.5ms; 25 scanlines of 3420 master
 * clocks is the legacy core's measured equivalent (1.59ms NTSC). */
constexpr uint64_t kPadThIdleResetMclk = 25 * 3420;

/* The master crystals: 53.693175 MHz NTSC, 53.203424 MHz PAL. One
 * emulated second advances exactly this many master clocks (3420 per
 * line, 262-line NTSC fields at 59.92 Hz, 313-line PAL fields at
 * 49.70 Hz), keeping the audio sample stream locked to the emulated
 * field rather than to nominal 60/50 — the field-count approximations
 * that preceded this ran the PAL pitch ~0.3% low. */
constexpr uint64_t kMclkPerSecondNtsc = 53693175;
constexpr uint64_t kMclkPerSecondPal = 53203424;
constexpr int32_t kFmGain = 3;     /* +9.5 dB */
constexpr int32_t kPsgGainDiv = 5; /* -14.0 dB */
constexpr int64_t kMaxMix = 32768 * kFmGain + 32768 / kPsgGainDiv;

/* A Genesis image, including a copier header and an overdump, fits in
 * 32 MiB. A v3 state is a few hundred kilobytes; 8 MiB leaves room for
 * every chunk twice over. Both caps are enforced before the file is
 * copied into a vector. */
constexpr std::uintmax_t kMaxRomBytes = 32u * 1024u * 1024u;
constexpr std::uintmax_t kMaxStateBytes = 8u * 1024u * 1024u;
constexpr size_t kMinRomBytes = 0x200;

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
accept_rom(std::vector<uint8_t> rom)
{
  /* The copier-header strip can shrink a file that already passed the
   * input-size check. parse_rom_header reads through 0x1FF, so anything
   * shorter is rejected here instead of walking off the buffer. */
  if (rom.size() < kMinRomBytes) {
    return std::unexpected("ROM image is too small");
  }
  if (rom.size() > kMaxRomBytes) {
    return std::unexpected("ROM image is too large");
  }
  return rom;
}

std::expected<std::vector<uint8_t>, std::string>
normalize_rom(std::span<const uint8_t> image)
{
  if (image.size() > kMaxRomBytes) {
    return std::unexpected("ROM image is too large");
  }
  if (image.size() < kMinRomBytes) {
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
    return accept_rom(std::move(rom));
  }

  size_t offset = 0;
  if (image.size() > 0x300 && has_rom_signature(image, 0x300) &&
      !has_rom_signature(image, 0x100)) {
    offset = 512;
  }
  std::vector<uint8_t> rom(image.begin() + (std::ptrdiff_t)offset, image.end());

  if (rom.size() < kMinRomBytes) {
    return std::unexpected("ROM image is too small");
  }
  if (has_rom_signature(rom, 0x100) &&
      std::memcmp(rom.data() + 0x100, "ESAG", 4) == 0) {
    for (size_t byte = 0; byte + 1 < rom.size(); byte += 2) {
      std::swap(rom[byte], rom[byte + 1]);
    }
  }
  return accept_rom(std::move(rom));
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

/* Write through a sibling temporary and rename it over the target, so a
 * failure mid-write never truncates the previous good state. */
std::expected<void, StateError>
write_file_atomic(const std::filesystem::path &target,
                  std::span<const uint8_t> bytes)
{
  std::error_code ec;
  std::filesystem::path temp = target;
  temp += ".tmp";

  {
    std::ofstream out{temp, std::ios::binary | std::ios::trunc};
    if (!out) {
      return std::unexpected(StateError::Io);
    }
    out.write(reinterpret_cast<const char *>(bytes.data()),
              (std::streamsize)bytes.size());
    out.flush();
    if (!out) {
      out.close();
      std::filesystem::remove(temp, ec);
      return std::unexpected(StateError::Io);
    }
  }

#if !defined(_WIN32)
  const int fd = ::open(temp.c_str(), O_RDONLY);
  if (fd >= 0) {
    (void)::fsync(fd);
    (void)::close(fd);
  }
  std::filesystem::permissions(temp,
                               std::filesystem::perms::owner_read |
                                   std::filesystem::perms::owner_write,
                               std::filesystem::perm_options::replace, ec);
  ec.clear();
#endif

  std::filesystem::rename(temp, target, ec);
#if defined(_WIN32)
  if (ec) {
    /* Windows may refuse to replace an existing file; retry after
     * removing it. POSIX rename is atomic and never needs this. */
    std::filesystem::remove(target, ec);
    ec.clear();
    std::filesystem::rename(temp, target, ec);
  }
#endif
  if (ec) {
    std::filesystem::remove(temp, ec);
    return std::unexpected(StateError::Io);
  }
  return {};
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

  /* Output stage: the Mega Drive 2 puts a first-order RC low-pass ahead
   * of the audio jack — the muffled character the model 1 lacks.
   * Measured cutoffs land around 4.7-5 kHz across MD2 board revisions
   * (the model 1 stage is far gentler, ~9-13 kHz). One pole is enough to
   * place the slope; the DAC itself is already modelled in the chip. */
  m_lp_alpha = 1.0 - std::exp(-2.0 * 3.14159265358979323846 * 4700.0 /
                              (double)SOUND_SAMPLERATE);
}

Machine::~Machine()
{
  flush_battery();
}

std::expected<void, std::string> Machine::load_rom(std::string_view filename)
{
  const std::filesystem::path path{std::string(filename)};
  std::error_code ec;
  const auto file_size = std::filesystem::file_size(path, ec);
  if (ec) {
    return std::unexpected("cannot open ROM file: " + std::string(filename));
  }
  if (file_size > kMaxRomBytes) {
    return std::unexpected("ROM image is too large");
  }

  std::ifstream in{path, std::ios::binary};
  if (!in) {
    return std::unexpected("cannot open ROM file: " + std::string(filename));
  }
  std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
  if (data.size() > kMaxRomBytes) {
    return std::unexpected("ROM image is too large");
  }

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
Machine::load_rom_mem(std::span<const uint8_t> rom_data,
                      std::string_view filename)
{
  auto normalized = normalize_rom(rom_data);
  if (!normalized) {
    return std::unexpected(normalized.error());
  }

  /* Keep the previous cartridge if its battery file cannot be written.
   * A machine with nowhere to put that file (no home directory) drops
   * the dirty flag inside flush_battery and still switches. */
  flush_battery();
  if (m_bus.sram_dirty()) {
    return std::unexpected(std::string(state_error_message(StateError::Io)));
  }

  /* Detach borrowed storage before either vector can reallocate. The new
   * cartridge starts from a cold machine on the next frame. */
  m_bus.attach_rom(nullptr, 0);
  m_bus.disable_sram();
  m_sram.clear();
  m_rom = std::move(*normalized);
  parse_rom_header();
  setup_sram();
  restore_battery();
  m_powered = false;
  m_halted = false;
  m_mclk_total = 0;
  m_frames = 0;
  if (!filename.empty()) {
    const std::string name(filename);
    const size_t slash = name.find_last_of("/\\");
    std::snprintf(m_leafname.data(), m_leafname.size(), "%s",
                  slash == std::string::npos ? name.c_str()
                                             : name.c_str() + slash + 1);
  }
  return {};
}

void Machine::unload_rom()
{
  flush_battery();
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
  m_io_data = {};
  for (auto &in : m_input) {
    in.clear();
  }
  m_pad_counter = {};
  m_pad_th = {1, 1};
  m_pad_edge_mclk = {};
  m_z80_busreq = false;
  m_z80_resume_delay = 0;
  m_z80.power_on_reset(); /* held from power-on; the ROM's boot code
                             releases it through 0xA11200 */
  m_vdp.reset(m_pal);
  m_halted = false;
  m_frames = 0;
  m_audio_acc = 0;
  m_mix_acc_l = 0;
  m_mix_acc_r = 0;
  m_mix_ticks = 0;
  m_lp_l = 0.0;
  m_lp_r = 0.0;
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
    flush_battery();
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
      /* STOP: the CPU is idle, so nothing here consumes 68K cycles --
       * step the rest of the machine on so the VDP can reach the line
       * that raises the interrupt. The step() below is what releases
       * the stop: it is the only place that takes a pending interrupt,
       * so skipping it while stopped leaves the CPU asleep for good. */
      const uint64_t before = m_vdp.frame_index();
      advance_mclk(64);
      m_cpu.step();
      if (m_cpu.stopped() && m_vdp.frame_index() != before) {
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
  flush_battery();
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

void Machine::emit_audio_sample()
{
  /* Windowed mean of the held chip mix (box-filter decimation), then the
   * console's output RC stage as a first-order low-pass. Point-sampling
   * the chips instead would beat the FM's 53267 Hz update rate against
   * SOUND_SAMPLERATE and repeat/skip staircase steps — a periodic
   * clicking that dominates everything else. */
  int32_t out_l = 0;
  int32_t out_r = 0;
  if (m_mix_ticks != 0) {
    const double mean_l = (double)m_mix_acc_l / (double)m_mix_ticks;
    const double mean_r = (double)m_mix_acc_r / (double)m_mix_ticks;
    m_mix_acc_l = 0;
    m_mix_acc_r = 0;
    m_mix_ticks = 0;
    m_lp_l += m_lp_alpha * (mean_l - m_lp_l);
    m_lp_r += m_lp_alpha * (mean_r - m_lp_r);
    out_l = (int32_t)std::lround(m_lp_l);
    out_r = (int32_t)std::lround(m_lp_r);
  }
  m_audio_left.push_back((uint16_t)(int16_t)std::clamp(out_l, -32768, 32767));
  m_audio_right.push_back((uint16_t)(int16_t)std::clamp(out_r, -32768, 32767));
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

void Machine::advance_68k_clocks(uint64_t clocks)
{
  advance_mclk(clocks * 7);
}

void Machine::advance_mclk(uint64_t ticks)
{
  const uint64_t per_second = m_pal ? kMclkPerSecondPal : kMclkPerSecondNtsc;
  constexpr uint64_t kSampleRate = SOUND_SAMPLERATE;

  /* Integrate the held mix only until a chip changes output or a host
   * sample is due. A long DMA can span several chip updates per host
   * sample; holding the old output across those updates distorts the
   * waveform. Use each chip's phase, not the absolute machine clock. */
  while (ticks != 0) {
    const uint64_t scaled_remaining = per_second - m_audio_acc;
    const uint64_t to_sample =
        (scaled_remaining + kSampleRate - 1) / kSampleRate;
    const uint64_t chunk =
        std::min({ticks, to_sample, m_z80bus.ym().mclk_until_output(),
                  m_z80bus.psg().mclk_until_output()});

    /* Cross-chip gain stage. Each core normalises internally for its own
     * headroom (the FM channel sum is /3, the PSG sum >>3), and those
     * constants do not encode the console's analog mix — summing them raw
     * left the FM section ~10 dB below and the PSG ~14 dB above a hardware
     * recording of Comix Zone's intro. Calibrated 2026-10-10 against that
     * recording (tmp/sound-ab-20261010/REPORT.md): the FM gain undoes the
     * core's /3 so the chip's own full scale reaches the mix, and the PSG
     * sits under it the way the console's mixing network puts it there. */
    const int32_t psg = m_z80bus.psg().output() / kPsgGainDiv;
    m_mix_acc_l +=
        (int64_t)(m_z80bus.ym().sample_left() * kFmGain + psg) * (int64_t)chunk;
    m_mix_acc_r += (int64_t)(m_z80bus.ym().sample_right() * kFmGain + psg) *
                   (int64_t)chunk;
    m_mix_ticks += chunk;
    m_mclk_total += chunk;

    const int ipl = m_vdp.advance_mclk(chunk);
    m_cpu.set_ipl(ipl);
    /* The FM chip's /IRQ pin is not routed on this board — the sound CPU's
     * only interrupt source is the VDP. Drivers poll the FM status register
     * for timer overflows instead, so the chip still tracks its own IRQ
     * line but nothing consumes it. */
    m_z80bus.ym().advance_mclk(chunk);
    m_z80bus.psg().advance_mclk(chunk);
    m_z80.set_int(m_vdp.zint());

    uint64_t z80_ticks = chunk;
    if (m_z80_resume_delay > 0) {
      const uint32_t consume =
          (uint32_t)std::min<uint64_t>(m_z80_resume_delay, z80_ticks);
      m_z80_resume_delay -= consume;
      z80_ticks -= consume;
    }
    if (z80_ticks != 0) {
      m_z80.advance_mclk(z80_ticks, m_z80_busreq);
    }

    m_audio_acc += chunk * kSampleRate;
    if (m_audio_acc >= per_second) {
      m_audio_acc -= per_second;
      emit_audio_sample();
    }
    ticks -= chunk;
  }
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

void Machine::update_pad_th(int port, bool th)
{
  if (port >= 2) {
    return;
  }
  const bool previous = m_pad_th[port] != 0;
  m_pad_th[port] = th ? 1 : 0;
  if (!th || previous) {
    return; /* only rising edges advance the handshake */
  }

  /* The pad counts TH rising edges ((c+2)&6: 0,2,4,6). An edge that
   * arrives after the line has been quiet for the idle timeout starts
   * a fresh count instead of continuing one — the real pad resets its
   * counter when TH stays high for more than ~1.5ms. */
  if (m_mclk_total - m_pad_edge_mclk[port] > kPadThIdleResetMclk) {
    m_pad_counter[port] = 2;
  } else {
    m_pad_counter[port] = (uint8_t)((m_pad_counter[port] + 2) & 6);
  }
  m_pad_edge_mclk[port] = m_mclk_total;
}

uint16_t Machine::read_port(int port)
{
  const uint8_t ctrl = m_io_ctrl[port];
  const uint8_t out = m_io_data[port];

  /* 3-button pad protocol. The console
   * drives TH (data bit 6) as an output and the pad multiplexes its six
   * buttons + start over the TL/TR/data lines, active low:
   *   TH=1: D0=up D1=down D2=left D3=right D4=B D5=C
   *   TH=0: D0=up D1=down D2=0  D3=0    D4=A D5=start
   * With TH set as an input the line is pulled high, so the pad sits in
   * the TH=1 phase. Port C has nothing attached; its input lines float
   * high (all released).
   *
   * A six-button pad keeps the same phases during a normal poll. After
   * three quick TH toggle pairs (counter==6, the legacy ControllerPorts
   * model: rising edges walk 0,2,4,6) it answers one extra pair instead
   * — the phase a game reads to detect and drive the shoulder buttons:
   *   TH=1: D0=z   D1=y   D2=x   D3=mode D4=B D5=C
   *   TH=0: D0-D3 = 0 (the six-button signature), D4=A D5=start
   * Counter==4 (one toggle pair into the burst) holds A/start without
   * the direction lines, matching the legacy table. */
  const bool th = (ctrl & 0x40) != 0 ? (out & 0x40) != 0 : true;
  uint8_t counter = 0;
  if (port < 2) {
    if (m_mclk_total - m_pad_edge_mclk[port] > kPadThIdleResetMclk) {
      m_pad_counter[port] = 0; /* quiet line: the pad has let go */
    }
    counter = m_pad_counter[port];
  }

  uint8_t pad = 0x3F; /* bits 0-5 released */
  if (port < 2) {
    const InputState &in = m_input[port];
    if (in.up.load(std::memory_order_relaxed) != 0) {
      pad &= (uint8_t)~0x01;
    }
    if (in.down.load(std::memory_order_relaxed) != 0) {
      pad &= (uint8_t)~0x02;
    }
    if (th) {
      if (counter == 6) { /* six-button extra phase */
        if (in.z.load(std::memory_order_relaxed) != 0) {
          pad &= (uint8_t)~0x01;
        }
        if (in.y.load(std::memory_order_relaxed) != 0) {
          pad &= (uint8_t)~0x02;
        }
        if (in.x.load(std::memory_order_relaxed) != 0) {
          pad &= (uint8_t)~0x04;
        }
        if (in.mode.load(std::memory_order_relaxed) != 0) {
          pad &= (uint8_t)~0x08;
        }
      } else {
        if (in.left.load(std::memory_order_relaxed) != 0) {
          pad &= (uint8_t)~0x04;
        }
        if (in.right.load(std::memory_order_relaxed) != 0) {
          pad &= (uint8_t)~0x08;
        }
      }
      if (in.b.load(std::memory_order_relaxed) != 0) {
        pad &= (uint8_t)~0x10;
      }
      if (in.c.load(std::memory_order_relaxed) != 0) {
        pad &= (uint8_t)~0x20;
      }
    } else {
      if (counter == 6) { /* six-button signature: low nibble forced 0 */
        pad &= (uint8_t)~0x0F;
      } else if (counter == 4) {
        pad &= (uint8_t)~0x0F; /* A/start only, no direction lines */
      } else {
        pad &= (uint8_t)~0x0C; /* left/right lines forced low */
      }
      if (in.a.load(std::memory_order_relaxed) != 0) {
        pad &= (uint8_t)~0x10;
      }
      if (in.start.load(std::memory_order_relaxed) != 0) {
        pad &= (uint8_t)~0x20;
      }
    }
  }

  /* Lines the console drives as outputs read back the written level;
   * input lines read the pad. TH as an input reads its pull-up. */
  uint8_t v = 0;
  for (int bit = 0; bit < 7; bit++) {
    const uint8_t mask = (uint8_t)(1u << bit);
    if ((ctrl & mask) != 0) {
      v |= out & mask;
    } else {
      v |= (bit < 6 ? pad : 0x40) & mask;
    }
  }
  return v; /* bit 7: no line, reads 0 */
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
    case 1:
      /* port A data: player 1 pad through the TH multiplexer */
      return read_port(0);
    case 2:
      /* port B data: player 2 pad */
      return read_port(1);
    case 3:
      /* port C data: unattached, lines float high */
      return read_port(2);
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
  if (page == 0x0000) {
    const uint32_t reg = (addr >> 1) & 7;
    if (reg >= 1 && reg <= 3) {
      /* Data register: latches the output level of every line the ctrl
       * register has as an output. Games drive TH from here (bit 6) to
       * step the pad multiplexer through its phases; a driven TH edge
       * feeds the six-button handshake. Byte writes arrive normalised
       * into both halves by the CPU core. */
      const int port = (int)reg - 1;
      const uint8_t value = (uint8_t)(data & 0xFF);
      if (port < 2 && (m_io_ctrl[port] & 0x40) != 0) {
        update_pad_th(port, (value & 0x40) != 0);
      }
      m_io_data[port] = value;
    } else if (reg >= 4) {
      m_io_ctrl[reg - 4] = (uint8_t)(data & 0xFF);
    }
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

std::expected<void, StateError> Machine::save_state(std::string_view filename)
{
  return save_state_impl(filename, -1);
}

std::expected<std::vector<uint8_t>, StateError> Machine::capture_state()
{
  return capture_state(-1);
}

std::expected<std::vector<uint8_t>, StateError> Machine::capture_state(int slot)
{
  if (m_rom.empty()) {
    return std::unexpected(StateError::NotLoaded);
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

  StateChunk z80bus_chunk{fourcc("ZBNK"), {}};
  {
    ChunkWriter w;
    w.u32(m_z80bus.bank());
    w.u8(m_z80.reset_line() ? 1 : 0);
    z80bus_chunk.data = w.take_payload();
  }

  StateChunk vdp_chunk{fourcc("VDP0"), {}};
  {
    const Vdp::SavedState v = m_vdp.save();
    ChunkWriter w;
    w.bytes(v.reg);
    w.bytes(v.vram);
    for (const uint16_t entry : v.cram) {
      w.u16(entry);
    }
    w.bytes(v.cram_dirty);
    for (const uint16_t entry : v.vsram) {
      w.u16(entry);
    }
    w.u32(v.address);
    w.u32(v.cd);
    w.u8(v.latch_second);
    w.u8(v.pending_read);
    w.u16(v.read_buffer);
    w.u8(v.dma_pending);
    w.u8(v.pal);
    w.u64(v.mclk);
    w.u32(v.dot);
    w.u32(v.line);
    w.u64(v.frames);
    w.u8(v.hint_counter);
    w.u8(v.vint_pending);
    w.u8(v.vint_flag);
    w.u8(v.hint_pending);
    w.u64(v.dma_debt_mclk);
    w.u8(v.dma_busy);
    w.u8(v.dma_fill_armed);
    vdp_chunk.data = w.take_payload();
  }

  StateChunk ym_chunk{fourcc("YM34"), {}};
  {
    const Ym3438::SavedState y = m_z80bus.ym().save();
    ChunkWriter w;
    for (const auto &bank : y.regs) {
      w.bytes(bank);
    }
    w.u8(y.latch_addr[0]);
    w.u8(y.latch_addr[1]);
    for (const Ym3438::ChannelState &c : y.channels) {
      for (const Ym3438::SlotState &s : c.slots) {
        w.u32(s.phase);
        w.u32((uint32_t)s.incr);
        w.u8(s.incr_dirty);
        w.u32((uint32_t)s.volume);
        w.u8(s.eg_state);
        w.u32((uint32_t)s.delta_attack);
        w.u32((uint32_t)s.delta_decay);
        w.u32((uint32_t)s.delta_sustain);
        w.u32((uint32_t)s.delta_release);
        w.u32((uint32_t)s.sustain_level);
        w.u32((uint32_t)s.total_level);
        w.u8(s.key_scale);
        w.u8(s.ssg_control);
        w.u8(s.ssg_inverted);
        w.u8(s.keyed);
        w.u8(s.am_enabled);
        w.u32(s.am_depth);
      }
      w.u8(c.algorithm);
      w.u8(c.feedback);
      w.u32((uint32_t)c.op1_history[0]);
      w.u32((uint32_t)c.op1_history[1]);
      w.u8(c.key_code);
      w.u8(c.pms);
      w.u32(c.ams);
      w.u8(c.pan_left);
      w.u8(c.pan_right);
      w.u8(c.pitch_dirty);
    }
    w.u8(y.status);
    w.u32(y.busy_mclk);
    w.u64((uint64_t)y.timer_a);
    w.u64((uint64_t)y.timer_b);
    w.u8(y.irq);
    w.u32(y.lfo_count);
    w.u32(y.lfo_incr);
    w.u32(y.lfo_am);
    w.u32((uint32_t)y.lfo_pm);
    w.u8(y.dac_enabled);
    w.u32((uint32_t)y.dac_output);
    w.u16((uint16_t)y.sample_l);
    w.u16((uint16_t)y.sample_r);
    w.u64(y.sample_timer);
    ym_chunk.data = w.take_payload();
  }

  StateChunk psg_chunk{fourcc("PSG0"), {}};
  {
    const Psg::SavedState p = m_z80bus.psg().save();
    ChunkWriter w;
    for (const uint16_t v : p.counter) {
      w.u16(v);
    }
    for (const uint16_t v : p.period) {
      w.u16(v);
    }
    w.bytes(p.atten);
    w.bytes(p.output_bit);
    w.u8(p.latch_channel);
    w.u8(p.latch_volume);
    w.u16(p.lfsr);
    w.u8(p.noise_mode);
    w.u8(p.noise_rate);
    w.u16((uint16_t)p.output_value);
    w.u64(p.mclk_acc);
    psg_chunk.data = w.take_payload();
  }

  StateChunk io_chunk{fourcc("IOCH"), {}};
  {
    ChunkWriter w;
    w.bytes(m_io_ctrl);
    w.bytes(m_io_data);
    w.u8(m_pad_counter[0]);
    w.u8(m_pad_counter[1]);
    w.u8(m_pad_th[0]);
    w.u8(m_pad_th[1]);
    w.u64(m_pad_edge_mclk[0]);
    w.u64(m_pad_edge_mclk[1]);
    w.u8(m_z80_busreq ? 1 : 0);
    w.u32(m_z80_resume_delay);
    io_chunk.data = w.take_payload();
  }

  StateChunk audio_chunk{fourcc("AUD0"), {}};
  {
    ChunkWriter w;
    w.u64(m_audio_acc);
    w.u64((uint64_t)m_mix_acc_l);
    w.u64((uint64_t)m_mix_acc_r);
    w.u64(m_mix_ticks);
    w.u64(std::bit_cast<uint64_t>(m_lp_l));
    w.u64(std::bit_cast<uint64_t>(m_lp_r));
    audio_chunk.data = w.take_payload();
  }

  /* Descriptive metadata. Deliberately deterministic: the wall-clock save
   * time lives in the file mtime, so a load followed by an immediate save
   * still reproduces the same blob byte for byte. */
  StateChunk meta_chunk{fourcc("META"), {}};
  {
    ChunkWriter w;
    w.u16(1); /* META payload version */
    const std::string_view version = VERSION;
    w.u16((uint16_t)version.size());
    w.bytes(std::span<const uint8_t>{
        reinterpret_cast<const uint8_t *>(version.data()), version.size()});
#if defined(NDEBUG)
    w.u8(0); /* release */
#else
    w.u8(1); /* debug */
#endif
#if defined(_WIN32)
    w.u8(1); /* platform */
#elif defined(__APPLE__)
    w.u8(2);
#else
    w.u8(3);
#endif
    w.u64((uint64_t)m_rom.size());
    w.u64(rom_id());
    const char *title = m_cartinfo.name_domestic[0] != '\0'
                            ? m_cartinfo.name_domestic
                            : m_cartinfo.name_overseas;
    const std::string_view title_view{title};
    w.u16((uint16_t)title_view.size());
    w.bytes(std::span<const uint8_t>{
        reinterpret_cast<const uint8_t *>(title_view.data()),
        title_view.size()});
    w.u8((uint8_t)((m_cartinfo.flag_japan != 0) |
                   ((m_cartinfo.flag_usa != 0) << 1) |
                   ((m_cartinfo.flag_europe != 0) << 2)));
    w.u32(slot >= 0 ? (uint32_t)slot : 0xFFFFFFFFu);
    meta_chunk.data = w.take_payload();
  }

  /* Optional trailing integrity chunk; the payload is patched after
   * serialization so it can cover the completed container. */
  StateChunk checksum_chunk{fourcc("CHKS"), std::vector<uint8_t>(4, 0)};

  const StateChunk chunks[] = {
      std::move(board_chunk), std::move(rom_chunk),     std::move(cpu_chunk),
      std::move(ram_chunk),   std::move(zram_chunk),    std::move(z80bus_chunk),
      std::move(z80_chunk),   std::move(vdp_chunk),     std::move(ym_chunk),
      std::move(psg_chunk),   std::move(io_chunk),      std::move(audio_chunk),
      std::move(meta_chunk),  std::move(checksum_chunk)};
  std::vector<uint8_t> blob = StateV3::serialize(chunks);

  const uint32_t checksum = state_crc32(
      std::span<const uint8_t>{blob.data(), blob.size() - sizeof(uint32_t)});
  const size_t checksum_at = blob.size() - sizeof(uint32_t);
  blob[checksum_at] = (uint8_t)(checksum & 0xff);
  blob[checksum_at + 1] = (uint8_t)((checksum >> 8) & 0xff);
  blob[checksum_at + 2] = (uint8_t)((checksum >> 16) & 0xff);
  blob[checksum_at + 3] = (uint8_t)((checksum >> 24) & 0xff);

  return blob;
}

std::expected<void, StateError>
Machine::save_state_impl(std::string_view filename, int slot)
{
  if (filename.empty()) {
    return std::unexpected(StateError::Io);
  }
  auto blob = capture_state(slot);
  if (!blob) {
    return std::unexpected(blob.error());
  }
  return write_file_atomic(std::filesystem::path{std::string(filename)}, *blob);
}

std::expected<void, StateError> Machine::load_state(std::string_view filename)
{
  if (m_rom.empty()) {
    return std::unexpected(StateError::NotLoaded);
  }
  if (filename.empty()) {
    return std::unexpected(StateError::Io);
  }

  const std::filesystem::path path{std::string(filename)};
  std::error_code ec;
  const auto file_size = std::filesystem::file_size(path, ec);
  if (ec) {
    return std::unexpected(StateError::Io);
  }
  if (file_size > kMaxStateBytes) {
    return std::unexpected(StateError::Corrupt);
  }

  std::ifstream in{path, std::ios::binary};
  if (!in) {
    return std::unexpected(StateError::Io);
  }
  std::vector<uint8_t> blob((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
  if (blob.size() > kMaxStateBytes) {
    return std::unexpected(StateError::Corrupt);
  }
  auto chunks = StateV3::deserialize(blob);
  if (!chunks) {
    return std::unexpected(chunks.error() == StateV3Error::UnsupportedVersion
                               ? StateError::UnsupportedVersion
                               : StateError::Corrupt);
  }

  /* Validate ROM identity before applying anything: a state for a different
   * game must leave the running machine untouched. */
  bool rom_seen = false;
  bool rom_matches = false;
  for (const StateChunk &chunk : *chunks) {
    if (chunk.id != fourcc("ROMI")) {
      continue;
    }
    if (rom_seen) {
      return std::unexpected(StateError::Corrupt);
    }
    ChunkReader r{chunk.data};
    const uint64_t rom_hash = r.u64();
    const uint32_t rom_size = r.u32();
    if (!r.ok()) {
      return std::unexpected(StateError::Corrupt);
    }
    rom_seen = true;
    rom_matches = rom_seen && rom_size == m_rom.size() &&
                  rom_hash == fnv1a64(m_rom.data(), m_rom.size());
  }
  if (!rom_seen) {
    return std::unexpected(StateError::Corrupt);
  }
  if (!rom_matches) {
    return std::unexpected(StateError::WrongRom);
  }

  bool board_seen = false, cpu_seen = false;
  bool vdp_seen = false;
  bool state_pal = false;
  const auto read_chunk = [&](const StateChunk &chunk, bool apply) {
    ChunkReader r{chunk.data};
    if (chunk.id == fourcc("BORD")) {
      const uint32_t frames = r.u32();
      const uint64_t mclk_total = r.u64();
      const uint8_t pal = r.u8();
      const uint8_t halted = r.u8();
      board_seen = r.ok();
      if (pal > 1 || halted > 1) {
        return false;
      }
      state_pal = pal != 0;
      if (apply && board_seen) {
        m_frames = frames;
        m_mclk_total = mclk_total;
        m_pal = pal != 0;
        m_halted = halted != 0;
      }
    } else if (chunk.id == fourcc("M68K")) {
      M68k::SavedState cpu{};
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
      if (cpu.queue_len > 2 ||
          cpu.fault_kind > (uint8_t)M68k::Fault::Kind::Stopped) {
        return false;
      }
      if (apply && cpu_seen) {
        m_cpu.restore(cpu);
      }
    } else if (chunk.id == fourcc("W68K")) {
      const std::span<const uint8_t> data = r.bytes(m_ram.size());
      if (apply && r.ok()) {
        std::memcpy(m_ram.data(), data.data(), m_ram.size());
      }
    } else if (chunk.id == fourcc("WZ80")) {
      const std::span<const uint8_t> data = r.bytes(m_z80bus.ram().size());
      if (apply && r.ok()) {
        std::memcpy(m_z80bus.ram().data(), data.data(), m_z80bus.ram().size());
      }
    } else if (chunk.id == fourcc("Z80C")) {
      z80f::Snapshot snap;
      z80f::Registers &r0 = snap.registers;
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
      if (apply && r.ok()) {
        m_z80.restore(snap);
        m_z80.restore_tstates(tstates);
      }
    } else if (chunk.id == fourcc("ZBNK")) {
      const uint32_t bank = r.u32();
      const uint8_t reset_asserted = r.u8();
      if (apply && r.ok()) {
        m_z80bus.set_bank(bank);
        /* Ordered ahead of Z80C: a rising reset edge clears the core, and
         * the snapshot restore then overwrites it with the saved one. */
        m_z80.reset_line(reset_asserted != 0);
      }
    } else if (chunk.id == fourcc("VDP0")) {
      Vdp::SavedState v{};
      const std::span<const uint8_t> reg = r.bytes(sizeof(v.reg));
      if (!reg.empty()) {
        std::memcpy(v.reg, reg.data(), sizeof(v.reg));
      }
      const std::span<const uint8_t> vram = r.bytes(sizeof(v.vram));
      if (!vram.empty()) {
        std::memcpy(v.vram, vram.data(), sizeof(v.vram));
      }
      for (uint16_t &entry : v.cram) {
        entry = r.u16();
      }
      const std::span<const uint8_t> cram_dirty = r.bytes(sizeof(v.cram_dirty));
      if (!cram_dirty.empty()) {
        std::memcpy(v.cram_dirty, cram_dirty.data(), sizeof(v.cram_dirty));
      }
      for (uint16_t &entry : v.vsram) {
        entry = r.u16();
      }
      v.address = r.u32();
      v.cd = r.u32();
      v.latch_second = r.u8();
      v.pending_read = r.u8();
      v.read_buffer = r.u16();
      v.dma_pending = r.u8();
      v.pal = r.u8();
      v.mclk = r.u64();
      v.dot = r.u32();
      v.line = r.u32();
      v.frames = r.u64();
      v.hint_counter = r.u8();
      v.vint_pending = r.u8();
      v.vint_flag = r.u8();
      v.hint_pending = r.u8();
      v.dma_debt_mclk = r.u64();
      v.dma_busy = r.u8();
      v.dma_fill_armed = r.u8();
      /* mclk, dot, line and DMA debt are bounded so a crafted state
       * cannot spin advance_mclk. A value this build can write passes. */
      if (r.ok() && !Vdp::valid_saved_state(v)) {
        return false;
      }
      if (apply && r.ok()) {
        m_vdp.restore(v);
        vdp_seen = true;
      }
    } else if (chunk.id == fourcc("YM34")) {
      Ym3438::SavedState y{};
      for (auto &bank : y.regs) {
        const std::span<const uint8_t> regs = r.bytes(sizeof(bank));
        if (!regs.empty()) {
          std::memcpy(bank, regs.data(), sizeof(bank));
        }
      }
      y.latch_addr[0] = r.u8();
      y.latch_addr[1] = r.u8();
      for (Ym3438::ChannelState &c : y.channels) {
        for (Ym3438::SlotState &s : c.slots) {
          s.phase = r.u32();
          s.incr = (int32_t)r.u32();
          s.incr_dirty = r.u8();
          s.volume = (int32_t)r.u32();
          s.eg_state = r.u8();
          s.delta_attack = (int32_t)r.u32();
          s.delta_decay = (int32_t)r.u32();
          s.delta_sustain = (int32_t)r.u32();
          s.delta_release = (int32_t)r.u32();
          s.sustain_level = (int32_t)r.u32();
          s.total_level = (int32_t)r.u32();
          s.key_scale = r.u8();
          s.ssg_control = r.u8();
          s.ssg_inverted = r.u8();
          s.keyed = r.u8();
          s.am_enabled = r.u8();
          s.am_depth = r.u32();
        }
        c.algorithm = r.u8();
        c.feedback = r.u8();
        c.op1_history[0] = (int32_t)r.u32();
        c.op1_history[1] = (int32_t)r.u32();
        c.key_code = r.u8();
        c.pms = r.u8();
        c.ams = r.u32();
        c.pan_left = r.u8();
        c.pan_right = r.u8();
        c.pitch_dirty = r.u8();
      }
      y.status = r.u8();
      y.busy_mclk = r.u32();
      y.timer_a = (int64_t)r.u64();
      y.timer_b = (int64_t)r.u64();
      y.irq = r.u8();
      y.lfo_count = r.u32();
      y.lfo_incr = r.u32();
      y.lfo_am = r.u32();
      y.lfo_pm = (int32_t)r.u32();
      y.dac_enabled = r.u8();
      y.dac_output = (int32_t)r.u32();
      y.sample_l = (int16_t)r.u16();
      y.sample_r = (int16_t)r.u16();
      y.sample_timer = r.u64();
      if (!Ym3438::valid_saved_state(y)) {
        return false;
      }
      if (apply && r.ok()) {
        m_z80bus.ym().restore(y);
      }
    } else if (chunk.id == fourcc("PSG0")) {
      Psg::SavedState p{};
      for (uint16_t &v : p.counter) {
        v = r.u16();
      }
      for (uint16_t &v : p.period) {
        v = r.u16();
      }
      const std::span<const uint8_t> atten = r.bytes(sizeof(p.atten));
      if (!atten.empty()) {
        std::memcpy(p.atten, atten.data(), sizeof(p.atten));
      }
      const std::span<const uint8_t> output_bit = r.bytes(sizeof(p.output_bit));
      if (!output_bit.empty()) {
        std::memcpy(p.output_bit, output_bit.data(), sizeof(p.output_bit));
      }
      p.latch_channel = r.u8();
      p.latch_volume = r.u8();
      p.lfsr = r.u16();
      p.noise_mode = r.u8();
      p.noise_rate = r.u8();
      p.output_value = (int16_t)r.u16();
      p.mclk_acc = r.u64();
      if (apply && r.ok()) {
        m_z80bus.psg().restore(p);
      }
    } else if (chunk.id == fourcc("IOCH")) {
      const std::span<const uint8_t> io_ctrl = r.bytes(m_io_ctrl.size());
      const std::span<const uint8_t> io_data = r.bytes(m_io_data.size());
      const uint8_t pad_counter0 = r.u8();
      const uint8_t pad_counter1 = r.u8();
      const uint8_t pad_th0 = r.u8();
      const uint8_t pad_th1 = r.u8();
      const uint64_t pad_edge0 = r.u64();
      const uint64_t pad_edge1 = r.u64();
      const uint8_t busreq = r.u8();
      const uint32_t resume_delay = r.u32();
      if (apply && r.ok()) {
        std::memcpy(m_io_ctrl.data(), io_ctrl.data(), m_io_ctrl.size());
        std::memcpy(m_io_data.data(), io_data.data(), m_io_data.size());
        m_pad_counter[0] = pad_counter0;
        m_pad_counter[1] = pad_counter1;
        m_pad_th[0] = pad_th0;
        m_pad_th[1] = pad_th1;
        m_pad_edge_mclk[0] = pad_edge0;
        m_pad_edge_mclk[1] = pad_edge1;
        m_z80_busreq = busreq != 0;
        m_z80_resume_delay = resume_delay;
      }
    } else if (chunk.id == fourcc("AUD0")) {
      const uint64_t audio_acc = r.u64();
      const uint64_t mix_acc_l = r.u64();
      const uint64_t mix_acc_r = r.u64();
      const uint64_t mix_ticks = r.u64();
      const uint64_t lp_l = r.u64();
      const uint64_t lp_r = r.u64();
      /* A v3 state does not encode the host sample rate. Allow the longest
       * sample interval supported by the build options (44100 Hz), but
       * reject accumulators that cannot come from the bounded chip outputs.
       * Check IEEE-754 magnitude bits so release -ffast-math cannot remove
       * the NaN/infinity rejection. */
      constexpr uint64_t kMaxMixTicks = (kMclkPerSecondNtsc + 44099) / 44100;
      constexpr uint64_t kMagnitude = 0x7FFFFFFFFFFFFFFFULL;
      constexpr uint64_t kMaxFilter = std::bit_cast<uint64_t>((double)kMaxMix);
      if (audio_acc >= (state_pal ? kMclkPerSecondPal : kMclkPerSecondNtsc) ||
          mix_ticks > kMaxMixTicks || (lp_l & kMagnitude) > kMaxFilter ||
          (lp_r & kMagnitude) > kMaxFilter) {
        return false;
      }
      const int64_t mix_limit = (int64_t)mix_ticks * kMaxMix;
      if ((int64_t)mix_acc_l < -mix_limit || (int64_t)mix_acc_l > mix_limit ||
          (int64_t)mix_acc_r < -mix_limit || (int64_t)mix_acc_r > mix_limit) {
        return false;
      }
      if (apply && r.ok()) {
        m_audio_acc = audio_acc;
        m_mix_acc_l = (int64_t)mix_acc_l;
        m_mix_acc_r = (int64_t)mix_acc_r;
        m_mix_ticks = mix_ticks;
        m_lp_l = std::bit_cast<double>(lp_l);
        m_lp_r = std::bit_cast<double>(lp_r);
      }
    }
    /* Unknown chunk ids are skipped: forward compatibility within v3. */
    return r.ok();
  };

  /* Apply in a fixed order, not file order: ZBNK must precede Z80C so a
   * crafted state cannot reorder the reset edge after the register restore. */
  static constexpr uint32_t kApplyOrder[] = {
      fourcc("BORD"), fourcc("M68K"), fourcc("W68K"), fourcc("WZ80"),
      fourcc("ZBNK"), fourcc("Z80C"), fourcc("VDP0"), fourcc("YM34"),
      fourcc("PSG0"), fourcc("IOCH"), fourcc("AUD0")};
  /* Parse every known payload before touching the live machine. Reuse the
   * same reader in both passes so validation cannot drift from application.
   * Missing optional chunks remain compatible with early v3 states. */
  for (const uint32_t id : kApplyOrder) {
    bool seen = false;
    for (const StateChunk &chunk : *chunks) {
      if (chunk.id == id) {
        if (seen || !read_chunk(chunk, false)) {
          return std::unexpected(StateError::Corrupt);
        }
        seen = true;
      }
    }
  }
  if (!board_seen || !cpu_seen) {
    return std::unexpected(StateError::Corrupt);
  }

  for (const uint32_t id : kApplyOrder) {
    for (const StateChunk &chunk : *chunks) {
      if (chunk.id == id) {
        (void)read_chunk(chunk, true);
        break;
      }
    }
  }
  if (!vdp_seen) {
    /* States written before the VDP0 chunk existed carry no video state;
     * re-sync at least the field timing with the board's standard. */
    m_vdp.reset(m_pal);
  }
  m_powered = true;
  m_bus.reset();
  m_bus.attach_rom(m_rom.data(), m_rom.size());
  m_bus.attach_ram(m_ram.data());
  return {};
}

/* ------------------------------------------------------------------ */
/* save-state slots                                                    */
/* ------------------------------------------------------------------ */

uint64_t Machine::rom_id() const
{
  return fnv1a64(m_rom.data(), m_rom.size());
}

std::filesystem::path Machine::user_data_dir() const
{
  std::filesystem::path base;
#if defined(_WIN32)
  const char *local = std::getenv("LOCALAPPDATA");
  if (local != nullptr && *local != '\0') {
    base = local;
  }
#endif
  if (base.empty()) {
    const char *xdg = std::getenv("XDG_DATA_HOME");
    if (xdg != nullptr && *xdg != '\0') {
      base = xdg;
    }
  }
  if (base.empty()) {
#if defined(_WIN32)
    const char *home = std::getenv("USERPROFILE");
    if (home != nullptr && *home != '\0') {
      base = home;
    }
#else
    const char *home = std::getenv("HOME");
    if (home != nullptr && *home != '\0') {
      base = std::filesystem::path(home) / ".local" / "share";
    }
#endif
  }
  return base;
}

std::filesystem::path Machine::rom_state_dir() const
{
  const std::filesystem::path base = user_data_dir();
  if (base.empty() || m_rom.empty()) {
    return {};
  }
  char id[17];
  std::snprintf(id, sizeof(id), "%016llx", (unsigned long long)rom_id());
  return base / "generator" / "states" / id;
}

std::filesystem::path Machine::state_slot_path(int slot) const
{
  const std::filesystem::path dir = rom_state_dir();
  if (dir.empty()) {
    return {};
  }
  return dir / ("slot" + std::to_string(slot) + ".gst");
}

std::filesystem::path Machine::battery_path() const
{
  const std::filesystem::path dir = rom_state_dir();
  if (dir.empty()) {
    return {};
  }
  return dir / "battery.sram";
}

std::expected<void, StateError> Machine::save_state_slot(int slot)
{
  if (slot < 0 || slot >= kStateSlotCount) {
    return std::unexpected(StateError::InvalidSlot);
  }
  if (m_rom.empty()) {
    return std::unexpected(StateError::NotLoaded);
  }

  const std::filesystem::path path = state_slot_path(slot);
  if (path.empty()) {
    return std::unexpected(StateError::Io);
  }

  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) {
    return std::unexpected(StateError::Io);
  }
#if !defined(_WIN32)
  std::filesystem::permissions(path.parent_path(),
                               std::filesystem::perms::owner_all,
                               std::filesystem::perm_options::replace, ec);
  ec.clear();
#endif

  const std::string name = path.string();
  return save_state_impl(name, slot);
}

std::expected<void, StateError> Machine::load_state_slot(int slot)
{
  if (slot < 0 || slot >= kStateSlotCount) {
    return std::unexpected(StateError::InvalidSlot);
  }
  if (m_rom.empty()) {
    return std::unexpected(StateError::NotLoaded);
  }

  const std::filesystem::path path = state_slot_path(slot);
  if (path.empty()) {
    return std::unexpected(StateError::Io);
  }
  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) {
    return std::unexpected(StateError::NoSlot);
  }

  const std::string name = path.string();
  return load_state(name);
}

time_t Machine::state_slot_date(int slot) const
{
  if (slot < 0 || slot >= kStateSlotCount) {
    return 0;
  }
  const std::filesystem::path path = state_slot_path(slot);
  if (path.empty()) {
    return 0;
  }

  std::error_code ec;
  const std::filesystem::file_time_type file_time =
      std::filesystem::last_write_time(path, ec);
  if (ec) {
    return 0;
  }
  const auto system_time =
      std::chrono::time_point_cast<std::chrono::system_clock::duration>(
          file_time - std::filesystem::file_time_type::clock::now() +
          std::chrono::system_clock::now());
  return std::chrono::system_clock::to_time_t(system_time);
}

/* ------------------------------------------------------------------ */
/* misc API                                                            */
/* ------------------------------------------------------------------ */

void Machine::set_input(int player, unsigned int up, unsigned int down,
                        unsigned int left, unsigned int right,
                        unsigned int start, unsigned int a, unsigned int b,
                        unsigned int c, unsigned int x, unsigned int y,
                        unsigned int z, unsigned int mode)
{
  if (player < 0 || player > 1) {
    return;
  }
  InputState &in = m_input[player];
  in.up.store(up != 0, std::memory_order_relaxed);
  in.down.store(down != 0, std::memory_order_relaxed);
  in.left.store(left != 0, std::memory_order_relaxed);
  in.right.store(right != 0, std::memory_order_relaxed);
  in.start.store(start != 0, std::memory_order_relaxed);
  in.a.store(a != 0, std::memory_order_relaxed);
  in.b.store(b != 0, std::memory_order_relaxed);
  in.c.store(c != 0, std::memory_order_relaxed);
  in.x.store(x != 0, std::memory_order_relaxed);
  in.y.store(y != 0, std::memory_order_relaxed);
  in.z.store(z != 0, std::memory_order_relaxed);
  in.mode.store(mode != 0, std::memory_order_relaxed);
}

void Machine::debug_log_zram_to(const char *path)
{
  if (path == nullptr) {
    return;
  }
  std::FILE *f = std::fopen(path, "w");
  if (f == nullptr) {
    return;
  }
  m_z80bus.set_ram_write_sink([this, f](uint16_t addr, uint8_t value) {
    std::fprintf(f, "%llu %04x %02x\n", (unsigned long long)m_mclk_total, addr,
                 value);
  });
  /* The sink closes with the machine; the Z80Bus clears it on reset. */
  m_zram_log.reset(f);
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

std::expected<void, StateError> Machine::save_sram()
{
  if (m_rom.empty()) {
    return std::unexpected(StateError::NotLoaded);
  }
  if (m_sram.empty()) {
    return {};
  }
  const std::filesystem::path path = battery_path();
  if (path.empty()) {
    return std::unexpected(StateError::Io);
  }

  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) {
    return std::unexpected(StateError::Io);
  }
#if !defined(_WIN32)
  std::filesystem::permissions(path.parent_path(),
                               std::filesystem::perms::owner_all,
                               std::filesystem::perm_options::replace, ec);
  ec.clear();
#endif

  auto written = write_file_atomic(path, m_sram);
  if (!written) {
    return written;
  }
  m_bus.clear_sram_dirty();
  return {};
}

std::expected<void, StateError> Machine::load_sram()
{
  if (m_rom.empty()) {
    return std::unexpected(StateError::NotLoaded);
  }
  if (m_sram.empty()) {
    return {};
  }
  const std::filesystem::path path = battery_path();
  if (path.empty()) {
    return std::unexpected(StateError::Io);
  }

  std::error_code ec;
  if (!std::filesystem::exists(path, ec) || ec) {
    return std::unexpected(StateError::NoSlot);
  }
  const auto file_size = std::filesystem::file_size(path, ec);
  if (ec || file_size != m_sram.size()) {
    return std::unexpected(StateError::Corrupt);
  }

  std::ifstream in{path, std::ios::binary};
  if (!in) {
    return std::unexpected(StateError::Io);
  }
  std::vector<uint8_t> bytes(m_sram.size());
  in.read(reinterpret_cast<char *>(bytes.data()),
          (std::streamsize)bytes.size());
  if (!in) {
    return std::unexpected(StateError::Io);
  }
  std::memcpy(m_sram.data(), bytes.data(), bytes.size());
  m_bus.clear_sram_dirty();
  return {};
}

void Machine::flush_battery()
{
  if (!m_bus.sram_dirty() || m_sram.empty()) {
    return;
  }
  /* No home directory: nothing can be persisted, and retrying every
   * frame would not create one. */
  if (battery_path().empty()) {
    m_bus.clear_sram_dirty();
    return;
  }
  if (save_sram()) {
    m_bus.clear_sram_dirty();
  }
}

void Machine::restore_battery()
{
  if (m_sram.empty()) {
    return;
  }
  const auto loaded = load_sram();
  if (!loaded && loaded.error() != StateError::NoSlot) {
    m_logger->log(LogLevel::Verbose, "SRAM image ignored");
  }
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
