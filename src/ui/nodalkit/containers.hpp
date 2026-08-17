/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Minimal container widgets for the NodalKit UI backend */

#pragma once

#include <nk/layout/box_layout.h>
#include <nk/ui_core/widget.h>

#include <memory>
#include <utility>

namespace generator::nkui {

/* NodalKit keeps Widget::append_child() protected, so a container is a
 * two-line subclass rather than a stock widget. This is the same shape the
 * upstream examples use. */
class Box : public nk::Widget {
public:
  static std::shared_ptr<Box> create(nk::Orientation orientation,
                                     float spacing = 0.0F)
  {
    auto box = std::shared_ptr<Box>(new Box());
    auto layout = std::make_unique<nk::BoxLayout>(orientation);
    layout->set_spacing(spacing);
    box->set_layout_manager(std::move(layout));
    return box;
  }

  static std::shared_ptr<Box> vertical(float spacing = 0.0F)
  {
    return create(nk::Orientation::Vertical, spacing);
  }

  static std::shared_ptr<Box> horizontal(float spacing = 0.0F)
  {
    return create(nk::Orientation::Horizontal, spacing);
  }

  void append(std::shared_ptr<nk::Widget> child)
  {
    append_child(std::move(child));
  }

private:
  Box() = default;
};

}  // namespace generator::nkui
