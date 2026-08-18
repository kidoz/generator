/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Genesis controller port and six-button protocol implementation. */

#include "controller_ports.hpp"

namespace generator {

void ControllerPorts::reset()
{
  m_controllers = {};
  m_control = {};
  m_output = {};
  m_counter = {};
  m_timeout = {};
  m_previous_th = {};
}

void ControllerPorts::refresh()
{
  for (std::size_t player = 0; player < m_timeout.size(); ++player) {
    if (m_timeout[player]++ > 25) {
      m_counter[player] = 0;
      m_timeout[player] = 0;
    }
  }
}

t_keys &ControllerPorts::controller(int player)
{
  return m_controllers[player];
}

const t_keys &ControllerPorts::controller(int player) const
{
  return m_controllers[player];
}

std::uint8_t ControllerPorts::read_data(int port) const
{
  const std::uint8_t input =
      port < 2 ? read_controller(port, (m_output[port] >> 6) & 1) : 0;
  return (input & ~m_control[port]) | (m_output[port] & m_control[port]);
}

void ControllerPorts::write_data(int port, std::uint8_t data)
{
  if (port < 2)
    update_th(port, (data >> 6) & 1);
  m_output[port] = data;
}

std::uint8_t ControllerPorts::control(int port) const
{
  return m_control[port];
}

void ControllerPorts::set_control(int port, std::uint8_t data)
{
  m_control[port] = data;
}

void ControllerPorts::update_th(int player, std::uint8_t new_th)
{
  const std::uint8_t previous_th = m_previous_th[player];
  m_previous_th[player] = new_th;

  if (new_th && !previous_th) {
    m_counter[player] = (m_counter[player] + 2) & 6;
    m_timeout[player] = 0;
  }
}

std::uint8_t ControllerPorts::read_controller(int player, bool th_high) const
{
  const t_keys &controller = m_controllers[player];
  const std::uint8_t counter = m_counter[player];

  if (th_high) {
    if (counter == 6) {
      return (1 - controller.x) | ((1 - controller.y) << 1) |
             ((1 - controller.z) << 2) | ((1 - controller.mode) << 3) |
             ((1 - controller.b) << 4) | ((1 - controller.c) << 5);
    }
    return (1 - controller.up) | ((1 - controller.down) << 1) |
           ((1 - controller.left) << 2) | ((1 - controller.right) << 3) |
           ((1 - controller.b) << 4) | ((1 - controller.c) << 5);
  }

  if (counter == 4) {
    return ((1 - controller.a) << 4) | ((1 - controller.start) << 5);
  }
  if (counter == 6) {
    return ((1 - controller.a) << 4) | ((1 - controller.start) << 5) | 0x0f;
  }
  return (1 - controller.up) | ((1 - controller.down) << 1) |
         ((1 - controller.a) << 4) | ((1 - controller.start) << 5);
}

}  // namespace generator
