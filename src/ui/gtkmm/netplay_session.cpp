/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "netplay_session.hpp"

#include "emulator_core.hpp"
#include "input_controller.hpp"

#if defined(NETPLAY)
#include "netplay.h"
#endif

std::mutex &generator::gtkui::NetplaySession::mutex()
{
  static std::mutex mutex;
  return mutex;
}

void generator::gtkui::NetplaySession::poll()
{
#if defined(NETPLAY)
  std::lock_guard lock(mutex());
  netplay_poll();
#endif
}

void netplay_on_frame(generator::EmulatorCore &core)
{
#if !defined(NETPLAY)
  (void)core;
#else
  std::lock_guard lock(generator::gtkui::NetplaySession::mutex());
  if (netplay_get_state() == NETPLAY_DISCONNECTED) {
    input_set_netplay_drive(false);
    return;
  }

  if (netplay_get_state() != NETPLAY_IN_GAME) {
    input_set_netplay_drive(false);
    return;
  }

  /* The emulation thread owns set_input for the duration of the match
   * so a keyboard event cannot overwrite the remote pad mid-frame. */
  input_set_netplay_drive(true);

  unsigned int buttons[PAD_BUTTON_COUNT] = {};
  input_copy_local_pad(0, buttons);

  netplay_input_t local{};
  local.up = (uint8_t)buttons[PAD_UP];
  local.down = (uint8_t)buttons[PAD_DOWN];
  local.left = (uint8_t)buttons[PAD_LEFT];
  local.right = (uint8_t)buttons[PAD_RIGHT];
  local.a = (uint8_t)buttons[PAD_A];
  local.b = (uint8_t)buttons[PAD_B];
  local.c = (uint8_t)buttons[PAD_C];
  local.start = (uint8_t)buttons[PAD_START];
  local.x = (uint8_t)buttons[PAD_X];
  local.y = (uint8_t)buttons[PAD_Y];
  local.z = (uint8_t)buttons[PAD_Z];
  local.mode = (uint8_t)buttons[PAD_MODE];

  netplay_input_t players[8]{};
  if (netplay_sync_frame(&local, players, 8) != 0)
    return;

  int count = netplay_player_count();
  if (count > 2)
    count = 2;
  for (int player = 0; player < count; player++) {
    const netplay_input_t &pad = players[player];
    core.set_input(player, pad.up, pad.down, pad.left, pad.right, pad.start,
                   pad.a, pad.b, pad.c, pad.x, pad.y, pad.z, pad.mode);
  }
#endif
}
