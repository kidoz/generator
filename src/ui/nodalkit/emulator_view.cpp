/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Emulator display widget for the NodalKit UI backend */

#include "emulator_view.hpp"
#include "ui_bridge.hpp"

#include <nk/render/snapshot_context.h>

#include <algorithm>

namespace generator::nkui {

namespace {

/* Mega Drive output was always shown on a 4:3 television, whether the VDP
 * ran 320 or 256 pixels wide. */
constexpr float kDisplayAspect = 4.0F / 3.0F;

/* The letterbox bars around the stage. Deliberately darker than any theme
 * surface so the screen edge stays readable in both light and dark mode. */
constexpr nk::Color kBezel{0.02F, 0.024F, 0.027F, 1.0F};

}  // namespace

std::shared_ptr<EmulatorView> EmulatorView::create()
{
  return std::shared_ptr<EmulatorView>(new EmulatorView());
}

EmulatorView::EmulatorView()
{
  set_debug_name("EmulatorView");
  set_horizontal_size_policy(nk::SizePolicy::Expanding);
  set_vertical_size_policy(nk::SizePolicy::Expanding);
  set_focusable(true);

  screen_ = nk::ImageView::create();
  /* The stage is already 4:3; the field stretches to fill it, exactly as a
   * television scaled non-square Mega Drive pixels. */
  screen_->set_preserve_aspect_ratio(false);
  screen_->set_scale_mode(nk::ScaleMode::NearestNeighbor);
  append_child(screen_);
}

EmulatorView::~EmulatorView() = default;

bool EmulatorView::pump()
{
  if (!g_frames.acquire())
    return false;

  const int width = g_frames.width();
  const int height = g_frames.height();
  if (width <= 0 || height <= 0)
    return false;

  screen_->update_pixel_buffer(g_frames.pixels(), width, height);
  frames_since_sample_.fetch_add(1, std::memory_order_relaxed);
  return true;
}

void EmulatorView::set_scale_mode(nk::ScaleMode mode)
{
  screen_->set_scale_mode(mode);
}

int EmulatorView::source_width() const
{
  return screen_->source_width();
}

int EmulatorView::source_height() const
{
  return screen_->source_height();
}

nk::SizeRequest
EmulatorView::measure(const nk::Constraints & /*constraints*/) const
{
  /* One Mega Drive pixel per logical pixel is the floor; two is a
   * comfortable default window size. */
  return {320.0F, 240.0F, 640.0F, 480.0F};
}

void EmulatorView::allocate(const nk::Rect &allocation)
{
  Widget::allocate(allocation);

  /* Largest 4:3 rectangle that fits, centred. */
  float width = allocation.width;
  float height = width / kDisplayAspect;
  if (height > allocation.height) {
    height = allocation.height;
    width = height * kDisplayAspect;
  }
  width = std::max(0.0F, width);
  height = std::max(0.0F, height);

  screen_->allocate({
      allocation.x + ((allocation.width - width) / 2.0F),
      allocation.y + ((allocation.height - height) / 2.0F),
      width,
      height,
  });
}

void EmulatorView::snapshot(nk::SnapshotContext &ctx) const
{
  ctx.add_color_rect(allocation(), kBezel);
  Widget::snapshot(ctx);
}

}  // namespace generator::nkui
