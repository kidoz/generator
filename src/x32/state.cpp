/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "state.hpp"

#include "core/state_files.hpp"
#include "core/state_v3.hpp"

#include <algorithm>

namespace generator {

namespace {

void write_cache(ChunkWriter &w, const Sh2Cache::SavedState &state)
{
  w.u8(state.ccr);
  for (const auto &way : state.entries) {
    for (const auto &entry : way) {
      w.u32(entry.tag);
      w.u8(entry.lru);
      w.u8(entry.valid ? 1 : 0);
    }
  }
}

Sh2Cache::SavedState read_cache(ChunkReader &r)
{
  Sh2Cache::SavedState state;
  state.ccr = r.u8();
  for (auto &way : state.entries) {
    for (auto &entry : way) {
      entry.tag = r.u32();
      entry.lru = r.u8();
      entry.valid = r.u8() != 0;
    }
  }
  return state;
}

void write_dmac(ChunkWriter &w, const Sh2Dmac::SavedState &state)
{
  for (const auto &channel : state.channels) {
    w.u32(channel.sar);
    w.u32(channel.dar);
    w.u32(channel.tcr);
    w.u32(channel.chcr);
    w.u32(channel.vcr);
    w.u32(channel.unit_cycles);
    w.u8(channel.active ? 1 : 0);
    w.u8(channel.read_te ? 1 : 0);
  }
  w.u32(state.dmaor);
  w.u32(state.cycle_credit);
  w.u8(state.drcr[0]);
  w.u8(state.drcr[1]);
  w.u8(state.next_channel);
  w.u8(state.dreq_level[0] ? 1 : 0);
  w.u8(state.dreq_level[1] ? 1 : 0);
  w.u8(state.edge_pending[0] ? 1 : 0);
  w.u8(state.edge_pending[1] ? 1 : 0);
}

Sh2Dmac::SavedState read_dmac(ChunkReader &r)
{
  Sh2Dmac::SavedState state;
  for (auto &channel : state.channels) {
    channel.sar = r.u32();
    channel.dar = r.u32();
    channel.tcr = r.u32();
    channel.chcr = r.u32();
    channel.vcr = r.u32();
    channel.unit_cycles = r.u32();
    channel.active = r.u8() != 0;
    channel.read_te = r.u8() != 0;
  }
  state.dmaor = r.u32();
  state.cycle_credit = r.u32();
  state.drcr[0] = r.u8();
  state.drcr[1] = r.u8();
  state.next_channel = r.u8();
  state.dreq_level[0] = r.u8() != 0;
  state.dreq_level[1] = r.u8() != 0;
  state.edge_pending[0] = r.u8() != 0;
  state.edge_pending[1] = r.u8() != 0;
  return state;
}

void write_divu(ChunkWriter &w, const Sh2Divu::SavedState &state)
{
  w.u32(state.dvsr);
  w.u32(state.dvdnth);
  w.u32(state.dvdntl);
  w.u32(state.dvcr);
  w.u32(state.vcrdiv);
  w.u32(state.pending_remainder);
  w.u32(state.pending_quotient);
  w.u32(state.remaining_cycles);
  w.u8(state.pending_overflow ? 1 : 0);
  w.u8(state.overflow_positive ? 1 : 0);
}

Sh2Divu::SavedState read_divu(ChunkReader &r)
{
  Sh2Divu::SavedState state;
  state.dvsr = r.u32();
  state.dvdnth = r.u32();
  state.dvdntl = r.u32();
  state.dvcr = r.u32();
  state.vcrdiv = r.u32();
  state.pending_remainder = r.u32();
  state.pending_quotient = r.u32();
  state.remaining_cycles = r.u32();
  state.pending_overflow = r.u8() != 0;
  state.overflow_positive = r.u8() != 0;
  return state;
}

void write_frt(ChunkWriter &w, const Sh2Frt::SavedState &state)
{
  w.u16(state.frc);
  w.u16(state.ocra);
  w.u16(state.ocrb);
  w.u16(state.icr);
  w.u8(state.tier);
  w.u8(state.ftcsr);
  w.u8(state.tcr);
  w.u8(state.tocr);
  w.u8(state.temp);
  w.u8(state.read_flags);
  w.u32(state.clock_accumulator);
}

Sh2Frt::SavedState read_frt(ChunkReader &r)
{
  Sh2Frt::SavedState state;
  state.frc = r.u16();
  state.ocra = r.u16();
  state.ocrb = r.u16();
  state.icr = r.u16();
  state.tier = r.u8();
  state.ftcsr = r.u8();
  state.tcr = r.u8();
  state.tocr = r.u8();
  state.temp = r.u8();
  state.read_flags = r.u8();
  state.clock_accumulator = r.u32();
  return state;
}

void write_intc(ChunkWriter &w, const Sh2Intc::SavedState &state)
{
  w.u16(state.ipra);
  w.u16(state.iprb);
  w.u16(state.vcra);
  w.u16(state.vcrb);
  w.u16(state.vcrc);
  w.u16(state.vcrd);
  w.u16(state.vcrwdt);
}

Sh2Intc::SavedState read_intc(ChunkReader &r)
{
  Sh2Intc::SavedState state;
  state.ipra = r.u16();
  state.iprb = r.u16();
  state.vcra = r.u16();
  state.vcrb = r.u16();
  state.vcrc = r.u16();
  state.vcrd = r.u16();
  state.vcrwdt = r.u16();
  return state;
}

void write_sh2_tail(ChunkWriter &w, const Sh2::SavedState &state)
{
  w.u8(state.dmac.module_pending[0] ? 1 : 0);
  w.u8(state.dmac.module_pending[1] ? 1 : 0);
  w.u16(state.intc.icr);
  w.u8(state.intc.irl_level);
  w.u8(state.intc.irl_vector);
}

void read_sh2_tail(ChunkReader &r, Sh2::SavedState &state)
{
  state.dmac.module_pending[0] = r.u8() != 0;
  state.dmac.module_pending[1] = r.u8() != 0;
  state.intc.icr = r.u16();
  state.intc.irl_level = r.u8();
  state.intc.irl_vector = r.u8();
}

void write_cache_data(ChunkWriter &w, const Sh2Cache::SavedState &state)
{
  for (const auto &way : state.entries) {
    for (const auto &entry : way)
      w.bytes(entry.data);
  }
}

void read_cache_data(ChunkReader &r, Sh2Cache::SavedState &state)
{
  for (auto &way : state.entries) {
    for (auto &entry : way) {
      const auto data = r.bytes(entry.data.size());
      if (data.size() == entry.data.size())
        std::copy_n(data.data(), data.size(), entry.data.data());
    }
  }
}

void write_pipeline(ChunkWriter &w, const Sh2::SavedState &state)
{
  w.u8(state.load_destination);
  w.u8(state.load_register);
  w.u8(state.multiplier);
}

void read_pipeline(ChunkReader &r, Sh2::SavedState &state)
{
  state.load_destination = r.u8();
  state.load_register = r.u8();
  state.multiplier = r.u8();
}

void write_bus_pipeline(ChunkWriter &w, const Sh2::SavedState &state)
{
  w.u8(state.multiplier_gap);
  w.u8(state.fetch_valid);
  w.u32(state.fetch_address);
  w.u32(state.fetch_data);
  w.u32(state.write_buffer);
}

void read_bus_pipeline(ChunkReader &r, Sh2::SavedState &state)
{
  state.multiplier_gap = r.u8();
  state.fetch_valid = r.u8();
  state.fetch_address = r.u32();
  state.fetch_data = r.u32();
  state.write_buffer = r.u32();
}

void write_fetch_ahead(ChunkWriter &w, const Sh2::SavedState &state)
{
  for (const auto &entry : state.fetch_ahead) {
    w.u8(entry.valid);
    w.u32(entry.address);
    w.u32(entry.data);
  }
}

void read_fetch_ahead(ChunkReader &r, Sh2::SavedState &state)
{
  for (auto &entry : state.fetch_ahead) {
    entry.valid = r.u8();
    entry.address = r.u32();
    entry.data = r.u32();
  }
}

void write_bsc(ChunkWriter &w, const Sh2Bsc::SavedState &state)
{
  w.u16(state.bcr1);
  w.u16(state.bcr2);
  w.u16(state.wcr);
  w.u16(state.mcr);
  w.u8(state.rtcsr);
  w.u8(state.rtcnt);
  w.u8(state.rtcor);
  w.u8(state.read_cmf);
  w.u16(state.clock_accumulator);
  w.u8(state.refresh_pending ? 1 : 0);
}

void read_bsc(ChunkReader &r, Sh2Bsc::SavedState &state)
{
  state.bcr1 = r.u16();
  state.bcr2 = r.u16();
  state.wcr = r.u16();
  state.mcr = r.u16();
  state.rtcsr = r.u8();
  state.rtcnt = r.u8();
  state.rtcor = r.u8();
  state.read_cmf = r.u8();
  state.clock_accumulator = r.u16();
  state.refresh_pending = r.u8() != 0;
}

void write_sh2(ChunkWriter &w, const Sh2::SavedState &state)
{
  write_cache(w, state.cache);
  write_dmac(w, state.dmac);
  write_divu(w, state.divu);
  write_frt(w, state.frt);
  write_intc(w, state.intc);
  for (const uint32_t value : state.r)
    w.u32(value);
  w.u32(state.mach);
  w.u32(state.macl);
  w.u32(state.pr);
  w.u32(state.gbr);
  w.u32(state.vbr);
  w.u32(state.pc);
  w.u32(state.branch_target);
  w.u32(state.stall_cycles);
  w.u16(state.sr);
  w.u8(state.delay_slots);
  w.u8(state.pending_vector);
  w.u8(state.pending_level);
  w.u8(state.fault);
}

void read_sh2(ChunkReader &r, Sh2::SavedState &state)
{
  state.cache = read_cache(r);
  state.dmac = read_dmac(r);
  state.divu = read_divu(r);
  state.frt = read_frt(r);
  state.intc = read_intc(r);
  for (uint32_t &value : state.r)
    value = r.u32();
  state.mach = r.u32();
  state.macl = r.u32();
  state.pr = r.u32();
  state.gbr = r.u32();
  state.vbr = r.u32();
  state.pc = r.u32();
  state.branch_target = r.u32();
  state.stall_cycles = r.u32();
  state.sr = r.u16();
  state.delay_slots = r.u8();
  state.pending_vector = r.u8();
  state.pending_level = r.u8();
  state.fault = r.u8();
}

void write_video(ChunkWriter &w, const X32Video::SavedState &state)
{
  for (const auto &framebuffer : state.framebuffers)
    w.bytes(framebuffer);
  w.bytes(state.palette);
  w.u16(state.bitmap_mode);
  w.u16(state.bitmap_mode_pending);
  w.u16(state.shift);
  w.u16(state.shift_pending);
  w.u16(state.fill_length);
  w.u16(state.fill_address);
  w.u16(state.fill_data);
  w.u32(state.fill_remaining);
  w.u8(state.fill_startup);
  w.u8(state.fill_subcycle);
  w.u8(state.display_buffer ? 1 : 0);
  w.u8(state.pending_buffer ? 1 : 0);
  w.u8(state.vblank ? 1 : 0);
  w.u8(state.hblank ? 1 : 0);
}

X32Video::SavedState read_video(ChunkReader &r)
{
  X32Video::SavedState state;
  for (auto &framebuffer : state.framebuffers) {
    const auto data = r.bytes(framebuffer.size());
    if (data.size() == framebuffer.size())
      std::copy_n(data.data(), data.size(), framebuffer.data());
  }
  const auto palette = r.bytes(state.palette.size());
  if (palette.size() == state.palette.size())
    std::copy_n(palette.data(), palette.size(), state.palette.data());
  state.bitmap_mode = r.u16();
  state.bitmap_mode_pending = r.u16();
  state.shift = r.u16();
  state.shift_pending = r.u16();
  state.fill_length = r.u16();
  state.fill_address = r.u16();
  state.fill_data = r.u16();
  state.fill_remaining = r.u32();
  state.fill_startup = r.u8();
  state.fill_subcycle = r.u8();
  state.display_buffer = r.u8() != 0;
  state.pending_buffer = r.u8() != 0;
  state.vblank = r.u8() != 0;
  state.hblank = r.u8() != 0;
  return state;
}

void write_pwm(ChunkWriter &w, const X32Pwm::SavedState &state)
{
  for (const auto &channel : state.channels) {
    for (const uint16_t value : channel.fifo)
      w.u16(value);
    w.u8(channel.count);
    w.u16(channel.current);
    w.u8(channel.active ? 1 : 0);
  }
  for (const uint16_t value : state.pulse_latch)
    w.u16(value);
  w.u16(state.control);
  w.u16(state.cycle);
  w.u64(state.clock_accumulator);
  w.u64(state.cycle_remaining);
  w.u8(state.timer_counter);
  w.u8(state.timer_event ? 1 : 0);
}

X32Pwm::SavedState read_pwm(ChunkReader &r)
{
  X32Pwm::SavedState state;
  for (auto &channel : state.channels) {
    for (uint16_t &value : channel.fifo)
      value = r.u16();
    channel.count = r.u8();
    channel.current = r.u16();
    channel.active = r.u8() != 0;
  }
  for (uint16_t &value : state.pulse_latch)
    value = r.u16();
  state.control = r.u16();
  state.cycle = r.u16();
  state.clock_accumulator = r.u64();
  state.cycle_remaining = r.u64();
  state.timer_counter = r.u8();
  state.timer_event = r.u8() != 0;
  return state;
}

void write_base(ChunkWriter &w, const X32::BaseState &state)
{
  w.bytes(state.sdram);
  for (const uint16_t value : state.communication)
    w.u16(value);
  write_video(w, state.video);
  write_pwm(w, state.pwm);
  w.u64(state.clock_accumulator);
  w.u32(state.master_cycles);
  w.u32(state.slave_cycles);
  w.u16(state.adapter_control);
  w.u8(state.bank);
  w.u8(state.cmd_master ? 1 : 0);
  w.u8(state.cmd_slave ? 1 : 0);
  w.u8(state.next_slave ? 1 : 0);
  w.u16(state.interrupt_common);
  w.u8(state.interrupt_masks[0]);
  w.u8(state.interrupt_masks[1]);
  w.u8(state.vint_pending[0] ? 1 : 0);
  w.u8(state.vint_pending[1] ? 1 : 0);
  w.u8(state.hint_pending[0] ? 1 : 0);
  w.u8(state.hint_pending[1] ? 1 : 0);
  w.u8(state.pwm_pending[0] ? 1 : 0);
  w.u8(state.pwm_pending[1] ? 1 : 0);
  w.u8(state.vres_pending[0] ? 1 : 0);
  w.u8(state.vres_pending[1] ? 1 : 0);
  w.u8(state.hcount);
  w.u8(state.hcount_pending);
  w.u8(state.hcounter);
  w.u8(state.last_vblank ? 1 : 0);
  w.u8(state.last_hblank ? 1 : 0);
  for (const uint16_t value : state.dreq_fifo)
    w.u16(value);
  w.u32(state.dreq_source);
  w.u32(state.dreq_destination);
  w.u32(state.dreq_length);
  w.u8(state.fifo_head);
  w.u8(state.fifo_count);
  w.u8(state.fifo_read_byte);
  w.u8(state.dreq_cpu_write ? 1 : 0);
  w.u8(state.dreq_rv ? 1 : 0);
}

X32::BaseState read_base(ChunkReader &r)
{
  X32::BaseState state;
  const auto sdram = r.bytes(state.sdram.size());
  if (sdram.size() == state.sdram.size())
    std::copy_n(sdram.data(), sdram.size(), state.sdram.data());
  for (uint16_t &value : state.communication)
    value = r.u16();
  state.video = read_video(r);
  state.pwm = read_pwm(r);
  state.clock_accumulator = r.u64();
  state.master_cycles = r.u32();
  state.slave_cycles = r.u32();
  state.adapter_control = r.u16();
  state.bank = r.u8();
  state.cmd_master = r.u8() != 0;
  state.cmd_slave = r.u8() != 0;
  state.next_slave = r.u8() != 0;
  state.interrupt_common = r.u16();
  state.interrupt_masks[0] = r.u8();
  state.interrupt_masks[1] = r.u8();
  state.vint_pending[0] = r.u8() != 0;
  state.vint_pending[1] = r.u8() != 0;
  state.hint_pending[0] = r.u8() != 0;
  state.hint_pending[1] = r.u8() != 0;
  state.pwm_pending[0] = r.u8() != 0;
  state.pwm_pending[1] = r.u8() != 0;
  state.vres_pending[0] = r.u8() != 0;
  state.vres_pending[1] = r.u8() != 0;
  state.hcount = r.u8();
  state.hcount_pending = r.u8();
  state.hcounter = r.u8();
  state.last_vblank = r.u8() != 0;
  state.last_hblank = r.u8() != 0;
  for (uint16_t &value : state.dreq_fifo)
    value = r.u16();
  state.dreq_source = r.u32();
  state.dreq_destination = r.u32();
  state.dreq_length = r.u32();
  state.fifo_head = r.u8();
  state.fifo_count = r.u8();
  state.fifo_read_byte = r.u8();
  state.dreq_cpu_write = r.u8() != 0;
  state.dreq_rv = r.u8() != 0;
  return state;
}

bool valid_sh2(const Sh2::SavedState &state)
{
  return Sh2::valid_saved_state(state) && state.dmac.cycle_credit <= 2 &&
         state.dmac.channels[0].unit_cycles <= 2 &&
         state.dmac.channels[1].unit_cycles <= 2 &&
         state.divu.remaining_cycles <= 39 && state.frt.clock_accumulator < 128;
}

}  // namespace

X32State save_x32_state(const X32 &device)
{
  return {state_fnv1a64(device.master_boot()),
          state_fnv1a64(device.slave_boot()),
          (uint32_t)device.master_boot().size(),
          (uint32_t)device.slave_boot().size(),
          device.base_state(),
          device.master().save(),
          device.slave().save()};
}

bool valid_x32_state(const X32 &device, const X32State &state)
{
  return state.master_boot_hash == state_fnv1a64(device.master_boot()) &&
         state.slave_boot_hash == state_fnv1a64(device.slave_boot()) &&
         state.master_boot_size == device.master_boot().size() &&
         state.slave_boot_size == device.slave_boot().size() &&
         state.base.clock_accumulator <= 6 && state.base.fifo_head <= 3 &&
         state.base.fifo_count <= 4 && state.base.fifo_read_byte <= 1 &&
         state.base.dreq_length <= 0x10000 &&
         state.base.bus_lease_cycles <= 13 && state.base.bus_lease_owner <= 2 &&
         (state.base.bus_lease_cycles == 0 || state.base.bus_lease_owner < 2) &&
         state.base.bus_requester <= 2 && state.base.bus_handoff_cycles <= 2 &&
         (state.base.bus_handoff_cycles == 0 ||
          (state.base.bus_lease_cycles == 0 &&
           ((state.base.bus_handoff_cycles == 1 &&
             state.base.bus_lease_owner == 2 && state.base.bus_requester < 2) ||
            (state.base.bus_handoff_cycles == 2 &&
             state.base.bus_lease_owner < 2)))) &&
         state.base.sh2_half <= 1 && state.base.interrupt_masks[0] <= 0xF &&
         state.base.interrupt_masks[1] <= 0xF &&
         X32Video::valid_saved_state(state.base.video) &&
         X32Pwm::valid_saved_state(state.base.pwm) && valid_sh2(state.master) &&
         valid_sh2(state.slave);
}

bool restore_x32_state(X32 &device, const X32State &state)
{
  if (!valid_x32_state(device, state))
    return false;
  device.restore_base_state(state.base);
  device.master().restore(state.master);
  device.slave().restore(state.slave);
  return true;
}

std::vector<uint8_t> serialize_x32_state(const X32State &state)
{
  ChunkWriter w;
  w.u64(state.master_boot_hash);
  w.u64(state.slave_boot_hash);
  w.u32(state.master_boot_size);
  w.u32(state.slave_boot_size);
  write_base(w, state.base);
  write_sh2(w, state.master);
  write_sh2(w, state.slave);
  w.u8(state.base.video.bitmap_latched ? 1 : 0);
  write_sh2_tail(w, state.master);
  write_sh2_tail(w, state.slave);
  write_cache_data(w, state.master.cache);
  write_cache_data(w, state.slave.cache);
  write_pipeline(w, state.master);
  write_pipeline(w, state.slave);
  write_bus_pipeline(w, state.master);
  write_bus_pipeline(w, state.slave);
  w.u32(state.base.bus_lease_cycles);
  w.u8(state.base.bus_lease_owner);
  w.u8(state.base.bus_requester);
  w.u8(state.base.bus_handoff_cycles);
  write_fetch_ahead(w, state.master);
  write_fetch_ahead(w, state.slave);
  w.u8(state.base.sh2_half);
  write_bsc(w, state.master.bsc);
  write_bsc(w, state.slave.bsc);
  return w.take_payload();
}

std::expected<X32State, std::string>
parse_x32_state(std::span<const uint8_t> payload)
{
  ChunkReader r{payload};
  X32State state;
  bool legacy_clock = true;
  state.master_boot_hash = r.u64();
  state.slave_boot_hash = r.u64();
  state.master_boot_size = r.u32();
  state.slave_boot_size = r.u32();
  state.base = read_base(r);
  read_sh2(r, state.master);
  read_sh2(r, state.slave);
  if (r.has(1))
    state.base.video.bitmap_latched = r.u8() != 0;
  if (r.remaining() >= 12) {
    read_sh2_tail(r, state.master);
    read_sh2_tail(r, state.slave);
  } else if (r.remaining() != 0) {
    return std::unexpected("invalid X32 interrupt tail");
  }
  if (r.remaining() == 2 * 4 * 64 * 16) {
    read_cache_data(r, state.master.cache);
    read_cache_data(r, state.slave.cache);
  } else if (r.remaining() == 2 * 4 * 64 * 16 + 6) {
    read_cache_data(r, state.master.cache);
    read_cache_data(r, state.slave.cache);
    read_pipeline(r, state.master);
    read_pipeline(r, state.slave);
  } else if (r.remaining() == 2 * 4 * 64 * 16 + 6 + 28) {
    read_cache_data(r, state.master.cache);
    read_cache_data(r, state.slave.cache);
    read_pipeline(r, state.master);
    read_pipeline(r, state.slave);
    read_bus_pipeline(r, state.master);
    read_bus_pipeline(r, state.slave);
  } else if (r.remaining() == 2 * 4 * 64 * 16 + 6 + 28 + 5) {
    read_cache_data(r, state.master.cache);
    read_cache_data(r, state.slave.cache);
    read_pipeline(r, state.master);
    read_pipeline(r, state.slave);
    read_bus_pipeline(r, state.master);
    read_bus_pipeline(r, state.slave);
    state.base.bus_lease_cycles = r.u32();
    state.base.bus_lease_owner = r.u8();
  } else if (r.remaining() == 2 * 4 * 64 * 16 + 6 + 28 + 7) {
    read_cache_data(r, state.master.cache);
    read_cache_data(r, state.slave.cache);
    read_pipeline(r, state.master);
    read_pipeline(r, state.slave);
    read_bus_pipeline(r, state.master);
    read_bus_pipeline(r, state.slave);
    state.base.bus_lease_cycles = r.u32();
    state.base.bus_lease_owner = r.u8();
    state.base.bus_requester = r.u8();
    state.base.bus_handoff_cycles = r.u8();
  } else if (r.remaining() == 2 * 4 * 64 * 16 + 6 + 28 + 7 + 36) {
    read_cache_data(r, state.master.cache);
    read_cache_data(r, state.slave.cache);
    read_pipeline(r, state.master);
    read_pipeline(r, state.slave);
    read_bus_pipeline(r, state.master);
    read_bus_pipeline(r, state.slave);
    state.base.bus_lease_cycles = r.u32();
    state.base.bus_lease_owner = r.u8();
    state.base.bus_requester = r.u8();
    state.base.bus_handoff_cycles = r.u8();
    read_fetch_ahead(r, state.master);
    read_fetch_ahead(r, state.slave);
  } else if (r.remaining() == 2 * 4 * 64 * 16 + 6 + 28 + 7 + 36 + 1) {
    read_cache_data(r, state.master.cache);
    read_cache_data(r, state.slave.cache);
    read_pipeline(r, state.master);
    read_pipeline(r, state.slave);
    read_bus_pipeline(r, state.master);
    read_bus_pipeline(r, state.slave);
    state.base.bus_lease_cycles = r.u32();
    state.base.bus_lease_owner = r.u8();
    state.base.bus_requester = r.u8();
    state.base.bus_handoff_cycles = r.u8();
    read_fetch_ahead(r, state.master);
    read_fetch_ahead(r, state.slave);
    state.base.sh2_half = r.u8();
    legacy_clock = false;
  } else if (r.remaining() == 2 * 4 * 64 * 16 + 6 + 28 + 7 + 36 + 1 + 30) {
    read_cache_data(r, state.master.cache);
    read_cache_data(r, state.slave.cache);
    read_pipeline(r, state.master);
    read_pipeline(r, state.slave);
    read_bus_pipeline(r, state.master);
    read_bus_pipeline(r, state.slave);
    state.base.bus_lease_cycles = r.u32();
    state.base.bus_lease_owner = r.u8();
    state.base.bus_requester = r.u8();
    state.base.bus_handoff_cycles = r.u8();
    read_fetch_ahead(r, state.master);
    read_fetch_ahead(r, state.slave);
    state.base.sh2_half = r.u8();
    read_bsc(r, state.master.bsc);
    read_bsc(r, state.slave.bsc);
    legacy_clock = false;
  } else if (r.remaining() != 0) {
    return std::unexpected("invalid X32 cache data tail");
  }
  if (!r.ok() || r.remaining() != 0)
    return std::unexpected("invalid X32 state payload");
  if (legacy_clock) {
    const uint8_t doubled = state.base.clock_accumulator * 2;
    state.base.sh2_half = doubled >= 7 ? 1 : 0;
    state.base.clock_accumulator = doubled >= 7 ? doubled - 7 : doubled;
  }
  return state;
}

}  // namespace generator
