/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Exercise the real dialog, GTK main loop, worker and network timeout.
 * A display is optional; the protocol/session suite also runs headlessly. */

#include "netplay_dialog.hpp"
#include "emulator_thread.hpp"
#include "input_controller.hpp"
#include "netplay_session.hpp"
#include "ui_bridge.hpp"
#include "netplay.h"
#include "socket.h"
#include "support_netplay_server.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <algorithm>
#include <thread>

std::unique_ptr<generator::EmulatorCore> g_emulator_core;

void input_set_netplay_drive(bool)
{
}
void input_copy_local_pad(int, unsigned int *buttons)
{
  std::fill_n(buttons, PAD_BUTTON_COUNT, 0);
}

namespace {

bool init_gtk()
{
  if (!gtk_init_check())
    return false;
  static auto app = Gtk::Application::create(
      "org.generator.NetplayTest", Gio::Application::Flags::NON_UNIQUE);
  app->register_application();
  return true;
}

template <typename T, typename Predicate>
T *find_widget(Gtk::Widget &root, Predicate predicate)
{
  if (auto *widget = dynamic_cast<T *>(&root); widget && predicate(*widget))
    return widget;
  for (auto *child = root.get_first_child(); child;
       child = child->get_next_sibling())
    if (auto *found = find_widget<T>(*child, predicate))
      return found;
  return nullptr;
}

Gtk::Button *button(Gtk::Widget &root, const char *label)
{
  return find_widget<Gtk::Button>(root, [label](const auto &widget) {
    return widget.get_label() == label;
  });
}

bool has_label(Gtk::Widget &root, const char *text)
{
  return find_widget<Gtk::Label>(root, [text](const auto &label) {
           return label.get_text() == text && label.get_visible();
         }) != nullptr;
}

template <typename Predicate>
bool spin_until(Predicate predicate, std::chrono::milliseconds timeout)
{
  auto end = std::chrono::steady_clock::now() + timeout;
  auto context = Glib::MainContext::get_default();
  do {
    while (context->pending())
      context->iteration(false);
    if (predicate())
      return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  } while (std::chrono::steady_clock::now() < end);
  return predicate();
}

void spin_for(std::chrono::milliseconds duration)
{
  spin_until([] { return false; }, duration);
}

class NullAudio final : public generator::IAudioBackend {
public:
  void output_samples(std::span<const uint16_t>,
                      std::span<const uint16_t>) override
  {
  }
};

class NullVideo final : public generator::IVideoBackend {
public:
  void render_line(int, std::span<const uint8_t>) override
  {
  }
  void present_field() override
  {
  }
};

class NullLogger final : public generator::ILogger {
public:
  void log(generator::LogLevel, std::string_view) override
  {
  }
};

void load_demo_rom()
{
  g_emulator_core = std::make_unique<generator::EmulatorCore>(
      std::make_unique<NullAudio>(), std::make_unique<NullVideo>(),
      std::make_shared<NullLogger>());
  std::vector<uint8_t> rom(0x4000, 0);
  rom[1] = 0xff;  // Stack at $ff0000; PC at $200, BRA.S -2.
  rom[6] = 2;
  std::copy_n("SEGA", 4, rom.begin() + 0x100);
  std::fill_n(rom.begin() + 0x120, 96, ' ');
  std::copy_n("Demo", 4, rom.begin() + 0x120);
  std::copy_n("Demo", 4, rom.begin() + 0x150);
  rom[0x200] = 0x60;
  rom[0x201] = 0xfe;
  REQUIRE(g_emulator_core->load_rom_mem(rom));
}

class DialogFixture {
public:
  DialogFixture()
  {
    worker.start();
    dialog = std::make_unique<NetplayDialog>(parent, worker,
                                             [this] { chose_rom = true; });
  }
  ~DialogFixture()
  {
    dialog.reset();
    worker.stop();
    netplay_shutdown();
    g_emulator_core.reset();
  }

  void connect_to_silent_peer()
  {
    auto *port = find_widget<Gtk::SpinButton>(
        *dialog, [](const auto &) { return true; });
    REQUIRE(port != nullptr);
    REQUIRE(silent.sock != nullptr);
    REQUIRE(socket_bind(silent, 0) == 0);
    port->set_value(socket_get_local_port(silent));
    g_signal_emit_by_name(button(*dialog, "Connect")->gobj(), "clicked");
    REQUIRE(dialog->busy());
  }

  void connect(uint16_t server_port)
  {
    auto *port = find_widget<Gtk::SpinButton>(
        *dialog, [](const auto &) { return true; });
    REQUIRE(port != nullptr);
    port->set_value(server_port);
    g_signal_emit_by_name(button(*dialog, "Connect")->gobj(), "clicked");
    REQUIRE(
        spin_until([&] { return !dialog->busy(); }, std::chrono::seconds(2)));
  }

  Gtk::Window parent;
  EmulatorThread worker;
  std::unique_ptr<NetplayDialog> dialog;
  bool chose_rom = false;
  // Use the socket API's RAII below rather than a live Internet endpoint.
  struct SilentPeer {
    netplay_socket_t *sock = socket_create();
    ~SilentPeer()
    {
      socket_destroy(sock);
    }
    operator netplay_socket_t *()
    {
      return sock;
    }
  } silent;
};

}  // namespace

TEST_CASE("netplay starts with a labelled form and a ROM action",
          "[netplay][gtk]")
{
  if (!init_gtk())
    SKIP("GTK display unavailable");
  DialogFixture ui;
  CHECK_FALSE(ui.dialog->get_modal());
  CHECK(has_label(*ui.dialog, "Server"));
  CHECK(has_label(*ui.dialog, "Port"));
  CHECK(has_label(*ui.dialog, "Nickname"));
  CHECK(has_label(*ui.dialog, "Connect to a server to browse rooms."));
  CHECK_FALSE(button(*ui.dialog, "Create room")->get_sensitive());
  CHECK_FALSE(button(*ui.dialog, "Join room")->get_sensitive());
  CHECK_FALSE(button(*ui.dialog, "Disconnect")->get_visible());
  auto *port = find_widget<Gtk::SpinButton>(*ui.dialog,
                                            [](const auto &) { return true; });
  REQUIRE(port != nullptr);
  CHECK(port->get_width_chars() >= 5);
  CHECK(port->get_value_as_int() == 27888);
  g_signal_emit_by_name(button(*ui.dialog, "Choose ROM…")->gobj(), "clicked");
  CHECK(ui.chose_rom);
}

TEST_CASE("netplay connection keeps GTK responsive and can be cancelled",
          "[netplay][gtk]")
{
  using namespace std::chrono_literals;
  if (!init_gtk())
    SKIP("GTK display unavailable");
  DialogFixture ui;
  ui.connect_to_silent_peer();
  bool ticked = false;
  auto tick = Glib::signal_timeout().connect(
      [&] {
        ticked = true;
        return false;
      },
      10);
  CHECK(spin_until([&] { return ticked; }, 500ms));
  CHECK(ui.dialog->busy());
  CHECK(button(*ui.dialog, "Cancel")->get_visible());
  CHECK_FALSE(button(*ui.dialog, "Connect")->get_sensitive());
  g_signal_emit_by_name(button(*ui.dialog, "Cancel")->gobj(), "clicked");
  REQUIRE(spin_until([&] { return !ui.dialog->busy(); }, 1500ms));
  CHECK(has_label(*ui.dialog, "Connection cancelled."));
  spin_for(400ms);
  CHECK(has_label(*ui.dialog, "Connection cancelled."));
  CHECK(button(*ui.dialog, "Connect")->get_sensitive());
  tick.disconnect();
}

TEST_CASE("netplay failure feedback survives status refreshes",
          "[netplay][gtk]")
{
  using namespace std::chrono_literals;
  if (!init_gtk())
    SKIP("GTK display unavailable");
  DialogFixture ui;
  ui.connect_to_silent_peer();
  REQUIRE(spin_until([&] { return !ui.dialog->busy(); }, 7s));
  CHECK(has_label(*ui.dialog, "Connection timeout - no response from server"));
  spin_for(400ms);
  CHECK(has_label(*ui.dialog, "Connection timeout - no response from server"));
  CHECK(button(*ui.dialog, "Connect")->get_sensitive());
}

TEST_CASE("closing netplay joins a pending connection safely", "[netplay][gtk]")
{
  using namespace std::chrono_literals;
  if (!init_gtk())
    SKIP("GTK display unavailable");
  DialogFixture ui;
  ui.connect_to_silent_peer();
  auto start = std::chrono::steady_clock::now();
  ui.dialog.reset();
  CHECK(std::chrono::steady_clock::now() - start < 1s);
  ui.worker.run_exclusive([] {});
}

TEST_CASE("room updates preserve selection and gate joining", "[netplay][gtk]")
{
  using namespace std::chrono_literals;
  if (!init_gtk())
    SKIP("GTK display unavailable");
  generator::test::LoopbackServer server;
  REQUIRE(server.start(1, 2, 0, "ROM"));
  DialogFixture ui;
  load_demo_rom();
  REQUIRE(g_emulator_core->rom_loaded());
  ui.connect(server.port());
  auto *list =
      find_widget<Gtk::ListBox>(*ui.dialog, [](const auto &) { return true; });
  REQUIRE(list != nullptr);
  REQUIRE(spin_until([&] { return list->get_row_at_index(0) != nullptr; }, 1s));
  list->select_row(*list->get_row_at_index(0));
  CHECK(button(*ui.dialog, "Join room")->get_sensitive());
  server.update_room(2, 2, 0);
  REQUIRE(spin_until(
      [&] { return has_label(*ui.dialog, "other · 2/2 players · Waiting"); },
      1s));
  CHECK(list->get_selected_row() != nullptr);
  CHECK_FALSE(button(*ui.dialog, "Join room")->get_sensitive());
  server.update_room(1, 2, 1);
  REQUIRE(spin_until(
      [&] { return has_label(*ui.dialog, "other · 1/2 players · Playing"); },
      1s));
  CHECK(list->get_selected_row() != nullptr);
  CHECK_FALSE(button(*ui.dialog, "Join room")->get_sensitive());
  server.update_room(1, 2, 0);
  REQUIRE(spin_until(
      [&] { return button(*ui.dialog, "Join room")->get_sensitive(); }, 1s));
  g_signal_emit_by_name(button(*ui.dialog, "Join room")->gobj(), "clicked");
  REQUIRE(spin_until([&] { return !ui.dialog->busy() && ui.dialog->in_room(); },
                     1s));
  CHECK_FALSE(button(*ui.dialog, "Start game")->get_visible());
  CHECK(has_label(*ui.dialog, "Waiting for the host to start the game."));
  g_signal_emit_by_name(button(*ui.dialog, "Leave room")->gobj(), "clicked");
  REQUIRE(spin_until(
      [&] { return !ui.dialog->busy() && !ui.dialog->in_room(); }, 1s));
}

TEST_CASE("starting a match returns to gameplay and keeps session controls",
          "[netplay][gtk]")
{
  using namespace std::chrono_literals;
  if (!init_gtk())
    SKIP("GTK display unavailable");
  generator::test::LoopbackServer server;
  REQUIRE(server.start());
  DialogFixture ui;
  load_demo_rom();
  ui.connect(server.port());
  ui.dialog->present();
  g_signal_emit_by_name(button(*ui.dialog, "Create room")->gobj(), "clicked");
  REQUIRE(spin_until([&] { return !ui.dialog->busy() && ui.dialog->in_room(); },
                     1s));
  CHECK(button(*ui.dialog, "Start game")->get_visible());
  REQUIRE(spin_until(
      [&] { return button(*ui.dialog, "Start game")->get_sensitive(); }, 1s));
  g_signal_emit_by_name(button(*ui.dialog, "Start game")->gobj(), "clicked");
  REQUIRE(spin_until(
      [&] { return !ui.dialog->busy() && !ui.dialog->get_visible(); }, 1s));
  CHECK(ui.dialog->in_room());
  ui.dialog->present();
  spin_for(400ms);
  CHECK(ui.dialog->get_visible());
  CHECK(button(*ui.dialog, "Return to game")->get_visible());
  CHECK(button(*ui.dialog, "Leave room")->get_sensitive());
  g_signal_emit_by_name(button(*ui.dialog, "Return to game")->gobj(),
                        "clicked");
  CHECK_FALSE(ui.dialog->get_visible());
  CHECK(ui.dialog->in_room());
}
