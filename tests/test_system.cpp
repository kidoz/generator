// System ownership and transitional SN76496 compatibility tests.

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <memory>

#include "machine.h"
#include "sn76496.hpp"
#include "system.hpp"

namespace {

constexpr int PSG_CLOCK = 3579545;
constexpr int PSG_SAMPLE_RATE = 44100;
constexpr int PSG_GAIN = 0;

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
