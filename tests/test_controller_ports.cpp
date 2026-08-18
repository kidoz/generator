/* Genesis controller-port and six-button protocol tests. */

#include <catch2/catch_test_macros.hpp>

#include "controller_ports.hpp"

namespace {

void advance_to_six_button_phase(generator::ControllerPorts &ports, int player)
{
  ports.write_data(player, 0x40);
  ports.write_data(player, 0x00);
  ports.write_data(player, 0x40);
  ports.write_data(player, 0x00);
  ports.write_data(player, 0x40);
}

}  // namespace

TEST_CASE("Controller ports expose active-low three-button input",
          "[controller][io]")
{
  generator::ControllerPorts ports;
  ports.set_control(0, 0x40);
  ports.write_data(0, 0x40);

  REQUIRE(ports.read_data(0) == 0x7F);

  auto &controller = ports.controller(0);
  controller.up = 1;
  controller.b = 1;
  REQUIRE(ports.read_data(0) == 0x6E);

  ports.write_data(0, 0x00);
  REQUIRE(ports.read_data(0) == 0x32);
}

TEST_CASE("Controller ports implement the six-button TH handshake",
          "[controller][io]")
{
  generator::ControllerPorts ports;
  ports.set_control(0, 0x40);
  ports.controller(0).x = 1;
  ports.controller(0).mode = 1;

  ports.write_data(0, 0x40);
  REQUIRE(ports.read_data(0) == 0x7F);
  ports.write_data(0, 0x00);
  REQUIRE(ports.read_data(0) == 0x33);

  ports.write_data(0, 0x40);
  REQUIRE(ports.read_data(0) == 0x7F);
  ports.write_data(0, 0x00);
  REQUIRE(ports.read_data(0) == 0x30);

  ports.write_data(0, 0x40);
  REQUIRE(ports.read_data(0) == 0x76);

  ports.write_data(0, 0x00);
  REQUIRE(ports.read_data(0) == 0x3F);
}

TEST_CASE("Controller six-button phase expires after the scanline timeout",
          "[controller][io]")
{
  generator::ControllerPorts ports;
  ports.set_control(0, 0x40);
  ports.controller(0).x = 1;

  advance_to_six_button_phase(ports, 0);
  REQUIRE(ports.read_data(0) == 0x7E);

  for (int scanline = 0; scanline < 27; ++scanline)
    ports.refresh();

  REQUIRE(ports.read_data(0) == 0x7F);
}

TEST_CASE("Controller ports keep players and instances independent",
          "[controller][system]")
{
  generator::ControllerPorts first;
  generator::ControllerPorts second;
  first.controller(0).a = 1;
  first.controller(1).b = 1;
  first.set_control(2, 0xFF);
  first.write_data(2, 0xA5);

  REQUIRE(first.controller(0).a == 1);
  REQUIRE(first.controller(1).a == 0);
  REQUIRE(first.controller(1).b == 1);
  REQUIRE(second.controller(0).a == 0);
  REQUIRE(first.read_data(2) == 0xA5);

  first.reset();
  REQUIRE(first.controller(0).a == 0);
  REQUIRE(first.controller(1).b == 0);
  REQUIRE(first.control(2) == 0);
  REQUIRE(first.read_data(2) == 0);
}
