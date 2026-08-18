/* SPDX-License-Identifier: GPL-2.0-or-later */
/* ILogger over a pair of output streams, shared by the windowed backends */

#include "stream_logger.hpp"

#include <iostream>

namespace generator::ui {

StreamLogger::StreamLogger() : out_(std::cout), err_(std::cerr)
{
}

StreamLogger::StreamLogger(std::ostream &out, std::ostream &err)
    : out_(out), err_(err)
{
}

void StreamLogger::log(LogLevel level, std::string_view message)
{
  switch (level) {
  case LogLevel::None:
  case LogLevel::Debug3:
  case LogLevel::Debug2:
  case LogLevel::Debug1:
    break;
  case LogLevel::Verbose:
  case LogLevel::Normal:
  case LogLevel::User:
    out_ << message << std::endl;
    break;
  case LogLevel::Critical:
    err_ << "CRITICAL: " << message << std::endl;
    break;
  }
}

}  // namespace generator::ui
