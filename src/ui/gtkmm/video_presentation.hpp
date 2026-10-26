/* SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace generator::gtkui {

struct VideoOption {
  const char *value;
  const char *label;
  const char *description;
};

inline constexpr std::array aspect_options{
    VideoOption{"tv", "Original TV (4:3)",
                "Matches the shape of a Mega Drive / Genesis on a TV."},
    VideoOption{"pixels", "Square pixels",
                "Displays the game's pixels without horizontal correction."},
    VideoOption{"stretch", "Fill window",
                "Fills the available space; the picture may look stretched."}};
inline constexpr std::array scaling_options{
    VideoOption{
        "fit", "Fit to window",
        "Uses the largest picture that fits, with borders when needed."},
    VideoOption{"integer", "Integer scaling",
                "Uses whole-number vertical steps. Choose Square pixels for "
                "uniform pixels; small windows shrink to fit."}};
inline constexpr std::array sampling_options{
    VideoOption{"nearest", "Sharp (nearest)",
                "Keeps pixel edges sharp when resizing the picture."},
    VideoOption{"linear", "Smooth (bilinear)",
                "Softens pixel edges when resizing the picture."}};
inline constexpr std::array scaler_options{
    VideoOption{"none", "None",
                "Preserves the original pixel art. Sampling still controls how "
                "it is resized."},
    VideoOption{"scale2x", "Scale2x",
                "Smooths diagonal edges with a lightweight 2× filter."},
    VideoOption{"scale3x", "Scale3x",
                "Smooths diagonal edges with a lightweight 3× filter."},
    VideoOption{"scale4x", "Scale4x", "Applies Scale2x twice for a 4× image."},
    VideoOption{
        "xbrz2x", "xBRZ 2×",
        "Smooths pixel-art edges with a 2× image filter; uses more CPU."},
    VideoOption{
        "xbrz3x", "xBRZ 3×",
        "Smooths pixel-art edges with a 3× image filter; uses more CPU."},
    VideoOption{
        "xbrz4x", "xBRZ 4×",
        "Smooths pixel-art edges with a 4× image filter; uses more CPU."},
    VideoOption{
        "xbrz5x", "xBRZ 5×",
        "Smooths pixel-art edges with a 5× image filter; uses more CPU."},
    VideoOption{
        "xbrz6x", "xBRZ 6×",
        "Smooths pixel-art edges with a 6× image filter; uses more CPU."}};

enum class AspectRatio {
  TV,
  PIXELS,
  STRETCH
};
enum class ScalingMode {
  FIT,
  INTEGER
};
enum class Sampling {
  NEAREST,
  LINEAR
};
enum class Scaler {
  NONE,
  SCALE2X,
  SCALE3X,
  SCALE4X,
  XBRZ2X,
  XBRZ3X,
  XBRZ4X,
  XBRZ5X,
  XBRZ6X
};

struct FrameBounds {
  float x = 0;
  float y = 0;
  float width = 0;
  float height = 0;
};

struct VideoSettings {
  AspectRatio aspect = AspectRatio::TV;
  ScalingMode scaling = ScalingMode::FIT;
  Sampling sampling = Sampling::NEAREST;
  Scaler scaler = Scaler::NONE;

  static VideoSettings load();
  bool save() const;
  FrameBounds bounds(int viewport_width, int viewport_height, int source_width,
                     int source_height) const;
  bool operator==(const VideoSettings &) const = default;
};

struct ScaledFrame {
  int width = 0;
  int height = 0;
  std::span<const uint32_t> pixels;
};

/* Works on a private frame copy, never the emulator's render banks. The
 * returned span remains valid until the next apply() or destruction. */
class VideoFrameScaler {
public:
  ScaledFrame apply(std::span<const uint8_t> source, int width, int height,
                    int stride_bytes, Scaler scaler);

private:
  std::vector<uint32_t> m_source;
  std::vector<uint32_t> m_scaled;
  std::vector<uint32_t> m_temporary;
};

}  // namespace generator::gtkui
