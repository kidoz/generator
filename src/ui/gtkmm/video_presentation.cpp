/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "video_presentation.hpp"

#include "gtkopts.h"
#include "generator.h"
#include "uiplot.h"
#include "xbrz_wrapper.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace generator::gtkui {
namespace {

template <typename Enum, size_t N>
Enum read_option(const char *key, const std::array<VideoOption, N> &options)
{
  const char *value = gtkopts_getvalue(key);
  for (size_t i = 0; value != nullptr && i < options.size(); ++i)
    if (value == std::string_view(options[i].value))
      return static_cast<Enum>(i);
  return static_cast<Enum>(0);
}

template <typename Enum, size_t N>
bool write_option(const char *key, Enum value,
                  const std::array<VideoOption, N> &options)
{
  auto index = static_cast<size_t>(value);
  if (index >= options.size())
    index = 0;
  return gtkopts_setvalue(key, options[index].value) == 0;
}

}  // namespace

VideoSettings VideoSettings::load()
{
  return {read_option<AspectRatio>("video_aspect", aspect_options),
          read_option<ScalingMode>("video_scaling", scaling_options),
          read_option<Sampling>("video_sampling", sampling_options),
          read_option<Scaler>("scaler", scaler_options)};
}

bool VideoSettings::save() const
{
  // Write every field, even when an earlier allocation failed.
  bool ok = write_option("video_aspect", aspect, aspect_options);
  ok = write_option("video_scaling", scaling, scaling_options) && ok;
  ok = write_option("video_sampling", sampling, sampling_options) && ok;
  return write_option("scaler", scaler, scaler_options) && ok;
}

FrameBounds VideoSettings::bounds(int viewport_width, int viewport_height,
                                  int source_width, int source_height) const
{
  if (viewport_width <= 0 || viewport_height <= 0 || source_width <= 0 ||
      source_height <= 0)
    return {};
  if (aspect == AspectRatio::STRETCH)
    return {0, 0, static_cast<float>(viewport_width),
            static_cast<float>(viewport_height)};
  double base_width =
      aspect == AspectRatio::TV ? source_height * (4.0 / 3.0) : source_width;
  double scale = std::min(viewport_width / base_width,
                          static_cast<double>(viewport_height) / source_height);
  if (scaling == ScalingMode::INTEGER && scale >= 1)
    scale = std::floor(scale);
  float width = static_cast<float>(base_width * scale);
  float height = static_cast<float>(source_height * scale);
  return {std::floor((viewport_width - width) / 2),
          std::floor((viewport_height - height) / 2), width, height};
}

ScaledFrame VideoFrameScaler::apply(std::span<const uint8_t> source, int width,
                                    int height, int stride_bytes, Scaler scaler)
{
  // Genesis fields are at most 320×240. Bounding sizes before multiplying
  // also makes corrupt dimensions safe to reject.
  if (width <= 0 || width > 320 || height <= 0 || height > 240 ||
      stride_bytes < width * 4)
    return {};
  size_t required = static_cast<size_t>(height - 1) * stride_bytes + width * 4;
  if (source.size() < required)
    return {};
  m_source.resize(static_cast<size_t>(width) * height);
  for (int y = 0; y < height; ++y)
    std::memcpy(m_source.data() + y * width,
                source.data() + static_cast<size_t>(y) * stride_bytes,
                static_cast<size_t>(width) * 4);

  int factor = 1;
  switch (scaler) {
  case Scaler::SCALE2X:
  case Scaler::XBRZ2X:
    factor = 2;
    break;
  case Scaler::SCALE3X:
  case Scaler::XBRZ3X:
    factor = 3;
    break;
  case Scaler::SCALE4X:
  case Scaler::XBRZ4X:
    factor = 4;
    break;
  case Scaler::XBRZ5X:
    factor = 5;
    break;
  case Scaler::XBRZ6X:
    factor = 6;
    break;
  case Scaler::NONE:
  default:
    break;
  }
  if (factor == 1)
    return {width, height, m_source};
  int output_width = width * factor;
  int output_height = height * factor;
  m_scaled.resize(static_cast<size_t>(output_width) * output_height);
  switch (scaler) {
  case Scaler::SCALE2X:
    uiplot_scale2x_frame32(m_source.data(), m_scaled.data(), width, height,
                           width * 4, output_width * 4);
    break;
  case Scaler::SCALE3X:
    uiplot_scale3x_frame32(m_source.data(), m_scaled.data(), width, height,
                           width * 4, output_width * 4);
    break;
  case Scaler::SCALE4X:
    m_temporary.resize(static_cast<size_t>(width) * height * 4);
    uiplot_scale4x_frame32(m_source.data(), m_scaled.data(), m_temporary.data(),
                           width, height, width * 4, output_width * 4);
    break;
  default:
    xbrz_scale(factor, m_source.data(), m_scaled.data(), width, height);
    break;
  }
  // xBRZ's RGB path does not promise to retain the unused alpha byte.
  for (auto &pixel : m_scaled)
    pixel |= 0xff000000u;
  return {output_width, output_height, m_scaled};
}

}  // namespace generator::gtkui
