/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "audio_preferences.hpp"
#include "gtkopts.h"
#include "platform/sdl3/audio_output.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <string_view>

namespace generator::gtkui {

AudioSettings AudioSettings::load()
{
  AudioSettings result;
  if (const char *volume = gtkopts_getvalue("audio_volume")) {
    char *end = nullptr;
    errno = 0;
    long value = std::strtol(volume, &end, 10);
    if (end != volume && *end == '\0' && errno != ERANGE)
      result.volume = static_cast<int>(std::clamp(value, 0L, 100L));
  }
  if (const char *muted = gtkopts_getvalue("audio_mute"))
    result.muted = std::string_view(muted) == "on";
  if (const char *driver = gtkopts_getvalue("audio_driver"); driver && *driver)
    result.driver = driver;
  return result;
}

bool AudioSettings::save() const
{
  bool ok =
      gtkopts_setvalue("audio_volume",
                       std::to_string(std::clamp(volume, 0, 100)).c_str()) == 0;
  ok = gtkopts_setvalue("audio_mute", muted ? "on" : "off") == 0 && ok;
  return gtkopts_setvalue("audio_driver",
                          driver.empty() ? "auto" : driver.c_str()) == 0 &&
         ok;
}

std::vector<AudioDriverOption>
audio_driver_options(const std::string &saved_driver)
{
  using generator::audio::SdlAudioOutput;
  std::vector<AudioDriverOption> options{
      {"auto", SdlAudioOutput::driver_label("auto"),
       "Chooses an available backend automatically. Recommended for most "
       "setups."}};
  for (int i = 0; i < SDL_GetNumAudioDrivers(); ++i) {
    const char *driver = SDL_GetAudioDriver(i);
    if (!driver)
      continue;
    std::string description = "Uses " + SdlAudioOutput::driver_label(driver) +
                              ". Takes effect after restarting Generator.";
    if (std::string_view(driver) == "dummy")
      description = "Runs without audible output. Useful for troubleshooting.";
    else if (std::string_view(driver) == "disk")
      description =
          "Writes audio to a file instead of playing it through speakers.";
    options.push_back(
        {driver, SdlAudioOutput::driver_label(driver), std::move(description)});
  }
  if (std::ranges::none_of(options, [&](const auto &option) {
        return option.value == saved_driver;
      }))
    options.push_back(
        {saved_driver, saved_driver + " (unavailable)",
         "This saved backend is not available in this installation. Choose "
         "Automatic or another available backend."});
  return options;
}

}  // namespace generator::gtkui
