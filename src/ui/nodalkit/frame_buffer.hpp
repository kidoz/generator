/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Frame handoff between the emulation thread and the NodalKit UI thread */

#pragma once

#include <cstdint>
#include <mutex>
#include <vector>

namespace generator::nkui {

/* Triple-buffered ARGB8888 field handoff.
 *
 * The emulation thread converts scanlines into the back field and publishes
 * it when the field completes; the UI thread adopts the most recently
 * published field and silently drops any it never got around to consuming.
 * Rows are packed at the field's own width, which is what nk::ImageView
 * expects - it copies width * height contiguous pixels. */
class FrameBuffer {
public:
  /* A Mega Drive field is at most 320x240 visible pixels: H32 mode narrows
   * to 256 columns, and only PAL 240-line mode uses the full height. */
  static constexpr int kMaxWidth = 320;
  static constexpr int kMaxHeight = 240;

  FrameBuffer();

  /* Emulation thread: start of the row for `line` in a field `width` wide. */
  uint32_t *row(int line, int width);

  /* Emulation thread: publish the field that was just rendered. */
  void publish(int width, int height);

  /* UI thread: adopt the newest published field. False when nothing new
   * arrived since the previous call. */
  bool acquire();

  /* UI thread: the field adopted by the last successful acquire(). */
  const uint32_t *pixels() const
  {
    return front_.pixels.data();
  }

  int width() const
  {
    return front_.width;
  }

  int height() const
  {
    return front_.height;
  }

private:
  struct Field {
    std::vector<uint32_t> pixels;
    int width = 0;
    int height = 0;
  };

  Field back_;  /* written by the emulation thread */
  Field ready_; /* published, waiting for the UI thread */
  Field front_; /* owned by the UI thread */
  bool ready_valid_ = false;
  std::mutex mutex_;
};

}  // namespace generator::nkui
