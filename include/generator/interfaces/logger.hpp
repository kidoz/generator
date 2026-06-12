/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Logger Interface - Modern C++ DI abstraction */

#pragma once

#include <string_view>

namespace generator {

enum class LogLevel {
    None = 0,
    Critical = 1,
    Normal = 2,
    Verbose = 3,
    User = 4,
    Debug1 = 5,
    Debug2 = 6,
    Debug3 = 7
};

class ILogger {
public:
    virtual ~ILogger() = default;

    /*
     * Output a log message at the specified level.
     */
    virtual void log(LogLevel level, std::string_view message) = 0;
};

} // namespace generator
