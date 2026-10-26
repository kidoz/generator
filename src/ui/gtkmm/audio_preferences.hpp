/* SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include <string>
#include <vector>

namespace generator::gtkui {

struct AudioSettings {
  int volume = 100;
  bool muted = false;
  std::string driver = "auto";

  static AudioSettings load();
  bool save() const;
  bool operator==(const AudioSettings &) const = default;
};

struct AudioDriverOption {
  std::string value;
  std::string label;
  std::string description;
};

std::vector<AudioDriverOption>
audio_driver_options(const std::string &saved_driver);

}  // namespace generator::gtkui
