/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Preferences dialog for the NodalKit UI backend */

#include "preferences_dialog.hpp"

#include <nk/widgets/preferences.h>

#include <utility>

namespace generator::nkui {

PreferencesDialog::PreferencesDialog(Options options)
    : options_(std::move(options))
{
}

void PreferencesDialog::present(nk::Window &window)
{
  if (dialog_ && dialog_->is_presented())
    return;

  auto page = nk::PreferencesPage::create();

  auto video = nk::PreferencesGroup::create("Video");

  auto smooth = nk::SwitchRow::create(
      "Smooth scaling", "Bilinear filtering instead of sharp pixels");
  smooth->set_active(options_.smooth_scaling());
  smooth_conn_ = nk::ScopedConnection(smooth->on_toggled().connect(
      [this](bool on) { options_.set_smooth_scaling(on); }));
  video->add(smooth);

  auto mode = nk::ComboRow::create("Video mode",
                                   "Autodetect applies when a ROM is loaded");
  mode->set_items({"Autodetect", "NTSC (60 Hz)", "PAL (50 Hz)"});
  mode->set_selected_index(options_.video_mode());
  mode_conn_ = nk::ScopedConnection(mode->on_selection_changed().connect(
      [this](int index) { options_.set_video_mode(index); }));
  video->add(mode);

  page->add(video);

  auto appearance = nk::PreferencesGroup::create("Appearance");

  auto scheme = nk::ComboRow::create("Color scheme");
  scheme->set_items({"System", "Light", "Dark"});
  scheme->set_selected_index(options_.color_scheme());
  scheme_conn_ = nk::ScopedConnection(scheme->on_selection_changed().connect(
      [this](int index) { options_.set_color_scheme(index); }));
  appearance->add(scheme);

  page->add(appearance);

  dialog_ = nk::Dialog::create("Preferences");
  dialog_->set_content(page);
  dialog_->set_minimum_panel_width(420.0F);
  dialog_->add_button("Close", nk::DialogResponse::Close);
  dialog_->present(window);
}

}  // namespace generator::nkui
