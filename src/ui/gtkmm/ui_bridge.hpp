/* SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "emulator_core.hpp"
#include <memory>

// Global emulator core instance
extern std::unique_ptr<generator::EmulatorCore> g_emulator_core;
