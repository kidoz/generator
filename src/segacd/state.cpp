/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "state.hpp"

#include "core/state_v3.hpp"

#include <algorithm>

namespace generator {

SegaCdState save_segacd_state(const SegaCd &cd)
{
  return SegaCdState{cd.disc().identity(), cd.base_state(),
                     cd.sub_cpu().save(),  cd.cdd().save(),
                     cd.cdc().save(),      cd.asic().save(cd.word_ram()),
                     cd.pcm().save(),      cd.cd_audio().save()};
}

bool valid_segacd_state(const SegaCd &cd, const SegaCdState &state)
{
  return state.disc_identity == cd.disc().identity() &&
         cd.valid_base_state(state.base) &&
         cd.sub_cpu().valid_saved_state(state.sub_cpu) &&
         cd.cdd().valid_saved_state(state.cdd) &&
         cd.cdc().valid_saved_state(state.cdc) &&
         cd.asic().valid_saved_state(state.asic, cd.word_ram()) &&
         cd.pcm().valid_saved_state(state.pcm) &&
         cd.cd_audio().valid_saved_state(state.cd_audio) &&
         state.cdd.lba <= cd.disc().sector_count() &&
         state.cd_audio.end_lba <= cd.disc().sector_count();
}

bool restore_segacd_state(SegaCd &cd, const SegaCdState &state)
{
  if (!valid_segacd_state(cd, state)) {
    return false;
  }
  cd.restore_base_state(state.base);
  (void)cd.sub_cpu().restore(state.sub_cpu);
  (void)cd.cdd().restore(state.cdd);
  (void)cd.cdc().restore(state.cdc);
  (void)cd.asic().restore(state.asic, cd.word_ram());
  (void)cd.pcm().restore(state.pcm);
  (void)cd.cd_audio().restore(state.cd_audio);
  cd.update_interrupts();
  return true;
}

namespace {

void write_cpu(ChunkWriter &w, const M68k::SavedState &cpu)
{
  for (const uint32_t value : cpu.d)
    w.u32(value);
  for (const uint32_t value : cpu.a)
    w.u32(value);
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
}

M68k::SavedState read_cpu(ChunkReader &r)
{
  M68k::SavedState cpu{};
  for (uint32_t &value : cpu.d)
    value = r.u32();
  for (uint32_t &value : cpu.a)
    value = r.u32();
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
  return cpu;
}

void write_sub_cpu(ChunkWriter &w, const SegaCdSubCpu::SavedState &state)
{
  w.bytes(state.bus.ram);
  w.u16(state.bus.data_bus);
  w.u32((uint32_t)state.bus.wait_states);
  w.u32(state.bus.interrupt_acks);
  w.u8((uint8_t)state.bus.acknowledged_level);
  write_cpu(w, state.cpu);
  w.u8(state.running ? 1 : 0);
  w.u64(state.master_ticks);
  w.u64(state.clock_acc);
  w.u64((uint64_t)state.clock_credit);
}

SegaCdSubCpu::SavedState read_sub_cpu(ChunkReader &r)
{
  SegaCdSubCpu::SavedState state;
  const auto ram = r.bytes(state.bus.ram.size());
  if (ram.size() == state.bus.ram.size())
    std::copy_n(ram.data(), ram.size(), state.bus.ram.data());
  state.bus.data_bus = r.u16();
  state.bus.wait_states = (int)r.u32();
  state.bus.interrupt_acks = r.u32();
  state.bus.acknowledged_level = r.u8();
  state.cpu = read_cpu(r);
  state.running = r.u8() != 0;
  state.master_ticks = r.u64();
  state.clock_acc = r.u64();
  state.clock_credit = (int64_t)r.u64();
  return state;
}

}  // namespace

std::vector<uint8_t> serialize_segacd_state(const SegaCdState &state)
{
  ChunkWriter w;
  w.u64(state.disc_identity);
  w.bytes(state.base.word_ram);
  w.bytes(state.base.main_communication);
  w.bytes(state.base.sub_communication);
  w.u16(state.base.main_data_bus);
  w.u16(state.base.sub_data_bus);
  w.u8(state.base.sub_reset_release ? 1 : 0);
  w.u8(state.base.sub_bus_request ? 1 : 0);
  w.u8(state.base.word_ram_main_owner ? 1 : 0);
  w.u8(state.base.word_ram_mode_1m ? 1 : 0);
  write_sub_cpu(w, state.sub_cpu);
  w.u64(state.cdd.clock_acc);
  w.u64(state.cdd.lba);
  w.u8((uint8_t)state.cdd.status);
  w.u8(state.cdd.sector_pending ? 1 : 0);
  w.bytes(state.cdd.sector);
  w.u8(state.cdd.irq_pending ? 1 : 0);
  w.u32(state.cdd.parameter);
  w.bytes(state.cdc.buffer);
  w.u32((uint32_t)state.cdc.read_pos);
  w.u32((uint32_t)state.cdc.write_pos);
  w.u32((uint32_t)state.cdc.used);
  w.u64(state.cdc.clock_acc);
  w.u8(state.cdc.dma_active ? 1 : 0);
  w.u8(state.cdc.irq_pending ? 1 : 0);
  w.u8((uint8_t)state.cdc.dma_target);
  w.u32(state.cdc.dma_destination);
  w.u32(state.cdc.dma_remaining);
  w.u32(state.asic.source_offset);
  w.u32(state.asic.destination_buffer_offset);
  w.u32(state.asic.source_width);
  w.u32(state.asic.source_height);
  w.u32(state.asic.destination_width);
  w.u32(state.asic.destination_height);
  w.u32(state.asic.scale_16_16);
  w.u16(state.asic.angle);
  w.u32(state.asic.destination_offset);
  w.u64(state.asic.clock_acc);
  w.u64(state.asic.pixels_remaining);
  w.u64(state.asic.pixel_index);
  w.u8(state.asic.busy ? 1 : 0);
  w.u8(state.asic.irq_pending ? 1 : 0);
  w.bytes(state.pcm.wave_ram);
  for (size_t i = 0; i < SegaCdPcm::kChannels; i++) {
    const auto &config = state.pcm.configs[i];
    w.u8(config.enabled ? 1 : 0);
    w.u8(config.loop ? 1 : 0);
    w.u32(config.start);
    w.u32(config.end);
    w.u32(config.loop_start);
    w.u32(config.pitch_16_16);
    w.u8(config.volume_left);
    w.u8(config.volume_right);
    w.u64(state.pcm.positions[i]);
    w.u32((uint32_t)state.pcm.left[i]);
    w.u32((uint32_t)state.pcm.right[i]);
  }
  w.u64(state.pcm.clock_acc);
  w.u32((uint32_t)state.pcm.mix_left);
  w.u32((uint32_t)state.pcm.mix_right);
  w.u64(state.cd_audio.clock_acc);
  w.bytes(state.cd_audio.sector);
  w.u32((uint32_t)state.cd_audio.frame);
  w.u64(state.cd_audio.lba);
  w.u64(state.cd_audio.end_lba);
  w.u8(state.cd_audio.playing ? 1 : 0);
  w.u8(state.cd_audio.volume);
  w.u32((uint32_t)state.cd_audio.left);
  w.u32((uint32_t)state.cd_audio.right);
  // Additive boot-control tail; the original SCD0 prefix stays readable.
  w.u8(state.base.program_ram_bank);
  w.u8(state.base.write_protect);
  w.u8(state.base.word_ram_dmna ? 1 : 0);
  w.u8(state.base.sub_reset_pending ? 1 : 0);
  // Hardware register extension; absent in older SCD0 payloads.
  w.u32(0x31444353);
  w.bytes(state.base.gate.registers);
  w.bytes(state.base.gate.backup_ram);
  w.u64(state.base.gate.clock_acc);
  w.u16(state.base.gate.stopwatch);
  w.u16(state.base.gate.divider);
  w.u16(state.base.gate.timer_count);
  w.u8(state.base.gate.pending);
  w.u8(state.base.gate.hint_override ? 1 : 0);
  w.bytes(state.cdd.protocol.command);
  w.bytes(state.cdd.protocol.status);
  w.u64(state.cdd.protocol.clock_acc);
  w.u8(state.cdd.protocol.hock ? 1 : 0);
  w.u8(state.cdd.protocol.drive_status);
  w.u16(state.cdd.protocol.lead_in);
  w.u8(state.cdd.protocol.seek_ticks);
  w.u8(state.cdd.protocol.seek_status);
  w.u32(state.cdd.protocol.sector_msf);
  w.u8(state.cdc.registers.enabled ? 1 : 0);
  w.u8(state.cdc.registers.index);
  w.u8(state.cdc.registers.mode);
  w.u8(state.cdc.registers.ifstat);
  w.u8(state.cdc.registers.ifctrl);
  w.bytes(state.cdc.registers.control);
  w.bytes(state.cdc.registers.header);
  w.bytes(state.cdc.registers.status);
  w.u16(state.cdc.registers.count);
  w.u16(state.cdc.registers.source);
  w.u16(state.cdc.registers.pointer);
  w.u16(state.cdc.registers.write_address);
  w.u32(state.cdc.registers.destination);
  w.u8(state.cdc.registers.transferring ? 1 : 0);
  w.u64(state.cdc.registers.decoder_clock_acc);
  w.u8(state.pcm.registers.active ? 1 : 0);
  w.u8(state.pcm.registers.control);
  w.u8(state.pcm.registers.bank);
  w.u8(state.pcm.registers.channel);
  w.u8(state.pcm.registers.disabled);
  for (const auto &channel : state.pcm.registers.channels)
    w.bytes(channel);
  for (const auto position : state.pcm.registers.positions)
    w.u32(position);
  w.bytes(state.asic.stamp.registers);
  w.u8(state.asic.stamp.priority);
  w.u32(state.asic.stamp.pixel);
  w.u64(state.asic.stamp.clock_acc);
  w.u8(state.asic.stamp.busy ? 1 : 0);
  w.u16(state.cd_audio.fader.current);
  w.u16(state.cd_audio.fader.target);
  w.u64(state.cd_audio.fader.clock_acc);
  w.u8(state.cd_audio.fader.enabled ? 1 : 0);
  return w.take_payload();
}

std::expected<SegaCdState, std::string>
parse_segacd_state(std::span<const uint8_t> payload)
{
  ChunkReader r{payload};
  SegaCdState state;
  state.disc_identity = r.u64();
  const auto word_ram = r.bytes(state.base.word_ram.size());
  const auto main_comm = r.bytes(state.base.main_communication.size());
  const auto sub_comm = r.bytes(state.base.sub_communication.size());
  if (word_ram.size() == state.base.word_ram.size())
    std::copy_n(word_ram.data(), word_ram.size(), state.base.word_ram.data());
  if (main_comm.size() == state.base.main_communication.size())
    std::copy_n(main_comm.data(), main_comm.size(),
                state.base.main_communication.data());
  if (sub_comm.size() == state.base.sub_communication.size())
    std::copy_n(sub_comm.data(), sub_comm.size(),
                state.base.sub_communication.data());
  state.base.main_data_bus = r.u16();
  state.base.sub_data_bus = r.u16();
  state.base.sub_reset_release = r.u8() != 0;
  state.base.sub_bus_request = r.u8() != 0;
  state.base.word_ram_main_owner = r.u8() != 0;
  state.base.word_ram_mode_1m = r.u8() != 0;
  state.sub_cpu = read_sub_cpu(r);
  state.cdd.clock_acc = r.u64();
  state.cdd.lba = r.u64();
  state.cdd.status = (SegaCdCddStatus)r.u8();
  state.cdd.sector_pending = r.u8() != 0;
  const auto cdd_sector = r.bytes(state.cdd.sector.size());
  if (cdd_sector.size() == state.cdd.sector.size())
    std::copy_n(cdd_sector.data(), cdd_sector.size(), state.cdd.sector.data());
  state.cdd.irq_pending = r.u8() != 0;
  state.cdd.parameter = r.u32();
  const auto cdc_buffer = r.bytes(state.cdc.buffer.size());
  if (cdc_buffer.size() == state.cdc.buffer.size())
    std::copy_n(cdc_buffer.data(), cdc_buffer.size(), state.cdc.buffer.data());
  state.cdc.read_pos = r.u32();
  state.cdc.write_pos = r.u32();
  state.cdc.used = r.u32();
  state.cdc.clock_acc = r.u64();
  state.cdc.dma_active = r.u8() != 0;
  state.cdc.irq_pending = r.u8() != 0;
  state.cdc.dma_target = (SegaCdCdcDmaTarget)r.u8();
  state.cdc.dma_destination = r.u32();
  state.cdc.dma_remaining = r.u32();
  state.asic.source_offset = r.u32();
  state.asic.destination_buffer_offset = r.u32();
  state.asic.source_width = r.u32();
  state.asic.source_height = r.u32();
  state.asic.destination_width = r.u32();
  state.asic.destination_height = r.u32();
  state.asic.scale_16_16 = r.u32();
  state.asic.angle = r.u16();
  state.asic.destination_offset = r.u32();
  state.asic.clock_acc = r.u64();
  state.asic.pixels_remaining = r.u64();
  state.asic.pixel_index = r.u64();
  state.asic.busy = r.u8() != 0;
  state.asic.irq_pending = r.u8() != 0;
  const auto wave_ram = r.bytes(state.pcm.wave_ram.size());
  if (wave_ram.size() == state.pcm.wave_ram.size())
    std::copy_n(wave_ram.data(), wave_ram.size(), state.pcm.wave_ram.data());
  for (size_t i = 0; i < SegaCdPcm::kChannels; i++) {
    auto &config = state.pcm.configs[i];
    config.enabled = r.u8() != 0;
    config.loop = r.u8() != 0;
    config.start = r.u32();
    config.end = r.u32();
    config.loop_start = r.u32();
    config.pitch_16_16 = r.u32();
    config.volume_left = r.u8();
    config.volume_right = r.u8();
    state.pcm.positions[i] = r.u64();
    state.pcm.left[i] = (int32_t)r.u32();
    state.pcm.right[i] = (int32_t)r.u32();
  }
  state.pcm.clock_acc = r.u64();
  state.pcm.mix_left = (int32_t)r.u32();
  state.pcm.mix_right = (int32_t)r.u32();
  state.cd_audio.clock_acc = r.u64();
  const auto audio_sector = r.bytes(state.cd_audio.sector.size());
  if (audio_sector.size() == state.cd_audio.sector.size())
    std::copy_n(audio_sector.data(), audio_sector.size(),
                state.cd_audio.sector.data());
  state.cd_audio.frame = r.u32();
  state.cd_audio.lba = r.u64();
  state.cd_audio.end_lba = r.u64();
  state.cd_audio.playing = r.u8() != 0;
  state.cd_audio.volume = r.u8();
  state.cd_audio.left = (int32_t)r.u32();
  state.cd_audio.right = (int32_t)r.u32();
  state.base.word_ram_dmna =
      !state.base.word_ram_mode_1m && !state.base.word_ram_main_owner;
  state.base.sub_reset_pending = !state.base.sub_reset_release;
  if (r.remaining() != 0) {
    state.base.program_ram_bank = r.u8();
    state.base.write_protect = r.u8();
    state.base.word_ram_dmna = r.u8() != 0;
    state.base.sub_reset_pending = r.u8() != 0;
  }
  if (r.remaining() != 0) {
    if (!r.has(kSegaCdRegisterStateBytes) || r.u32() != 0x31444353)
      return std::unexpected("invalid Sega CD register extension");
    auto read_array = [&](auto &destination) {
      const auto bytes = r.bytes(destination.size());
      if (bytes.size() == destination.size())
        std::copy(bytes.begin(), bytes.end(), destination.begin());
    };
    read_array(state.base.gate.registers);
    read_array(state.base.gate.backup_ram);
    state.base.gate.clock_acc = r.u64();
    state.base.gate.stopwatch = r.u16();
    state.base.gate.divider = r.u16();
    state.base.gate.timer_count = r.u16();
    state.base.gate.pending = r.u8();
    state.base.gate.hint_override = r.u8() != 0;
    read_array(state.cdd.protocol.command);
    read_array(state.cdd.protocol.status);
    state.cdd.protocol.clock_acc = r.u64();
    state.cdd.protocol.hock = r.u8() != 0;
    state.cdd.protocol.drive_status = r.u8();
    state.cdd.protocol.lead_in = r.u16();
    state.cdd.protocol.seek_ticks = r.u8();
    state.cdd.protocol.seek_status = r.u8();
    state.cdd.protocol.sector_msf = r.u32();
    state.cdc.registers.enabled = r.u8() != 0;
    state.cdc.registers.index = r.u8();
    state.cdc.registers.mode = r.u8();
    state.cdc.registers.ifstat = r.u8();
    state.cdc.registers.ifctrl = r.u8();
    read_array(state.cdc.registers.control);
    read_array(state.cdc.registers.header);
    read_array(state.cdc.registers.status);
    state.cdc.registers.count = r.u16();
    state.cdc.registers.source = r.u16();
    state.cdc.registers.pointer = r.u16();
    state.cdc.registers.write_address = r.u16();
    state.cdc.registers.destination = r.u32();
    state.cdc.registers.transferring = r.u8() != 0;
    state.cdc.registers.decoder_clock_acc = r.u64();
    state.pcm.registers.active = r.u8() != 0;
    state.pcm.registers.control = r.u8();
    state.pcm.registers.bank = r.u8();
    state.pcm.registers.channel = r.u8();
    state.pcm.registers.disabled = r.u8();
    for (auto &channel : state.pcm.registers.channels)
      read_array(channel);
    for (auto &position : state.pcm.registers.positions)
      position = r.u32();
    read_array(state.asic.stamp.registers);
    state.asic.stamp.priority = r.u8();
    state.asic.stamp.pixel = r.u32();
    state.asic.stamp.clock_acc = r.u64();
    state.asic.stamp.busy = r.u8() != 0;
    state.cd_audio.fader.current = r.u16();
    state.cd_audio.fader.target = r.u16();
    state.cd_audio.fader.clock_acc = r.u64();
    state.cd_audio.fader.enabled = r.u8() != 0;
  }
  if (!r.ok() || r.remaining() != 0) {
    return std::unexpected("invalid Sega CD state payload");
  }
  return state;
}

}  // namespace generator
