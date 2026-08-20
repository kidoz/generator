/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Preferences dialog for the NodalKit UI backend */

#pragma once

#include <nk/foundation/signal.h>
#include <nk/platform/window.h>
#include <nk/widgets/dialog.h>

#include <functional>
#include <memory>

namespace generator::nkui {

/* Live-apply preferences surface. The dialog owns no state of its own: it
 * reads current values through the getters when presented and pushes every
 * change through the setters immediately, so there is no Apply/Cancel
 * transaction to get out of sync with the menus. */
class PreferencesDialog {
public:
  struct Options {
    /* Bilinear filtering instead of sharp pixels. */
    std::function<bool()> smooth_scaling;
    std::function<void(bool)> set_smooth_scaling;

    /* 0 = autodetect from the ROM, 1 = NTSC, 2 = PAL. */
    std::function<int()> video_mode;
    std::function<void(int)> set_video_mode;

    /* 0 = follow the system, 1 = light, 2 = dark. */
    std::function<int()> color_scheme;
    std::function<void(int)> set_color_scheme;
  };

  explicit PreferencesDialog(Options options);

  /* Build a dialog reflecting the current values and present it. No-op
   * while a previous presentation is still open. */
  void present(nk::Window &window);

private:
  Options options_;
  std::shared_ptr<nk::Dialog> dialog_;
  nk::ScopedConnection smooth_conn_;
  nk::ScopedConnection mode_conn_;
  nk::ScopedConnection scheme_conn_;
};

}  // namespace generator::nkui
