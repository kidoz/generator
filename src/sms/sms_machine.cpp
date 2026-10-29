/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "sms_machine.hpp"

#include "core/media_detect.hpp"
#include "core/state_files.hpp"

#include "gensoundp.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <system_error>
#include <utility>

namespace generator {

namespace {

constexpr uint64_t kMclkNtsc = 53693175;
constexpr uint64_t kMclkPal = 53203424;
constexpr std::uintmax_t kMaxStateBytes = 8u * 1024u * 1024u;

void set_leafname(std::array<char, 128> &out, std::string_view filename)
{
  const size_t slash = filename.find_last_of("/\\");
  const std::string leaf = slash == std::string_view::npos
                               ? std::string(filename)
                               : std::string(filename.substr(slash + 1));
  std::snprintf(out.data(), out.size(), "%s", leaf.c_str());
}

void write_z80(ChunkWriter &w, const z80f::Snapshot &snapshot)
{
  const z80f::Registers &r = snapshot.registers;
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
  w.u8(r.iff1 ? 1 : 0);
  w.u8(r.iff2 ? 1 : 0);
  w.u8(r.im);
  w.u8(r.halted ? 1 : 0);
  w.u8(r.ei_pending ? 1 : 0);
  w.u8(r.q);
  w.u64(snapshot.cycle_counter);
  w.u64(snapshot.base_cycle_counter);
  w.u8(snapshot.nmi_line ? 1 : 0);
  w.u8(snapshot.nmi_pending ? 1 : 0);
  w.u8(snapshot.int_line ? 1 : 0);
  w.u8(snapshot.int_pulse_pending ? 1 : 0);
}

z80f::Snapshot read_z80(ChunkReader &r)
{
  z80f::Snapshot snapshot;
  z80f::Registers &reg = snapshot.registers;
  reg.a = r.u8();
  reg.flags.bits = r.u8();
  reg.b = r.u8();
  reg.c = r.u8();
  reg.d = r.u8();
  reg.e = r.u8();
  reg.h = r.u8();
  reg.l = r.u8();
  reg.a_alt = r.u8();
  reg.f_alt = r.u8();
  reg.b_alt = r.u8();
  reg.c_alt = r.u8();
  reg.d_alt = r.u8();
  reg.e_alt = r.u8();
  reg.h_alt = r.u8();
  reg.l_alt = r.u8();
  reg.ix = r.u16();
  reg.iy = r.u16();
  reg.sp = r.u16();
  reg.pc = r.u16();
  reg.wz = r.u16();
  reg.i = r.u8();
  reg.r = r.u8();
  reg.iff1 = r.u8() != 0;
  reg.iff2 = r.u8() != 0;
  reg.im = r.u8();
  reg.halted = r.u8() != 0;
  reg.ei_pending = r.u8() != 0;
  reg.q = r.u8();
  snapshot.cycle_counter = r.u64();
  snapshot.base_cycle_counter = r.u64();
  snapshot.nmi_line = r.u8() != 0;
  snapshot.nmi_pending = r.u8() != 0;
  snapshot.int_line = r.u8() != 0;
  snapshot.int_pulse_pending = r.u8() != 0;
  return snapshot;
}

void write_psg(ChunkWriter &w, const Psg::SavedState &psg)
{
  for (const uint16_t value : psg.counter)
    w.u16(value);
  for (const uint16_t value : psg.period)
    w.u16(value);
  w.bytes(psg.atten);
  w.bytes(psg.output_bit);
  w.u8(psg.latch_channel);
  w.u8(psg.latch_volume);
  w.u16(psg.lfsr);
  w.u8(psg.noise_mode);
  w.u8(psg.noise_rate);
  w.u16((uint16_t)psg.output_value);
  w.u64(psg.mclk_acc);
}

Psg::SavedState read_psg(ChunkReader &r)
{
  Psg::SavedState psg{};
  for (uint16_t &value : psg.counter)
    value = r.u16();
  for (uint16_t &value : psg.period)
    value = r.u16();
  const auto atten = r.bytes(4);
  const auto output = r.bytes(4);
  if (atten.size() == 4)
    std::copy_n(atten.data(), 4, psg.atten);
  if (output.size() == 4)
    std::copy_n(output.data(), 4, psg.output_bit);
  psg.latch_channel = r.u8();
  psg.latch_volume = r.u8();
  psg.lfsr = r.u16();
  psg.noise_mode = r.u8();
  psg.noise_rate = r.u8();
  psg.output_value = (int16_t)r.u16();
  psg.mclk_acc = r.u64();
  return psg;
}

}  // namespace

MasterSystemMachine::MasterSystemMachine(std::unique_ptr<IAudioBackend> audio,
                                         std::unique_ptr<IVideoBackend> video,
                                         std::shared_ptr<ILogger> logger)
    : MasterSystemMachine(audio.get(), video.get(), std::move(logger))
{
  m_owned_audio = std::move(audio);
  m_owned_video = std::move(video);
}

MasterSystemMachine::MasterSystemMachine(IAudioBackend *audio,
                                         IVideoBackend *video,
                                         std::shared_ptr<ILogger> logger)
    : m_audio(audio), m_video(video), m_logger(std::move(logger)),
      m_bus(m_cartridge, m_vdp, m_psg, m_pads), m_cpu(m_bus)
{
}

MasterSystemMachine::~MasterSystemMachine()
{
  (void)flush_battery();
}

std::expected<void, std::string>
MasterSystemMachine::load_rom(std::string_view filename)
{
  auto data = read_media_file(filename);
  if (!data) {
    return std::unexpected(data.error());
  }
  return load_rom_mem(*data, filename);
}

std::expected<void, std::string>
MasterSystemMachine::load_rom_mem(std::span<const uint8_t> rom_data,
                                  std::string_view filename)
{
  auto normalized = normalize_sms_rom(rom_data);
  if (!normalized) {
    return std::unexpected(normalized.error());
  }
  if (m_cartridge.sram_dirty()) {
    const auto saved = save_sram();
    if (!saved) {
      return std::unexpected(std::string(state_error_message(saved.error())));
    }
  }

  auto loaded = m_cartridge.load(std::move(*normalized));
  if (!loaded) {
    return loaded;
  }
  m_cartinfo = {};
  if (!filename.empty()) {
    set_leafname(m_leafname, filename);
    std::snprintf(m_cartinfo.name_domestic, sizeof(m_cartinfo.name_domestic),
                  "%s", m_leafname.data());
    std::snprintf(m_cartinfo.name_overseas, sizeof(m_cartinfo.name_overseas),
                  "%s", m_leafname.data());
  }
  m_powered = false;
  m_mclk_total = 0;
  m_frames = 0;
  (void)load_sram();
  return {};
}

void MasterSystemMachine::unload_rom()
{
  (void)flush_battery();
  m_cartridge.unload();
  m_cartinfo = {};
  m_leafname = {};
  m_powered = false;
  m_mclk_total = 0;
  m_frames = 0;
}

bool MasterSystemMachine::rom_loaded() const
{
  return m_cartridge.loaded();
}

void MasterSystemMachine::power_on()
{
  m_cartridge.reset();
  m_bus.reset();
  m_vdp.reset(m_pal);
  m_psg.reset();
  m_cpu.reset();
  m_pause.store(false, std::memory_order_relaxed);
  m_cpu.set_nmi_line(false);
  m_cpu.set_int_line(false);
  m_mclk_total = 0;
  m_frames = 0;
  m_audio_acc = 0;
  m_mix_acc = 0;
  m_mix_ticks = 0;
  m_audio_left.clear();
  m_audio_right.clear();
  m_powered = true;
}

void MasterSystemMachine::reset()
{
  m_powered = false;
  if (rom_loaded()) {
    power_on();
  }
  m_frames = 0;
}

void MasterSystemMachine::soft_reset()
{
  reset();
}

void MasterSystemMachine::run_frame()
{
  if (!rom_loaded()) {
    finish_frame_video();
    m_frames++;
    return;
  }
  if (!m_powered) {
    power_on();
  }

  const uint64_t start = m_vdp.frame_index();
  while (m_vdp.frame_index() == start) {
    const int used = m_cpu.step();
    advance_mclk((uint64_t)used * 15);
  }
  finish_frame_video();
  m_frames++;
  (void)flush_battery();
}

void MasterSystemMachine::advance_mclk(uint64_t ticks)
{
  const uint64_t per_second = m_pal ? kMclkPal : kMclkNtsc;
  constexpr uint64_t sample_rate = SOUND_SAMPLERATE;
  while (ticks != 0) {
    const uint64_t to_sample =
        (per_second - m_audio_acc + sample_rate - 1) / sample_rate;
    const uint64_t chunk =
        std::min({ticks, to_sample, m_psg.mclk_until_output()});
    m_mix_acc += (int64_t)m_psg.output() * (int64_t)chunk;
    m_mix_ticks += chunk;
    m_mclk_total += chunk;

    m_vdp.advance_mclk(chunk);
    m_psg.advance_mclk(chunk);
    m_cpu.set_int_line(m_vdp.irq());

    m_audio_acc += chunk * sample_rate;
    if (m_audio_acc >= per_second) {
      m_audio_acc -= per_second;
      emit_audio_sample();
    }
    ticks -= chunk;
  }
}

void MasterSystemMachine::emit_audio_sample()
{
  int32_t sample = 0;
  if (m_mix_ticks != 0) {
    sample = (int32_t)(m_mix_acc / (int64_t)m_mix_ticks);
    m_mix_acc = 0;
    m_mix_ticks = 0;
  }
  sample = std::clamp(sample, -32768, 32767);
  m_audio_left.push_back((uint16_t)(int16_t)sample);
  m_audio_right.push_back((uint16_t)(int16_t)sample);
}

void MasterSystemMachine::flush_audio()
{
  const size_t owed = (size_t)SOUND_SAMPLERATE / framerate();
  while (m_audio_left.size() < owed) {
    m_audio_left.push_back(0);
    m_audio_right.push_back(0);
  }
  m_audio->output_samples(m_audio_left, m_audio_right);
  m_audio_left.clear();
  m_audio_right.clear();
}

void MasterSystemMachine::finish_frame_video()
{
  for (uint32_t line = 0; line < m_vdp.visible_lines(); line++) {
    m_video->render_line_rgba(
        (int)line,
        std::span<const uint32_t>(m_vdp.line_pixels(line), SmsVdp::kWidth));
  }
  m_video->present_field();
  flush_audio();
}

void MasterSystemMachine::set_input(int player, unsigned int up,
                                    unsigned int down, unsigned int left,
                                    unsigned int right, unsigned int start,
                                    unsigned int a, unsigned int b,
                                    unsigned int, unsigned int, unsigned int,
                                    unsigned int, unsigned int)
{
  if (player >= 0 && player < 2) {
    uint8_t buttons = 0;
    if (up)
      buttons |= 1 << 0;
    if (down)
      buttons |= 1 << 1;
    if (left)
      buttons |= 1 << 2;
    if (right)
      buttons |= 1 << 3;
    if (a)
      buttons |= 1 << 4;
    if (b)
      buttons |= 1 << 5;
    m_pads.buttons[(size_t)player].store(buttons, std::memory_order_relaxed);
  }
  m_pause.store(start != 0, std::memory_order_relaxed);
  m_cpu.set_nmi_line(m_pause.load(std::memory_order_relaxed));
}

void MasterSystemMachine::set_video_mode(int pal, int autodetect)
{
  m_autodetect = autodetect != 0;
  if (!m_autodetect) {
    m_pal = pal != 0;
    m_vdp.reset(m_pal);
  }
}

int MasterSystemMachine::video_mode() const
{
  return m_pal ? 1 : 0;
}

unsigned int MasterSystemMachine::framerate() const
{
  return m_pal ? 50 : 60;
}

void MasterSystemMachine::screen_size(int *width, int *height) const
{
  if (width)
    *width = (int)SmsVdp::kWidth;
  if (height)
    *height = (int)m_vdp.visible_lines();
}

const t_cartinfo *MasterSystemMachine::rom_info() const
{
  return &m_cartinfo;
}

const char *MasterSystemMachine::rom_name() const
{
  return m_leafname.data();
}

void MasterSystemMachine::set_debug(int enabled)
{
  m_debug = enabled;
}

void MasterSystemMachine::set_loglevel(int level)
{
  m_loglevel = level;
}

unsigned int MasterSystemMachine::frame_count() const
{
  return m_frames;
}

void MasterSystemMachine::debug_log_zram_to(const char *)
{
}

uint64_t MasterSystemMachine::rom_id() const
{
  return m_cartridge.rom_id();
}

std::filesystem::path MasterSystemMachine::battery_path() const
{
  return rom_loaded() ? state_battery_file(rom_id(), "battery.sram")
                      : std::filesystem::path{};
}

std::expected<void, StateError> MasterSystemMachine::save_sram()
{
  if (!rom_loaded()) {
    return std::unexpected(StateError::NotLoaded);
  }
  const auto path = battery_path();
  if (path.empty()) {
    return std::unexpected(StateError::Io);
  }
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) {
    return std::unexpected(StateError::Io);
  }
  return state_write_atomic(path, m_cartridge.sram());
}

std::expected<void, StateError> MasterSystemMachine::load_sram()
{
  if (!rom_loaded()) {
    return std::unexpected(StateError::NotLoaded);
  }
  const auto path = battery_path();
  if (path.empty()) {
    return std::unexpected(StateError::Io);
  }
  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) {
    return {};
  }
  auto data = state_read_bounded(path, SmsCartridge::kSramSize);
  if (!data) {
    return std::unexpected(data.error());
  }
  if (data->size() == SmsCartridge::kSramSize) {
    std::copy_n(data->data(), data->size(), m_cartridge.sram().data());
  }
  m_cartridge.clear_sram_dirty();
  return {};
}

std::expected<void, StateError> MasterSystemMachine::flush_battery()
{
  if (!m_cartridge.sram_dirty()) {
    return {};
  }
  auto result = save_sram();
  if (result) {
    m_cartridge.clear_sram_dirty();
  }
  return result;
}

std::expected<std::vector<uint8_t>, StateError>
MasterSystemMachine::capture_state()
{
  if (!rom_loaded()) {
    return std::unexpected(StateError::NotLoaded);
  }

  StateChunk system{fourcc("SYS0"), {}};
  {
    ChunkWriter w;
    w.u8((uint8_t)SystemKind::MasterSystem);
    system.data = w.take_payload();
  }
  StateChunk board{fourcc("BORD"), {}};
  {
    ChunkWriter w;
    w.u32(m_frames);
    w.u64(m_mclk_total);
    w.u8(m_pal ? 1 : 0);
    board.data = w.take_payload();
  }
  StateChunk rom{fourcc("ROMI"), {}};
  {
    ChunkWriter w;
    w.u64(rom_id());
    w.u32((uint32_t)m_cartridge.rom().size());
    rom.data = w.take_payload();
  }
  StateChunk cpu{fourcc("SZ80"), {}};
  {
    ChunkWriter w;
    write_z80(w, m_cpu.save_snapshot());
    cpu.data = w.take_payload();
  }
  StateChunk cart{fourcc("SCRT"), {}};
  {
    const auto saved = m_cartridge.save();
    ChunkWriter w;
    w.u8((uint8_t)saved.mapper);
    w.u8(saved.control);
    w.bytes(saved.banks);
    w.bytes(saved.sram);
    cart.data = w.take_payload();
  }
  StateChunk bus{fourcc("SRAM"), {}};
  {
    const auto saved = m_bus.save();
    ChunkWriter w;
    w.bytes(saved.ram);
    w.u8(saved.memory_control);
    w.u8(saved.io_control);
    bus.data = w.take_payload();
  }
  StateChunk vdp{fourcc("SVDP"), {}};
  {
    const auto saved = m_vdp.save();
    ChunkWriter w;
    w.bytes(saved.reg);
    w.bytes(saved.vram);
    w.bytes(saved.cram);
    w.u16(saved.address);
    w.u8(saved.code);
    w.u8(saved.latch);
    w.u8(saved.read_buffer);
    w.u8(saved.status);
    w.u8(saved.pal);
    w.u64(saved.mclk);
    w.u32(saved.line);
    w.u64(saved.frames);
    w.u8(saved.line_counter);
    w.u8(saved.line_pending);
    w.u8(saved.frame_pending);
    w.u8(saved.hcounter_latch);
    vdp.data = w.take_payload();
  }
  StateChunk psg{fourcc("PSG0"), {}};
  {
    ChunkWriter w;
    write_psg(w, m_psg.save());
    psg.data = w.take_payload();
  }
  StateChunk audio{fourcc("AUD0"), {}};
  {
    ChunkWriter w;
    w.u64(m_audio_acc);
    w.u64((uint64_t)m_mix_acc);
    w.u64(m_mix_ticks);
    audio.data = w.take_payload();
  }

  const StateChunk chunks[] = {
      std::move(system), std::move(board), std::move(rom),
      std::move(cpu),    std::move(cart),  std::move(bus),
      std::move(vdp),    std::move(psg),   std::move(audio)};
  return state_serialize(chunks);
}

std::expected<void, StateError>
MasterSystemMachine::save_state(std::string_view filename)
{
  if (filename.empty()) {
    return std::unexpected(StateError::Io);
  }
  auto blob = capture_state();
  if (!blob) {
    return std::unexpected(blob.error());
  }
  return state_write_atomic(std::filesystem::path{std::string(filename)},
                            *blob);
}

std::expected<void, StateError>
MasterSystemMachine::load_state(std::string_view filename)
{
  if (!rom_loaded()) {
    return std::unexpected(StateError::NotLoaded);
  }
  if (filename.empty()) {
    return std::unexpected(StateError::Io);
  }
  auto blob = state_read_bounded(std::filesystem::path{std::string(filename)},
                                 kMaxStateBytes);
  if (!blob) {
    return std::unexpected(blob.error());
  }
  auto chunks = StateV3::deserialize(*blob);
  if (!chunks) {
    return std::unexpected(chunks.error() == StateV3Error::UnsupportedVersion
                               ? StateError::UnsupportedVersion
                               : StateError::Corrupt);
  }

  bool duplicate = false;
  auto unique = [&](uint32_t id) -> const StateChunk * {
    const StateChunk *found = nullptr;
    for (const StateChunk &chunk : *chunks) {
      if (chunk.id == id) {
        if (found)
          duplicate = true;
        found = &chunk;
      }
    }
    return found;
  };

  const StateChunk *system = unique(fourcc("SYS0"));
  const StateChunk *board = unique(fourcc("BORD"));
  const StateChunk *rom = unique(fourcc("ROMI"));
  const StateChunk *cpu = unique(fourcc("SZ80"));
  const StateChunk *cart = unique(fourcc("SCRT"));
  const StateChunk *bus = unique(fourcc("SRAM"));
  const StateChunk *vdp = unique(fourcc("SVDP"));
  const StateChunk *psg = unique(fourcc("PSG0"));
  const StateChunk *audio = unique(fourcc("AUD0"));
  if (duplicate || !system || !board || !rom || !cpu || !cart || !bus || !vdp ||
      !psg || !audio) {
    return std::unexpected(StateError::Corrupt);
  }

  {
    ChunkReader r{system->data};
    if (r.u8() != (uint8_t)SystemKind::MasterSystem || !r.ok() ||
        r.remaining() != 0) {
      return std::unexpected(StateError::WrongRom);
    }
  }
  {
    ChunkReader r{rom->data};
    const uint64_t hash = r.u64();
    const uint32_t size = r.u32();
    if (!r.ok() || r.remaining() != 0) {
      return std::unexpected(StateError::Corrupt);
    }
    if (hash != rom_id() || size != m_cartridge.rom().size()) {
      return std::unexpected(StateError::WrongRom);
    }
  }

  uint32_t frames;
  uint64_t mclk;
  bool pal;
  {
    ChunkReader r{board->data};
    frames = r.u32();
    mclk = r.u64();
    const uint8_t pal_value = r.u8();
    pal = pal_value != 0;
    if (!r.ok() || r.remaining() != 0 || pal_value > 1) {
      return std::unexpected(StateError::Corrupt);
    }
  }
  ChunkReader cpu_reader{cpu->data};
  const z80f::Snapshot snapshot = read_z80(cpu_reader);
  if (!cpu_reader.ok() || cpu_reader.remaining() != 0 ||
      snapshot.registers.im > 2) {
    return std::unexpected(StateError::Corrupt);
  }

  SmsCartridge::SavedState cart_state;
  {
    ChunkReader r{cart->data};
    cart_state.mapper = (SmsMapper)r.u8();
    cart_state.control = r.u8();
    const auto banks = r.bytes(3);
    const auto sram = r.bytes(SmsCartridge::kSramSize);
    if (!r.ok() || r.remaining() != 0 ||
        cart_state.mapper > SmsMapper::Codemasters) {
      return std::unexpected(StateError::Corrupt);
    }
    std::copy_n(banks.data(), 3, cart_state.banks);
    std::copy_n(sram.data(), sram.size(), cart_state.sram.data());
  }
  SmsBus::SavedState bus_state;
  {
    ChunkReader r{bus->data};
    const auto ram = r.bytes(bus_state.ram.size());
    bus_state.memory_control = r.u8();
    bus_state.io_control = r.u8();
    if (!r.ok() || r.remaining() != 0) {
      return std::unexpected(StateError::Corrupt);
    }
    std::copy_n(ram.data(), ram.size(), bus_state.ram.data());
  }
  SmsVdp::SavedState vdp_state;
  {
    ChunkReader r{vdp->data};
    const auto reg = r.bytes(16);
    const auto vram = r.bytes(0x4000);
    const auto cram = r.bytes(32);
    if (reg.size() == 16)
      std::copy_n(reg.data(), 16, vdp_state.reg);
    if (vram.size() == 0x4000)
      std::copy_n(vram.data(), 0x4000, vdp_state.vram);
    if (cram.size() == 32)
      std::copy_n(cram.data(), 32, vdp_state.cram);
    vdp_state.address = r.u16();
    vdp_state.code = r.u8();
    vdp_state.latch = r.u8();
    vdp_state.read_buffer = r.u8();
    vdp_state.status = r.u8();
    vdp_state.pal = r.u8();
    vdp_state.mclk = r.u64();
    vdp_state.line = r.u32();
    vdp_state.frames = r.u64();
    vdp_state.line_counter = r.u8();
    vdp_state.line_pending = r.u8();
    vdp_state.frame_pending = r.u8();
    vdp_state.hcounter_latch = r.u8();
    if (!r.ok() || r.remaining() != 0 ||
        !SmsVdp::valid_saved_state(vdp_state)) {
      return std::unexpected(StateError::Corrupt);
    }
  }
  ChunkReader psg_reader{psg->data};
  const Psg::SavedState psg_state = read_psg(psg_reader);
  if (!psg_reader.ok() || psg_reader.remaining() != 0) {
    return std::unexpected(StateError::Corrupt);
  }

  uint64_t audio_acc;
  int64_t mix_acc;
  uint64_t mix_ticks;
  {
    ChunkReader r{audio->data};
    audio_acc = r.u64();
    mix_acc = (int64_t)r.u64();
    mix_ticks = r.u64();
    const uint64_t per_second = pal ? kMclkPal : kMclkNtsc;
    const uint64_t max_ticks =
        (per_second + SOUND_SAMPLERATE - 1) / SOUND_SAMPLERATE;
    const int64_t limit = (int64_t)mix_ticks * 32768;
    if (!r.ok() || r.remaining() != 0 || audio_acc >= per_second ||
        mix_ticks > max_ticks || mix_acc < -limit || mix_acc > limit) {
      return std::unexpected(StateError::Corrupt);
    }
  }

  m_cartridge.restore(cart_state);
  m_bus.restore(bus_state);
  m_vdp.restore(vdp_state);
  m_psg.restore(psg_state);
  m_cpu.load_snapshot(snapshot);
  m_cpu.set_nmi_line(false);
  m_cpu.set_int_line(m_vdp.irq());
  m_pause.store(false, std::memory_order_relaxed);
  m_frames = frames;
  m_mclk_total = mclk;
  m_pal = pal;
  m_audio_acc = audio_acc;
  m_mix_acc = mix_acc;
  m_mix_ticks = mix_ticks;
  m_powered = true;
  return {};
}

std::filesystem::path MasterSystemMachine::state_slot_path(int slot) const
{
  return rom_loaded() ? state_slot_file(rom_id(), slot)
                      : std::filesystem::path{};
}

std::expected<void, StateError> MasterSystemMachine::save_state_slot(int slot)
{
  if (slot < 0 || slot >= kStateSlotCount) {
    return std::unexpected(StateError::InvalidSlot);
  }
  if (!rom_loaded()) {
    return std::unexpected(StateError::NotLoaded);
  }
  const auto path = state_slot_path(slot);
  if (path.empty()) {
    return std::unexpected(StateError::Io);
  }
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) {
    return std::unexpected(StateError::Io);
  }
  return save_state(path.string());
}

std::expected<void, StateError> MasterSystemMachine::load_state_slot(int slot)
{
  if (slot < 0 || slot >= kStateSlotCount) {
    return std::unexpected(StateError::InvalidSlot);
  }
  if (!rom_loaded()) {
    return std::unexpected(StateError::NotLoaded);
  }
  const auto path = state_slot_path(slot);
  if (path.empty()) {
    return std::unexpected(StateError::Io);
  }
  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) {
    return std::unexpected(StateError::NoSlot);
  }
  return load_state(path.string());
}

time_t MasterSystemMachine::state_slot_date(int slot) const
{
  if (slot < 0 || slot >= kStateSlotCount) {
    return 0;
  }
  const auto path = state_slot_path(slot);
  if (path.empty()) {
    return 0;
  }
  std::error_code ec;
  const auto time = std::filesystem::last_write_time(path, ec);
  if (ec) {
    return 0;
  }
  const auto system_time =
      std::chrono::time_point_cast<std::chrono::system_clock::duration>(
          time - std::filesystem::file_time_type::clock::now() +
          std::chrono::system_clock::now());
  return std::chrono::system_clock::to_time_t(system_time);
}

}  // namespace generator
