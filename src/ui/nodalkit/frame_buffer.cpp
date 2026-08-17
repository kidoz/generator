/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Frame handoff between the emulation thread and the NodalKit UI thread */

#include "frame_buffer.hpp"

#include <algorithm>
#include <utility>

namespace generator::nkui {

namespace {

constexpr std::size_t kFieldPixels =
    static_cast<std::size_t>(FrameBuffer::kMaxWidth) *
    static_cast<std::size_t>(FrameBuffer::kMaxHeight);

}  // namespace

FrameBuffer::FrameBuffer()
{
  /* Every field is allocated at the maximum size once, so neither thread
   * ever reallocates while the other holds a pointer into a buffer. */
  back_.pixels.assign(kFieldPixels, 0xFF000000U);
  ready_.pixels.assign(kFieldPixels, 0xFF000000U);
  front_.pixels.assign(kFieldPixels, 0xFF000000U);
}

uint32_t *FrameBuffer::row(int line, int width)
{
  if (line < 0 || line >= kMaxHeight || width <= 0 || width > kMaxWidth)
    return nullptr;

  return back_.pixels.data() +
         static_cast<std::size_t>(line) * static_cast<std::size_t>(width);
}

void FrameBuffer::publish(int width, int height)
{
  back_.width = std::clamp(width, 0, kMaxWidth);
  back_.height = std::clamp(height, 0, kMaxHeight);

  const std::lock_guard<std::mutex> lock(mutex_);
  std::swap(back_, ready_);
  ready_valid_ = true;
}

bool FrameBuffer::acquire()
{
  const std::lock_guard<std::mutex> lock(mutex_);
  if (!ready_valid_)
    return false;

  std::swap(ready_, front_);
  ready_valid_ = false;
  return true;
}

}  // namespace generator::nkui
