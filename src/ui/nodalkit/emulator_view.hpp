/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Emulator display widget for the NodalKit UI backend */

#pragma once

#include <nk/render/image_node.h>
#include <nk/ui_core/widget.h>
#include <nk/widgets/image_view.h>

#include <atomic>
#include <memory>

namespace generator::nkui {

/* The emulator screen.
 *
 * A Mega Drive frame is 320x224 (or 256x224 in H32 mode) but was always
 * shown on a 4:3 television, so the widget letterboxes a 4:3 stage inside
 * its allocation and stretches the field over it. Both H32 and H40 fill the
 * same stage, which is what the hardware did. */
class EmulatorView : public nk::Widget {
public:
  static std::shared_ptr<EmulatorView> create();
  ~EmulatorView() override;

  /* UI thread: adopt the newest completed field, if any. Returns true when
   * the display was updated. */
  bool pump();

  /* Emulated fields displayed since the last call, then reset. */
  unsigned int sample_frames()
  {
    return frames_since_sample_.exchange(0, std::memory_order_relaxed);
  }

  void set_scale_mode(nk::ScaleMode mode);

  /* Source dimensions of the field currently on screen. */
  int source_width() const;
  int source_height() const;

  // --- Widget overrides ---
  [[nodiscard]] nk::SizeRequest
  measure(const nk::Constraints &constraints) const override;
  void allocate(const nk::Rect &allocation) override;

protected:
  EmulatorView();
  void snapshot(nk::SnapshotContext &ctx) const override;

private:
  std::shared_ptr<nk::ImageView> screen_;
  std::atomic<unsigned int> frames_since_sample_{0};
};

}  // namespace generator::nkui
