/* SPDX-License-Identifier: GPL-2.0-or-later */
/* ILogger over a pair of output streams, shared by the windowed backends */

#pragma once

#include "interfaces/logger.hpp"

#include <iosfwd>

namespace generator::ui {

/* Writes log messages to two streams: everything from Verbose up to User on
 * the normal stream, and Critical on the error stream with a marker. Debug
 * levels are dropped -- the emulator's own verbosity gate (gen_loglevel)
 * decides whether they are ever emitted, and a windowed backend has nowhere
 * useful to put them.
 *
 * The gtkmm and NodalKit backends carried byte-identical copies of this. */
class StreamLogger : public ILogger {
public:
  /* Defaults to std::cout and std::cerr. */
  StreamLogger();

  StreamLogger(std::ostream &out, std::ostream &err);

  void log(LogLevel level, std::string_view message) override;

private:
  std::ostream &out_;
  std::ostream &err_;
};

}  // namespace generator::ui
