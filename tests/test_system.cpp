// System subsystem ownership and transitional compatibility tests.

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <memory>
#include <stdexcept>

#include "controller_ports.hpp"
#include "fm_write_queue.hpp"
#include "machine.h"
#include "sn76496.hpp"
#include "support.h"
#include "fm.h"
#include "system.hpp"
#include "vdp.hpp"
#include "ym2612.hpp"

namespace {

constexpr int PSG_CLOCK = 3579545;
constexpr int PSG_SAMPLE_RATE = 44100;
constexpr int PSG_GAIN = 0;
constexpr int FM_CLOCK = 7670454;
constexpr int FM_SAMPLE_RATE = 48000;

class ActiveSystem {
public:
  explicit ActiveSystem(generator::System &system)
  {
    generator::set_system(&system);
  }

  ~ActiveSystem()
  {
    generator::set_system(nullptr);
  }

  ActiveSystem(const ActiveSystem &) = delete;
  ActiveSystem &operator=(const ActiveSystem &) = delete;
};

generator::System make_system()
{
  return {std::unique_ptr<generator::IAudioBackend>{},
          std::unique_ptr<generator::IVideoBackend>{},
          std::shared_ptr<generator::ILogger>{}};
}

}  // namespace

// Stub for SN76496::save_state(); serialization is covered by the persistence
// suite, while this target checks ownership and compatibility routing.
void state_transfer32(const char * /*mod*/, const char * /*name*/,
                      uint8 /*instance*/, uint32 * /*data*/, uint32 /*size*/)
{
}

void state_transfer8(const char * /*mod*/, const char * /*name*/,
                     uint8 /*instance*/, uint8 * /*data*/, uint32 /*size*/)
{
}

void state_transfer16(const char * /*mod*/, const char * /*name*/,
                      uint8 /*instance*/, uint16 * /*data*/, uint32 /*size*/)
{
}

TEST_CASE("System owns independent SN76496 instances", "[system][psg]")
{
  generator::System first = make_system();
  generator::System second = make_system();

  REQUIRE(&first.psg() != &second.psg());
  REQUIRE(first.psg().init(PSG_CLOCK, PSG_GAIN, PSG_SAMPLE_RATE) == 0);
  REQUIRE(second.psg().init(PSG_CLOCK, PSG_GAIN, PSG_SAMPLE_RATE) == 0);

  first.psg().write(0x90);
  REQUIRE(first.psg().Volume[0] == first.psg().VolTable[0]);
  REQUIRE(second.psg().Volume[0] == 0);
}

TEST_CASE("System owns independent VDP instances", "[system][vdp]")
{
  generator::System first = make_system();
  generator::System second = make_system();

  REQUIRE(&first.vdp() != &second.vdp());
  first.vdp().vdp_pal = 1;
  REQUIRE(second.vdp().vdp_pal == 0);
}

TEST_CASE("System owns independent FM write queues", "[system][fmq]")
{
  generator::System first = make_system();
  generator::System second = make_system();

  REQUIRE(&first.fm_write_queue() != &second.fm_write_queue());
  first.fm_write_queue().push(100, 1, 0xAA);
  REQUIRE(first.fm_write_queue().peek_pos() == 100);
  REQUIRE(second.fm_write_queue().peek_pos() == FMQ_FRAC_ONE);
}

TEST_CASE("System owns independent YM2612 instances", "[system][fm]")
{
  generator::System first = make_system();
  generator::System second = make_system();

  REQUIRE(&first.ym2612() != &second.ym2612());
  REQUIRE(first.ym2612().init(1, FM_CLOCK, FM_SAMPLE_RATE, nullptr, nullptr) ==
          0);
  REQUIRE(second.ym2612().init(1, FM_CLOCK, FM_SAMPLE_RATE, nullptr, nullptr) ==
          0);
}

TEST_CASE("System owns independent controller ports", "[system][controller]")
{
  generator::System first = make_system();
  generator::System second = make_system();

  REQUIRE(&first.controllers() != &second.controllers());
  first.controllers().controller(0).start = 1;
  REQUIRE(second.controllers().controller(0).start == 0);
}

TEST_CASE("Active controller access routes to the registered System",
          "[system][controller]")
{
  generator::System system = make_system();
  ActiveSystem active{system};

  REQUIRE(&generator::controllers() == &system.controllers());
}

TEST_CASE("Active controller access rejects a missing System",
          "[system][controller]")
{
  generator::set_system(nullptr);
  REQUIRE_THROWS_AS(generator::controllers(), std::logic_error);
}

TEST_CASE("Active VDP access routes to the registered System", "[system][vdp]")
{
  generator::System system = make_system();
  ActiveSystem active{system};

  REQUIRE(&generator::vdp() == &system.vdp());
}

TEST_CASE("Active VDP access rejects a missing System", "[system][vdp]")
{
  generator::set_system(nullptr);
  REQUIRE_THROWS_AS(generator::vdp(), std::logic_error);
}

TEST_CASE("Legacy PSG API routes to the active System", "[system][psg]")
{
  generator::System system = make_system();
  ActiveSystem active{system};

  REQUIRE(SN76496Init(0, PSG_CLOCK, PSG_GAIN, PSG_SAMPLE_RATE) == 0);
  SN76496Write(0, 0x90);

  REQUIRE(system.psg().Volume[0] == system.psg().VolTable[0]);

  std::array<uint16, 4> samples{};
  SN76496Update(0, samples.data(), static_cast<int>(samples.size()));
  REQUIRE(samples[0] <= 0x7fff);
}

TEST_CASE("Legacy PSG API rejects calls without an active System",
          "[system][psg]")
{
  generator::set_system(nullptr);
  REQUIRE(SN76496Init(0, PSG_CLOCK, PSG_GAIN, PSG_SAMPLE_RATE) == -1);
  REQUIRE(SN76496Init(1, PSG_CLOCK, PSG_GAIN, PSG_SAMPLE_RATE) == -1);
}

TEST_CASE("Legacy FM write queue API routes to the active System",
          "[system][fmq]")
{
  generator::System system = make_system();
  ActiveSystem active{system};

  fmq_push(100, 1, 0xAA);
  REQUIRE(system.fm_write_queue().peek_pos() == 100);

  uint8_t port = 0;
  uint8_t value = 0;
  REQUIRE(fmq_pop(FMQ_FRAC_ONE, &port, &value));
  REQUIRE(port == 1);
  REQUIRE(value == 0xAA);
}

TEST_CASE("Legacy FM write queue API is safe without an active System",
          "[system][fmq]")
{
  generator::set_system(nullptr);

  fmq_reset();
  fmq_push(100, 1, 0xAA);
  REQUIRE_FALSE(fmq_pop(FMQ_FRAC_ONE, nullptr, nullptr));
  REQUIRE(fmq_peek_pos() == FMQ_FRAC_ONE);
  REQUIRE_FALSE(fmq_overflowed());
}

TEST_CASE("Legacy YM2612 API routes to the active System", "[system][fm]")
{
  generator::System system = make_system();
  ActiveSystem active{system};

  REQUIRE(YM2612Init(1, FM_CLOCK, FM_SAMPLE_RATE, nullptr, nullptr) == 0);
  REQUIRE(system.ym2612().init(1, FM_CLOCK, FM_SAMPLE_RATE, nullptr, nullptr) ==
          -1);
  REQUIRE(YM2612Write(0, 0, 0x22) == 0);
  REQUIRE(YM2612Write(0, 1, 0x08) == 0);
  YM2612Shutdown();
}

TEST_CASE("Legacy YM2612 API is safe without an active System", "[system][fm]")
{
  generator::set_system(nullptr);

  REQUIRE(YM2612Init(1, FM_CLOCK, FM_SAMPLE_RATE, nullptr, nullptr) == -1);
  REQUIRE(YM2612Read(0, 0) == 0);
  REQUIRE(YM2612Write(0, 0, 0x22) == 0);
  REQUIRE(YM2612TimerOver(0, 0) == 0);
  YM2612ResetChip(0);
  YM2612Shutdown();
  YM2612_save_state();
}
