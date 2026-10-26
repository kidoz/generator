/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "audio_preferences.hpp"
#include "platform/sdl3/audio_output.hpp"
#include "gtkopts.h"
#include "gensoundp.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>

using generator::audio::SdlAudioOutput;
using generator::gtkui::AudioSettings;

namespace {

class OptionsScope {
public:
  OptionsScope() : previous_(gtkopts_conf)
  {
    gtkopts_conf = nullptr;
  }
  ~OptionsScope()
  {
    clear();
    gtkopts_conf = previous_;
  }
  static void clear()
  {
    while (gtkopts_conf) {
      auto *next = gtkopts_conf->next;
      std::free(gtkopts_conf->key);
      std::free(gtkopts_conf->value);
      std::free(gtkopts_conf);
      gtkopts_conf = next;
    }
  }

private:
  t_conf *previous_;
};

struct OutputScope {
  OutputScope()
  {
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    SdlAudioOutput::set_controls(100, false);
  }
  ~OutputScope()
  {
    soundp_stop();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    SdlAudioOutput::set_controls(100, false);
  }
};

}  // namespace

TEST_CASE("audio settings persist volume, mute and unavailable backend choices",
          "[audio][preferences]")
{
  OptionsScope scope;
  CHECK(AudioSettings::load() == AudioSettings{});
  AudioSettings custom{37, true, "missing-backend"};
  REQUIRE(custom.save());
  CHECK(AudioSettings::load() == custom);
  auto path =
      std::filesystem::current_path() /
      ("audio-options-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()) +
       ".conf");
  REQUIRE(gtkopts_save(path.c_str()) == 0);
  OptionsScope::clear();
  REQUIRE(gtkopts_load(path.c_str()) == 0);
  std::filesystem::remove(path);
  CHECK(AudioSettings::load() == custom);
  auto drivers = generator::gtkui::audio_driver_options(custom.driver);
  REQUIRE_FALSE(drivers.empty());
  CHECK(drivers.front().value == "auto");
  CHECK(drivers.back().value == "missing-backend");
  CHECK(drivers.back().label.find("unavailable") != std::string::npos);
}

TEST_CASE("invalid saved audio levels cannot amplify output",
          "[audio][preferences]")
{
  OptionsScope scope;
  REQUIRE(gtkopts_setvalue("audio_volume", "250") == 0);
  CHECK(AudioSettings::load().volume == 100);
  REQUIRE(gtkopts_setvalue("audio_volume", "-10") == 0);
  CHECK(AudioSettings::load().volume == 0);
  REQUIRE(gtkopts_setvalue("audio_volume", "garbage") == 0);
  CHECK(AudioSettings::load().volume == 100);
  REQUIRE(gtkopts_setvalue("audio_volume", "9999999999999999999999999999") ==
          0);
  CHECK(AudioSettings::load().volume == 100);
}

TEST_CASE(
    "SDL playback gain mutes and restores volume and survives output restart",
    "[audio][sdl]")
{
  OutputScope output;
  REQUIRE(SdlAudioOutput::set_controls(37, false));
  REQUIRE(soundp_start() == 0);
  auto state = SdlAudioOutput::status();
  REQUIRE(state.active);
  CHECK(state.driver == "dummy");
  CHECK(state.sample_rate == SOUND_SAMPLERATE);
  CHECK(state.channels == 2);
  CHECK(state.gain == Catch::Approx(0.37));
  CHECK(std::string(soundp_backend_name()) == "Silent (dummy)");
  REQUIRE(SdlAudioOutput::set_controls(37, true));
  CHECK(SdlAudioOutput::status().gain == 0);
  CHECK_FALSE(SdlAudioOutput::play_test());
  REQUIRE(soundp_reset() == 0);
  CHECK(SdlAudioOutput::status().gain == 0);
  REQUIRE(SdlAudioOutput::set_controls(37, false));
  CHECK(SdlAudioOutput::status().gain == Catch::Approx(0.37));
  REQUIRE(SdlAudioOutput::set_controls(200, false));
  CHECK(SdlAudioOutput::status().gain == 1);
  REQUIRE(SdlAudioOutput::set_controls(-10, false));
  CHECK(SdlAudioOutput::status().gain == 0);
}

TEST_CASE("speaker test leaves the game audio queue unchanged", "[audio][sdl]")
{
  OutputScope output;
  REQUIRE(soundp_start() == 0);
  soundp_pause();
  soundp_flush();
  REQUIRE(soundp_samplesbuffered() == 0);
  REQUIRE(SdlAudioOutput::play_test());
  CHECK(soundp_samplesbuffered() == 0);
  REQUIRE(SdlAudioOutput::set_controls(50, true));
  SdlAudioOutput::stop_test();
  CHECK(soundp_samplesbuffered() == 0);
  soundp_stop();
  CHECK_FALSE(SdlAudioOutput::status().active);
  CHECK_FALSE(SdlAudioOutput::play_test());
}
