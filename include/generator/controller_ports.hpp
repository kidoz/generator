/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Genesis controller port state and six-button handshake protocol. */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

/* Legacy input-state name retained while UI and netplay interfaces migrate. */
struct t_keys {
  unsigned int a = 0;
  unsigned int b = 0;
  unsigned int c = 0;
  unsigned int up = 0;
  unsigned int down = 0;
  unsigned int left = 0;
  unsigned int right = 0;
  unsigned int start = 0;
  unsigned int x = 0;
  unsigned int y = 0;
  unsigned int z = 0;
  unsigned int mode = 0;
};

namespace generator {

/* Two controller data ports plus the external port latches. The class owns
 * both host input state and the Genesis six-button TH-edge/timeout protocol;
 * mem68k remains responsible only for mapping hardware addresses to it. */
class ControllerPorts {
public:
  void reset();
  void refresh();

  t_keys &controller(int player);
  const t_keys &controller(int player) const;

  std::uint8_t read_data(int port) const;
  void write_data(int port, std::uint8_t data);
  std::uint8_t control(int port) const;
  void set_control(int port, std::uint8_t data);

private:
  std::array<t_keys, 2> m_controllers{};
  std::array<std::uint8_t, 3> m_control{};
  std::array<std::uint8_t, 3> m_output{};
  std::array<std::uint8_t, 2> m_counter{};
  std::array<std::uint8_t, 2> m_timeout{};
  std::array<std::uint8_t, 2> m_previous_th{};

  void update_th(int player, std::uint8_t new_th);
  std::uint8_t read_controller(int player, bool th_high) const;
};

/* Checked access to the active System-owned controller ports. */
ControllerPorts &controllers();

}  // namespace generator
