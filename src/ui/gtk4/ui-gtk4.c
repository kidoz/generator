/* Generator is (c) James Ponder, 1997-2001 http://www.squish.net/generator/ */
/* GTK4/libadwaita user interface */

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>
#include <pwd.h>

#include <gtk/gtk.h>
#include <adwaita.h>
#include <SDL3/SDL.h>

#include "generator.h"
#include "snprintf.h"
#include "ui.h"
#include "ui-gtk4.h"
#include "gen_core.h"
#include "gen_ui_callbacks.h"
#include "ui-console.h"
#include "uiplot.h"
#include "gtkopts.h"
#include "vdp.h"
#include "gensound.h"
#include "gensoundp.h"
#include "cpu68k.h"
#include "mem68k.h"
#include "cpuz80.h"
#include <gdk/gdkkeysyms.h>
#include "event.h"
#include "state.h"
#include "initcart.h"
#include "patch.h"
#include "dib.h"
#include "avi.h"

/* Global UI instance */
GenUI *gen_ui = nullptr;

/* Interlace mode */
t_interlace ui_interlace = DEINTERLACE_WEAVEFILTER;

/* Key names for configuration */
const char *ui_gtk4_keys[] = {"a",    "b",     "c",  "start",
                              "left", "right", "up", "down"};

/*** Forward declarations ***/
static void ui_usage(void);
static void ui_activate(GtkApplication *app, gpointer user_data);
static void ui_startup(GtkApplication *app, gpointer user_data);
static void ui_shutdown(GtkApplication *app, gpointer user_data);
static void ui_create_main_window(GtkApplication *app);
static void ui_setup_actions(GtkApplication *app);
static void ui_draw_callback(GtkDrawingArea *area, cairo_t *cr, int width,
                             int height, gpointer user_data);
static gboolean ui_key_pressed(GtkEventControllerKey *controller, guint keyval,
                               guint keycode, GdkModifierType state,
                               gpointer user_data);
static gboolean ui_key_released(GtkEventControllerKey *controller, guint keyval,
                                guint keycode, GdkModifierType state,
                                gpointer user_data);
static gboolean ui_window_close_request(GtkWindow *window, gpointer user_data);
static gboolean ui_tick_callback(GtkWidget *widget, GdkFrameClock *frame_clock,
                                 gpointer user_data);
static void ui_newframe(int pending_samples);
static void ui_simpleplot(void);
static void ui_sdl_events(void);
static void ui_rendertoscreen(void);
static gboolean ui_gtk4_apply_audio_driver(const char *requested,
                                           gboolean restart_audio,
                                           gboolean persist_config);
static void ui_gtk4_update_audio_backend_subtitle(void);
static void ui_gtk4_ensure_preferences_window(void);
static void ui_gtk4_on_audio_driver_changed(GObject *row, GParamSpec *pspec,
                                            gpointer user_data);
static void ui_gtk4_rebuild_audio_driver_model(void);
static guint ui_gtk4_find_driver_index(const char *driver_id);
static gboolean ui_gtk4_driver_is_available(const char *driver_id);
static char *ui_gtk4_normalize_audio_driver(const char *driver);
static char *ui_gtk4_format_driver_label(const char *driver_id);
static void ui_gtk4_apply_scaler(const char *scaler_name);
static void ui_gtk4_on_scaler_changed(GObject *row, GParamSpec *pspec,
                                      gpointer user_data);

/*** New Architecture: gen_context callbacks ***/
static void gtk4_cb_line(gen_context_t *ctx, int line);
static void gtk4_cb_end_field(gen_context_t *ctx);
static void gtk4_cb_audio_output(gen_context_t *ctx, const uint16 *left,
                                  const uint16 *right, unsigned int samples);
static void gtk4_cb_log_debug(gen_context_t *ctx, const char *msg);
static void gtk4_cb_log_user(gen_context_t *ctx, const char *msg);
static void gtk4_cb_log_verbose(gen_context_t *ctx, const char *msg);
static void gtk4_cb_log_normal(gen_context_t *ctx, const char *msg);
static void gtk4_cb_log_critical(gen_context_t *ctx, const char *msg);
static void gtk4_cb_musiclog(gen_context_t *ctx, const uint8 *data,
                              unsigned int length);
[[noreturn]] static void gtk4_cb_fatal_error(gen_context_t *ctx, const char *msg);

/*** Emulation thread functions ***/
static gpointer ui_emu_thread_func(gpointer data);
static void ui_start_emu_thread(void);
static void ui_stop_emu_thread(void);

/*** Gamepad functions ***/
static void ui_open_gamepad(SDL_JoystickID id);
static void ui_close_gamepad(SDL_JoystickID id);
static int ui_gamepad_id_to_player(SDL_JoystickID id);
static void ui_handle_gamepad_button(SDL_GamepadButtonEvent *event);
static void ui_handle_gamepad_axis(SDL_GamepadAxisEvent *event);

/*** GTK4 Callback Structure for gen_context ***/
static const gen_ui_callbacks_t gtk4_callbacks = {
    .line = gtk4_cb_line,
    .end_field = gtk4_cb_end_field,
    .audio_output = gtk4_cb_audio_output,
    .log_debug3 = gtk4_cb_log_debug,
    .log_debug2 = gtk4_cb_log_debug,
    .log_debug1 = gtk4_cb_log_debug,
    .log_user = gtk4_cb_log_user,
    .log_verbose = gtk4_cb_log_verbose,
    .log_normal = gtk4_cb_log_normal,
    .log_critical = gtk4_cb_log_critical,
    .log_request = gtk4_cb_log_normal,
    .musiclog = gtk4_cb_musiclog,
    .fatal_error = gtk4_cb_fatal_error};

/*** Program entry point ***/

int ui_init(int argc, char *argv[])
{
  int ch;
  struct passwd *passwd;
  struct stat statbuf;
  int i;

  fprintf(stderr, "Generator is (c) James Ponder 1997-2003, all rights "
                  "reserved. v" VERSION "\n\n");
  fprintf(stderr, "GTK4/libadwaita UI version\n\n");

  /* Allocate UI structure */
  gen_ui = g_new0(GenUI, 1);
  gen_ui->hborder = HBORDER_DEFAULT;
  gen_ui->vborder = VBORDER_DEFAULT;
  gen_ui->statusbar_enabled = TRUE;
  gen_ui->frameskip = 0;
  gen_ui->filter_type = FILTER_SCALE2X; /* Default to Scale2x (fast, good
                                           quality, <1ms overhead) */
  gen_ui->scale_factor = 2;             /* Default to 2x scale */

  /* Initialize controller mappings to zero (will use defaults in
   * ui_update_controller_from_keys) */
  memset(&gen_ui->controllers, 0, sizeof(gen_ui->controllers));

  /* Initialize Genesis controller state to all buttons released */
  memset(mem68k_cont, 0, sizeof(mem68k_cont));

  /* Initialize gamepad slots */
  memset(gen_ui->gamepads, 0, sizeof(gen_ui->gamepads));
  gen_ui->num_gamepads = 0;

  /* Initialize new architecture support (gen_context) */
  gen_ui->ctx = gen_context_create();
  if (gen_ui->ctx == nullptr) {
    fprintf(stderr, "Failed to create emulator context\n");
    return -1;
  }
  if (gen_context_init(gen_ui->ctx) != 0) {
    fprintf(stderr, "Failed to initialize emulator context\n");
    gen_context_destroy(gen_ui->ctx);
    return -1;
  }
  gen_ui_set_callbacks(gen_ui->ctx, &gtk4_callbacks, gen_ui);

  /* Initialize emulation thread state (thread started after GTK activation) */
  gen_ui->emu_thread = nullptr;
  gen_ui->emu_thread_running = FALSE;
  gen_ui->frame_requested = FALSE;
  atomic_store(&gen_ui->render_complete, 0);

  /* Initialize debug output (set GENERATOR_DEBUG=1 to enable) */
  gen_ui->debug_telemetry = (g_getenv("GENERATOR_DEBUG") != nullptr);

  /* Initialize dynamic rate control */
  gen_ui->dynamic_rate_control = TRUE; /* Enable by default */
  gen_ui->rate_adjust = 1.0;           /* Normal speed */
  gen_ui->rate_delta = 0.03;           /* ±3% default adjustment window */
  gen_ui->fps_index = 0;
  gen_ui->measured_fps = 0.0;
  gen_ui->frames_recorded = 0;
  gen_ui->debug_counter = 0;
  gen_ui->last_frame_time = 0;
  /* fps_times array is zero-initialized by g_new0 */

  /* Pre-allocate upscaling buffers to avoid malloc/free per frame
     Maximum size needed: 320x240 @ 4x scale = 1280x960 pixels */
  gen_ui->upscale_buffer_size = 1280 * 960;
  gen_ui->upscale_src_buffer =
      g_malloc(gen_ui->upscale_buffer_size * sizeof(guint32));
  gen_ui->upscale_dst_buffer =
      g_malloc(gen_ui->upscale_buffer_size * sizeof(guint32));
  /* Scale4x needs intermediate 2x buffer: 640x480 pixels max */
  gen_ui->scale4x_temp_buffer = g_malloc(640 * 480 * sizeof(guint32));

  /* Allocate screen buffers */
  gen_ui->screen_buffers[0] = g_malloc0(4 * HMAXSIZE * VMAXSIZE);
  gen_ui->screen_buffers[1] = g_malloc0(4 * HMAXSIZE * VMAXSIZE);
  gen_ui->screen_buffers[2] = g_malloc0(4 * HMAXSIZE * VMAXSIZE);

  /* Parse command line */
  while ((ch = getopt(argc, argv, "?dc:w:")) != -1) {
    switch (ch) {
    case 'd':
      gen_debugmode = 1;
      break;
    case 'c':
      gen_ui->configfile = g_strdup(optarg);
      break;
    case 'w':
      if (chdir(optarg) != 0) {
        fprintf(stderr, "Failed to change directory to %s\n", optarg);
      }
      break;
    case '?':
    default:
      ui_usage();
    }
  }
  argc -= optind;
  argv += optind;

  if (argc > 0) {
    gen_ui->initload = g_strdup(argv[0]);
    argc--;
    argv++;
  }

  if (argc != 0)
    ui_usage();

  /* Determine config file location */
  if (gen_ui->configfile == nullptr) {
    passwd = getpwuid(getuid());
    if (passwd == nullptr) {
      fprintf(stderr, "Who are you? (getpwent failed)\n");
      exit(1);
    }
    gen_ui->configfile = g_strdup_printf("%s/.genrc", passwd->pw_dir);
  }

  /* Load configuration */
  if (stat(gen_ui->configfile, &statbuf) != 0) {
    fprintf(stderr, "No configuration file found, using defaults.\n");
  } else {
    if (gtkopts_load(gen_ui->configfile) != 0) {
      fprintf(stderr, "Error loading configuration file, using defaults.\n");
    }
  }

  /* Apply preferred audio backend before SDL initialises audio */
  ui_gtk4_apply_audio_driver(gtkopts_getvalue("audio_driver"), FALSE, TRUE);

  /* Apply scaler setting from config */
  ui_gtk4_apply_scaler(gtkopts_getvalue("scaler"));

  /* Initialize SDL for gamepad support only.
   * NOTE: We do NOT initialize SDL_INIT_VIDEO because GTK4 handles all rendering.
   * Initializing SDL video can conflict with GTK4's Wayland/X11 display handling. */
  if (!SDL_Init(SDL_INIT_GAMEPAD)) {
    fprintf(stderr, "Couldn't initialise SDL: %s\n", SDL_GetError());
    return -1;
  }

  /* Setup gamepads (with hot-plug support via events) */
  int gamepad_count = 0;
  SDL_JoystickID *gamepad_ids = SDL_GetGamepads(&gamepad_count);
  fprintf(stderr, "%d gamepads detected\n", gamepad_count);
  if (gamepad_ids) {
    for (i = 0; i < gamepad_count; i++) {
      ui_open_gamepad(gamepad_ids[i]);
    }
    SDL_free(gamepad_ids);
  }
  SDL_SetGamepadEventsEnabled(true);

  /* Initialize screen buffers */
  memset(gen_ui->screen_buffers[0], 0, 4 * HMAXSIZE * VMAXSIZE);
  memset(gen_ui->screen_buffers[1], 0, 4 * HMAXSIZE * VMAXSIZE);
  gen_ui->screen0 = gen_ui->screen_buffers[0];
  gen_ui->screen1 = gen_ui->screen_buffers[1];
  gen_ui->newscreen = gen_ui->screen_buffers[2];
  atomic_store(&gen_ui->whichbank, 0);
  gen_ui->musicfile_fd = -1;

  /* Set up color conversion for Cairo CAIRO_FORMAT_RGB24
     Format: 32-bit value 0xXXRRGGBB (XX=unused, RR=red, GG=green, BB=blue)
     Red in bits 16-23, Green in bits 8-15, Blue in bits 0-7 */
  uiplot_setshifts(16, 8, 0); /* red=16, green=8, blue=0 */
  uiplot_setmasks(0x00FF0000, 0x0000FF00, 0x000000FF); /* 8-bit per channel */

  /* Create GTK application with NON_UNIQUE flag to allow multiple instances */
  gen_ui->app =
      adw_application_new("net.squish.generator", G_APPLICATION_NON_UNIQUE);
  g_signal_connect(gen_ui->app, "activate", G_CALLBACK(ui_activate), nullptr);
  g_signal_connect(gen_ui->app, "startup", G_CALLBACK(ui_startup), nullptr);
  g_signal_connect(gen_ui->app, "shutdown", G_CALLBACK(ui_shutdown), nullptr);

  fprintf(stderr,
          "GTK4/libadwaita UI initialized. Use the menu to quit properly.\n");
  return 0;
}

static void ui_usage(void)
{
  fprintf(stderr, "generator [options] [<rom>]\n\n");
  fprintf(stderr, "  -d                     debug mode\n");
  fprintf(stderr, "  -w <work dir>          set work directory\n");
  fprintf(stderr, "  -c <config file>       use alternative config file\n\n");
  fprintf(stderr,
          "  ROM types supported: .rom or .smd interleaved (autodetected)\n");
  exit(1);
}

void ui_final(void)
{
  if (gen_ui) {
    if (gen_ui->prefs_window) {
      gtk_window_destroy(GTK_WINDOW(gen_ui->prefs_window));
      gen_ui->prefs_window = nullptr;
      gen_ui->audio_driver_row = nullptr;
    }
    g_clear_object(&gen_ui->audio_driver_model);
    if (gen_ui->audio_driver_ids) {
      g_ptr_array_free(gen_ui->audio_driver_ids, TRUE);
      gen_ui->audio_driver_ids = nullptr;
    }
    g_clear_pointer(&gen_ui->audio_driver_selection, g_free);
    SDL_Quit();
    g_free(gen_ui->screen_buffers[0]);
    g_free(gen_ui->screen_buffers[1]);
    g_free(gen_ui->screen_buffers[2]);
    g_free(gen_ui->upscale_src_buffer);
    g_free(gen_ui->upscale_dst_buffer);
    g_free(gen_ui->configfile);
    g_free(gen_ui->initload);
    g_free(gen_ui);
    gen_ui = nullptr;
  }
}

int ui_loop(void)
{
  char *p;

  /* Load initial ROM if specified */
  if (gen_ui->initload) {
    p = gen_loadimage(gen_ui->initload);
    if (p) {
      ui_gtk4_messageerror(p);
    } else {
      /* ROM loaded successfully, start emulation */
      gen_ui->running = TRUE;
    }
  } else {
    gen_loadmemrom((const char *)initcart, initcart_len);
    /* Default ROM loaded, start emulation */
    gen_ui->running = TRUE;
  }

  /* Run GTK application */
  int status = g_application_run(G_APPLICATION(gen_ui->app), 0, nullptr);
  g_object_unref(gen_ui->app);
  return status;
}

/*** GTK Application callbacks ***/

static void ui_startup(GtkApplication *app, gpointer user_data)
{
  (void)user_data;

  /* Setup application-level actions and menus */
  ui_setup_actions(app);

  /* Attach context to already-initialized subsystems
   * Note: main() in generator.c already calls mem68k_init(), vdp_init(), etc.
   * We just need to sync the context with the global state. */
  if (gen_ui->ctx != nullptr) {
    if (gen_core_attach(gen_ui->ctx) != 0) {
      fprintf(stderr, "Failed to attach emulator context\n");
    } else {
      fprintf(stderr, "Emulator context attached to core\n");
    }
  }
}

static void ui_activate(GtkApplication *app, gpointer user_data)
{
  (void)user_data;

  /* Create and show main window */
  if (gen_ui->window == nullptr) {
    ui_create_main_window(app);
  }
  gtk_window_present(GTK_WINDOW(gen_ui->window));

  /* Start the emulation thread */
  if (gen_ui->emu_thread == nullptr) {
    ui_start_emu_thread();
  }
}

static void ui_shutdown(GtkApplication *app, gpointer user_data)
{
  (void)app;
  (void)user_data;

  /* Stop emulation and cleanup */
  gen_ui->running = FALSE;
  gen_quit = 1;

  /* Stop the emulation thread */
  ui_stop_emu_thread();

  /* Close all gamepads */
  for (int i = 0; i < MAX_GAMEPADS; i++) {
    if (gen_ui->gamepads[i].gamepad != nullptr) {
      SDL_CloseGamepad(gen_ui->gamepads[i].gamepad);
      gen_ui->gamepads[i].gamepad = nullptr;
    }
  }
  gen_ui->num_gamepads = 0;

  /* Stop and cleanup sound system - this will close SDL3 audio device
     and stop the audio callback thread, preventing sound from continuing */
  sound_final();

  /* Shutdown core and destroy context */
  if (gen_ui->ctx != nullptr) {
    gen_core_shutdown(gen_ui->ctx);
    gen_context_destroy(gen_ui->ctx);
    gen_ui->ctx = nullptr;
  }

  /* Save configuration */
  if (gtkopts_save(gen_ui->configfile) != 0) {
    fprintf(stderr, "Failed to save configuration\n");
  }
}

/*** Action setup ***/

static void ui_setup_actions(GtkApplication *app)
{
  static const GActionEntry app_entries[] = {
      {"open-rom", ui_action_open_rom, nullptr, nullptr, nullptr},
      {"save-rom", ui_action_save_rom, nullptr, nullptr, nullptr},
      {"load-state", ui_action_load_state, nullptr, nullptr, nullptr},
      {"save-state", ui_action_save_state, nullptr, nullptr, nullptr},
      {"reset", ui_action_reset, nullptr, nullptr, nullptr},
      {"soft-reset", ui_action_soft_reset, nullptr, nullptr, nullptr},
      {"pause", ui_action_pause, nullptr, "false", nullptr},
      {"preferences", ui_action_preferences, nullptr, nullptr, nullptr},
      {"about", ui_action_about, nullptr, nullptr, nullptr},
      {"quit", ui_action_quit, nullptr, nullptr, nullptr}};

  g_action_map_add_action_entries(G_ACTION_MAP(app), app_entries,
                                  G_N_ELEMENTS(app_entries), app);

  /* Set keyboard accelerators */
  const char *open_accels[] = {"<Ctrl>O", nullptr};
  const char *load_state_accels[] = {"<Ctrl>L", nullptr};
  const char *save_state_accels[] = {"<Ctrl>S", nullptr};
  const char *reset_accels[] = {"F5", nullptr};
  const char *pause_accels[] = {"space", nullptr};
  const char *quit_accels[] = {"<Ctrl>Q", nullptr};

  gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.open-rom",
                                        open_accels);
  gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.load-state",
                                        load_state_accels);
  gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.save-state",
                                        save_state_accels);
  gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.reset",
                                        reset_accels);
  gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.pause",
                                        pause_accels);
  gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.quit",
                                        quit_accels);
}

/*** Main window creation ***/

static void ui_create_main_window(GtkApplication *app)
{
  GtkWidget *toolbar_view, *content_box, *menu_button, *open_button,
      *pause_button;
  GMenu *primary_menu;
  GtkEventController *key_controller;

  /* Create main window */
  gen_ui->window =
      ADW_APPLICATION_WINDOW(adw_application_window_new(GTK_APPLICATION(app)));
  gtk_window_set_title(GTK_WINDOW(gen_ui->window), "Generator");
  gtk_window_set_default_size(GTK_WINDOW(gen_ui->window), 640, 480);

  /* Create toolbar view (HIG recommended pattern for header bars) */
  toolbar_view = adw_toolbar_view_new();

  /* Create header bar */
  gen_ui->header_bar = adw_header_bar_new();
  adw_header_bar_set_show_back_button(ADW_HEADER_BAR(gen_ui->header_bar),
                                      FALSE);
  adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar_view),
                               gen_ui->header_bar);

  /* Add primary action button (Open ROM) to header bar start */
  open_button = gtk_button_new_with_label("Open ROM");
  gtk_actionable_set_action_name(GTK_ACTIONABLE(open_button), "app.open-rom");
  gtk_widget_add_css_class(open_button, "suggested-action");
  adw_header_bar_pack_start(ADW_HEADER_BAR(gen_ui->header_bar), open_button);

  /* Add pause/resume toggle button */
  pause_button = gtk_toggle_button_new();
  gtk_button_set_icon_name(GTK_BUTTON(pause_button),
                           "media-playback-pause-symbolic");
  gtk_actionable_set_action_name(GTK_ACTIONABLE(pause_button), "app.pause");
  gtk_widget_set_tooltip_text(pause_button, "Pause (Space)");
  adw_header_bar_pack_start(ADW_HEADER_BAR(gen_ui->header_bar), pause_button);

  /* Create primary menu (HIG pattern: contains app-wide actions) */
  primary_menu = g_menu_new();

  /* File operations section */
  GMenu *file_section = g_menu_new();
  g_menu_append(file_section, "_Save ROM As…", "app.save-rom");
  g_menu_append_section(primary_menu, nullptr, G_MENU_MODEL(file_section));

  /* State management section */
  GMenu *state_section = g_menu_new();
  g_menu_append(state_section, "_Load State…", "app.load-state");
  g_menu_append(state_section, "_Save State…", "app.save-state");
  g_menu_append_section(primary_menu, "Save States",
                        G_MENU_MODEL(state_section));

  /* Emulation control section */
  GMenu *emulation_section = g_menu_new();
  g_menu_append(emulation_section, "_Reset", "app.reset");
  g_menu_append(emulation_section, "_Soft Reset", "app.soft-reset");
  g_menu_append_section(primary_menu, "Emulation",
                        G_MENU_MODEL(emulation_section));

  /* App section (preferences, about, quit) */
  GMenu *app_section = g_menu_new();
  g_menu_append(app_section, "_Preferences", "app.preferences");
  g_menu_append(app_section, "_About Generator", "app.about");
  g_menu_append_section(primary_menu, nullptr, G_MENU_MODEL(app_section));

  /* Add primary menu button to header bar end */
  menu_button = gtk_menu_button_new();
  gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(menu_button),
                                "open-menu-symbolic");
  gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(menu_button),
                                 G_MENU_MODEL(primary_menu));
  gtk_menu_button_set_primary(GTK_MENU_BUTTON(menu_button), TRUE);
  gtk_widget_set_tooltip_text(menu_button, "Main Menu");
  adw_header_bar_pack_end(ADW_HEADER_BAR(gen_ui->header_bar), menu_button);

  /* Create content box for emulation display and status */
  content_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

  /* Create drawing area for emulation display */
  gen_ui->drawing_area = gtk_drawing_area_new();
  gtk_widget_set_hexpand(gen_ui->drawing_area, TRUE);
  gtk_widget_set_vexpand(gen_ui->drawing_area, TRUE);
  gtk_widget_set_size_request(gen_ui->drawing_area, 320, 224);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(gen_ui->drawing_area),
                                 ui_draw_callback, nullptr, nullptr);
  gtk_box_append(GTK_BOX(content_box), gen_ui->drawing_area);

  /* Create status bar using AdwActionRow for better styling */
  GtkWidget *status_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_widget_add_css_class(status_box, "toolbar");
  gtk_widget_set_margin_start(status_box, 6);
  gtk_widget_set_margin_end(status_box, 6);
  gtk_widget_set_margin_top(status_box, 6);
  gtk_widget_set_margin_bottom(status_box, 6);

  gen_ui->status_label = gtk_label_new("Ready");
  gtk_label_set_ellipsize(GTK_LABEL(gen_ui->status_label), PANGO_ELLIPSIZE_END);
  gtk_widget_set_halign(gen_ui->status_label, GTK_ALIGN_START);
  gtk_widget_set_hexpand(gen_ui->status_label, TRUE);
  gtk_box_append(GTK_BOX(status_box), gen_ui->status_label);

  gtk_box_append(GTK_BOX(content_box), status_box);

  /* Set content in toolbar view */
  adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar_view), content_box);

  /* Set window content */
  adw_application_window_set_content(gen_ui->window, toolbar_view);

  /* Setup keyboard input */
  key_controller = gtk_event_controller_key_new();
  g_signal_connect(key_controller, "key-pressed", G_CALLBACK(ui_key_pressed),
                   nullptr);
  g_signal_connect(key_controller, "key-released", G_CALLBACK(ui_key_released),
                   nullptr);
  gtk_widget_add_controller(GTK_WIDGET(gen_ui->window), key_controller);

  /* Connect window close signal to properly cleanup when window is closed */
  g_signal_connect(gen_ui->window, "close-request",
                   G_CALLBACK(ui_window_close_request), nullptr);

  /* Drive the emulation loop from the GTK frame clock so we receive a steady
     pulse that matches the window's refresh rate (typically vblank). */
  gtk_widget_add_tick_callback(gen_ui->drawing_area, ui_tick_callback, nullptr,
                               nullptr);

  /* Show window */
  gtk_window_present(GTK_WINDOW(gen_ui->window));
}

/*** File dialog callbacks ***/

static void on_open_rom_response(GObject *source, GAsyncResult *result,
                                 gpointer data)
{
  GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
  GError *error_obj = nullptr;
  GFile *file = gtk_file_dialog_open_finish(dialog, result, &error_obj);

  if (error_obj) {
    /* User cancelled or error occurred */
    if (error_obj->code != GTK_DIALOG_ERROR_DISMISSED) {
      fprintf(stderr, "File dialog error: %s\n", error_obj->message);
    }
    g_error_free(error_obj);
    return;
  }

  if (file) {
    char *filename = g_file_get_path(file);
    fprintf(stderr, "Opening ROM: %s\n", filename);

    /* Stop currently running game before loading new ROM */
    gen_ui->running = FALSE;
    sound_stop();

    char *error = gen_loadimage(filename);
    if (error) {
      fprintf(stderr, "ROM load error: %s\n", error);
      ui_gtk4_messageerror(error);
      /* ROM load failed - restart previous ROM and audio */
      sound_start();
      gen_ui->running = TRUE;
    } else {
      /* ROM loaded successfully - restart sound and emulation */
      fprintf(stderr, "ROM loaded successfully, resetting...\n");
      sound_start();
      gen_reset();
      gen_ui->running = TRUE;
    }
    g_free(filename);
    g_object_unref(file);
  }
}

static void on_save_rom_response(GObject *source, GAsyncResult *result,
                                 gpointer data)
{
  GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
  GFile *file = gtk_file_dialog_save_finish(dialog, result, nullptr);
  if (file) {
    char *filename = g_file_get_path(file);
    /* TODO: Implement ROM save functionality */
    g_free(filename);
    g_object_unref(file);
  }
}

static void on_load_state_response(GObject *source, GAsyncResult *result,
                                   gpointer data)
{
  GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
  GFile *file = gtk_file_dialog_open_finish(dialog, result, nullptr);
  if (file) {
    char *filename = g_file_get_path(file);
    if (state_loadfile(filename) != 0) {
      ui_gtk4_messageerror("Failed to load state");
    }
    g_free(filename);
    g_object_unref(file);
  }
}

static void on_save_state_response(GObject *source, GAsyncResult *result,
                                   gpointer data)
{
  GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
  GFile *file = gtk_file_dialog_save_finish(dialog, result, nullptr);
  if (file) {
    char *filename = g_file_get_path(file);
    if (state_savefile(filename) != 0) {
      ui_gtk4_messageerror("Failed to save state");
    }
    g_free(filename);
    g_object_unref(file);
  }
}

/*** Action implementations ***/

void ui_action_open_rom(GSimpleAction *action, GVariant *parameter,
                        gpointer user_data)
{
  GtkFileDialog *dialog;

  dialog = gtk_file_dialog_new();
  gtk_file_dialog_set_title(dialog, "Open ROM");

  gtk_file_dialog_open(dialog, GTK_WINDOW(gen_ui->window), nullptr,
                       on_open_rom_response, nullptr);
}

void ui_action_save_rom(GSimpleAction *action, GVariant *parameter,
                        gpointer user_data)
{
  GtkFileDialog *dialog;

  dialog = gtk_file_dialog_new();
  gtk_file_dialog_set_title(dialog, "Save ROM");

  gtk_file_dialog_save(dialog, GTK_WINDOW(gen_ui->window), nullptr,
                       on_save_rom_response, nullptr);
}

void ui_action_load_state(GSimpleAction *action, GVariant *parameter,
                          gpointer user_data)
{
  GtkFileDialog *dialog;

  dialog = gtk_file_dialog_new();
  gtk_file_dialog_set_title(dialog, "Load State");

  gtk_file_dialog_open(dialog, GTK_WINDOW(gen_ui->window), nullptr,
                       on_load_state_response, nullptr);
}

void ui_action_save_state(GSimpleAction *action, GVariant *parameter,
                          gpointer user_data)
{
  GtkFileDialog *dialog;

  dialog = gtk_file_dialog_new();
  gtk_file_dialog_set_title(dialog, "Save State");

  gtk_file_dialog_save(dialog, GTK_WINDOW(gen_ui->window), nullptr,
                       on_save_state_response, nullptr);
}

void ui_action_reset(GSimpleAction *action, GVariant *parameter,
                     gpointer user_data)
{
  gen_reset();
}

void ui_action_soft_reset(GSimpleAction *action, GVariant *parameter,
                          gpointer user_data)
{
  gen_softreset();
}

void ui_action_pause(GSimpleAction *action, GVariant *parameter,
                     gpointer user_data)
{
  GVariant *state = g_action_get_state(G_ACTION(action));
  gboolean paused = g_variant_get_boolean(state);
  g_variant_unref(state);

  paused = !paused;
  gen_ui->running = !paused;

  /* Pause or resume audio playback */
  if (paused) {
    soundp_pause();
  } else {
    soundp_resume();
  }

  g_simple_action_set_state(action, g_variant_new_boolean(paused));
}

void ui_action_preferences(GSimpleAction *action, GVariant *parameter,
                           gpointer user_data)
{
  (void)action;
  (void)parameter;
  (void)user_data;

  if (!gen_ui || !gen_ui->window)
    return;

  if (!gen_ui->prefs_window)
    ui_gtk4_ensure_preferences_window();
  else {
    ui_gtk4_rebuild_audio_driver_model();
    if (gen_ui->audio_driver_row && gen_ui->audio_driver_model) {
      g_signal_handlers_block_by_func(G_OBJECT(gen_ui->audio_driver_row),
                                      ui_gtk4_on_audio_driver_changed, nullptr);
      adw_combo_row_set_model(ADW_COMBO_ROW(gen_ui->audio_driver_row),
                              G_LIST_MODEL(gen_ui->audio_driver_model));
      guint index = ui_gtk4_find_driver_index(
          gen_ui->audio_driver_selection ? gen_ui->audio_driver_selection
                                         : "auto");
      if (index != GTK_INVALID_LIST_POSITION) {
        adw_combo_row_set_selected(ADW_COMBO_ROW(gen_ui->audio_driver_row),
                                   index);
      }
      g_signal_handlers_unblock_by_func(G_OBJECT(gen_ui->audio_driver_row),
                                        ui_gtk4_on_audio_driver_changed,
                                        nullptr);
    }
  }

  if (!gen_ui->prefs_window)
    return;

  ui_gtk4_update_audio_backend_subtitle();
  gtk_window_present(GTK_WINDOW(gen_ui->prefs_window));
}

void ui_action_about(GSimpleAction *action, GVariant *parameter,
                     gpointer user_data)
{
  adw_show_about_dialog((GtkWidget *)gen_ui->window, "application-name",
                        "Generator", "application-icon", "applications-games",
                        "version", VERSION, "developer-name", "James Ponder",
                        "website", "http://www.squish.net/generator/",
                        "copyright", "© 1997-2003 James Ponder", "license-type",
                        GTK_LICENSE_GPL_2_0, "comments",
                        "Sega Genesis / Mega Drive Emulator", nullptr);
}

void ui_action_quit(GSimpleAction *action, GVariant *parameter,
                    gpointer user_data)
{
  gen_quit = 1;
  g_application_quit(G_APPLICATION(gen_ui->app));
}

/*** Drawing and rendering ***/

static void ui_draw_callback(GtkDrawingArea *area, cairo_t *cr, int width,
                             int height, gpointer user_data)
{
  cairo_surface_t *surface;
  uint8 *screen_data;
  unsigned int base_width = (gen_ctx_vdp_reg()[12] & 1) ? 320 : 256;
  unsigned int base_height = gen_ctx_vdp_vislines();
  unsigned int xoffset = (gen_ctx_vdp_reg()[12] & 1) ? 0 : 32;
  unsigned int yoffset = (gen_ctx_vdp_reg()[1] & (1 << 3)) ? 0 : 8;

  /* Apply scaling factor for upscaled rendering */
  int scale = (gen_ui->filter_type != FILTER_NONE) ? gen_ui->scale_factor : 1;
  unsigned int display_width = base_width * scale;
  unsigned int display_height = base_height * scale;
  unsigned int scaled_xoffset = xoffset * scale;
  unsigned int scaled_yoffset = yoffset * scale;

  if (!gen_ui || !gen_ui->screen0 || !gen_ui->screen1)
    return;

  /* Double buffering: display the buffer indicated by whichbank
   * whichbank is updated atomically by ui_rendertoscreen() after each frame
   * completes, so we're always reading from a stable, complete frame. */
  int current_bank = atomic_load(&gen_ui->whichbank);
  screen_data = (current_bank == 0) ? gen_ui->screen0 : gen_ui->screen1;

  /* Create Cairo image surface from emulator buffer
     Format: CAIRO_FORMAT_RGB24 which is 32-bit with unused alpha */
  surface = cairo_image_surface_create_for_data(
      screen_data + (scaled_yoffset * HMAXSIZE + scaled_xoffset) *
                        4, /* offset into buffer */
      CAIRO_FORMAT_RGB24, display_width, display_height,
      HMAXSIZE * 4 /* stride: full buffer width including borders */
  );

  /* Scale to fit drawing area while maintaining aspect ratio */
  cairo_scale(cr, (double)width / display_width,
              (double)height / display_height);

  /* Draw the emulator screen */
  cairo_set_source_surface(cr, surface, 0, 0);

  /* Use bilinear filtering for smooth scaling to window size
     The upscaling filter (Scale2x/3x/4x) has already been applied to the
     buffer, so Cairo only needs to scale from the upscaled buffer to the window
     size */
  cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
  cairo_paint(cr);

  cairo_surface_destroy(surface);
}

/*** Window close handling ***/

static gboolean ui_window_close_request(GtkWindow *window, gpointer user_data)
{
  /* Stop emulation */
  gen_ui->running = FALSE;
  gen_quit = 1;

  /* Quit the GTK application - this will trigger ui_shutdown */
  g_application_quit(G_APPLICATION(gen_ui->app));

  /* Return FALSE to allow the window to close */
  return FALSE;
}

/*** Input handling ***/

/* Default keyboard mappings for two players (6-button mode)
   Player 1: Arrow keys + Z/X/C/Enter + A/S/D/Tab (X/Y/Z/Mode)
   Player 2: WASD + J/K/L/Space + U/I/O/RShift (X/Y/Z/Mode) */
static const struct {
  guint up, down, left, right, a, b, c, start;
  guint x, y, z, mode;  /* 6-button extensions */
} default_keys[2] = {{GDK_KEY_Up, GDK_KEY_Down, GDK_KEY_Left, GDK_KEY_Right,
                      GDK_KEY_z, GDK_KEY_x, GDK_KEY_c, GDK_KEY_Return,
                      GDK_KEY_a, GDK_KEY_s, GDK_KEY_d, GDK_KEY_Tab},
                     {GDK_KEY_w, GDK_KEY_s, GDK_KEY_a, GDK_KEY_d, GDK_KEY_j,
                      GDK_KEY_k, GDK_KEY_l, GDK_KEY_space,
                      GDK_KEY_u, GDK_KEY_i, GDK_KEY_o, GDK_KEY_Shift_R}};

static void ui_update_controller_from_keys(int player, guint keyval,
                                           gboolean pressed)
{
  if (player < 0 || player > 1)
    return;

  /* Get configured keys or use defaults */
  t_gtk4keys *keys = &gen_ui->controllers[player];

  /* Check which button this key corresponds to and update mem68k_cont */
  /* Standard 3-button controller */
  if (keyval == keys->up || keyval == default_keys[player].up) {
    mem68k_cont[player].up = pressed ? 1 : 0;
  } else if (keyval == keys->down || keyval == default_keys[player].down) {
    mem68k_cont[player].down = pressed ? 1 : 0;
  } else if (keyval == keys->left || keyval == default_keys[player].left) {
    mem68k_cont[player].left = pressed ? 1 : 0;
  } else if (keyval == keys->right || keyval == default_keys[player].right) {
    mem68k_cont[player].right = pressed ? 1 : 0;
  } else if (keyval == keys->a || keyval == default_keys[player].a) {
    mem68k_cont[player].a = pressed ? 1 : 0;
  } else if (keyval == keys->b || keyval == default_keys[player].b) {
    mem68k_cont[player].b = pressed ? 1 : 0;
  } else if (keyval == keys->c || keyval == default_keys[player].c) {
    mem68k_cont[player].c = pressed ? 1 : 0;
  } else if (keyval == keys->start || keyval == default_keys[player].start) {
    mem68k_cont[player].start = pressed ? 1 : 0;
  }
  /* 6-button controller extensions */
  else if (keyval == keys->x || keyval == default_keys[player].x) {
    mem68k_cont[player].x = pressed ? 1 : 0;
  } else if (keyval == keys->y || keyval == default_keys[player].y) {
    mem68k_cont[player].y = pressed ? 1 : 0;
  } else if (keyval == keys->z || keyval == default_keys[player].z) {
    mem68k_cont[player].z = pressed ? 1 : 0;
  } else if (keyval == keys->mode || keyval == default_keys[player].mode) {
    mem68k_cont[player].mode = pressed ? 1 : 0;
  }
}

static gboolean ui_key_pressed(GtkEventControllerKey *controller, guint keyval,
                               guint keycode, GdkModifierType state,
                               gpointer user_data)
{
  /* Update controller state for both players */
  ui_update_controller_from_keys(0, keyval, TRUE);
  ui_update_controller_from_keys(1, keyval, TRUE);

  /* Don't consume the event - allow GTK to handle it for menu accelerators,
   * etc. */
  return FALSE;
}

static gboolean ui_key_released(GtkEventControllerKey *controller, guint keyval,
                                guint keycode, GdkModifierType state,
                                gpointer user_data)
{
  /* Update controller state for both players */
  ui_update_controller_from_keys(0, keyval, FALSE);
  ui_update_controller_from_keys(1, keyval, FALSE);

  return FALSE;
}

/*** Emulation loop ***/

static gboolean ui_tick_callback(GtkWidget *widget, GdkFrameClock *frame_clock,
                                 gpointer user_data)
{
  gint64 current_time;
  gint64 elapsed_us;
  gint64 frame_duration_us;
  int pending_samples;
  static int min_pending = INT_MAX;
  static int frames_tracked = 0;

  (void)widget;
  (void)user_data;

  if (frame_clock) {
    current_time = gdk_frame_clock_get_frame_time(frame_clock);
  } else {
    current_time = g_get_monotonic_time();
  }

  if (gen_quit)
    return G_SOURCE_REMOVE;

  if (!gen_ui->running) {
    /* Keep timestamps in sync so we don't try to "catch up" when resuming. */
    gen_ui->last_frame_time = 0;
    gen_ui->frames_recorded = 0;
    gen_ui->fps_index = 0;
    return G_SOURCE_CONTINUE;
  }

  /* Process SDL events with non-blocking SDL_PollEvent (called in
     ui_sdl_events) This is the recommended pattern for GTK+SDL3 integration to
     avoid blocking the GTK event loop, which would starve the SDL3 audio thread
   */
  ui_sdl_events();

  /* Get current time in microseconds for frame timing */
  if (gen_ui->last_frame_time == 0) {
    gen_ui->last_frame_time = current_time;
  }

  /* Calculate frame duration based on Genesis timing
     NTSC: 60Hz = 16666.67 microseconds per frame
     PAL:  50Hz = 20000 microseconds per frame
     Apply dynamic rate adjustment if enabled */
  if (gen_ui->dynamic_rate_control) {
    /* Apply rate adjustment: higher rate_adjust means slower emulation (longer
     * frame time) */
    frame_duration_us = (gen_ctx_vdp_pal() ? 20000 : 16667) * gen_ui->rate_adjust;
  } else {
    frame_duration_us = gen_ctx_vdp_pal() ? 20000 : 16667;
  }

  elapsed_us = current_time - gen_ui->last_frame_time;

  /* Check sound buffer status to prevent overflow
     This provides backpressure to maintain audio/video sync */
  pending_samples = soundp_samplesbuffered();

  /* Debug telemetry (only when GENERATOR_DEBUG is set) */
  if (gen_ui->debug_telemetry) {
    if (pending_samples < min_pending)
      min_pending = pending_samples;
    frames_tracked++;
    if (frames_tracked >= 120) {
      fprintf(stderr,
              "GTK4 audio telemetry: min_buffer=%d threshold=%u feedback=%d "
              "rate=%.4f\n",
              min_pending, gen_ctx_sound_threshold(), gen_ctx_sound_feedback(), gen_ui->rate_adjust);
      min_pending = INT_MAX;
      frames_tracked = 0;
    }
  }

  /* If sound buffer is too full (more than 2x threshold), skip this frame
     to let audio catch up and prevent buffer overflow */
  if (pending_samples > (int)(gen_ctx_sound_threshold() * 2)) {
    return G_SOURCE_CONTINUE;
  }

  /*
   * THREADED EXECUTION PATH
   * Emulation runs in a dedicated thread. This tick callback:
   * 1. Checks if a frame was completed by the emulation thread
   * 2. Swaps display buffers and triggers redraw
   * 3. Signals the emulation thread to produce more frames as needed
   */
  if (gen_ui->emu_thread != nullptr) {
    /* Check if emulation thread completed a frame */
    if (atomic_load(&gen_ui->render_complete)) {
      atomic_store(&gen_ui->render_complete, 0);

      /* Swap display buffer (already done in gtk4_cb_end_field via
       * ui_rendertoscreen) */
      gtk_widget_queue_draw(gen_ui->drawing_area);

      /* Update timing for rate control */
      gen_ui->last_frame_time = current_time;
      pending_samples = soundp_samplesbuffered();

      /* Record frame for FPS measurement */
      if (gen_ui->dynamic_rate_control) {
        gen_ui->fps_times[gen_ui->fps_index] = current_time;
        if (gen_ui->frames_recorded < 60)
          gen_ui->frames_recorded++;
        gen_ui->fps_index = (gen_ui->fps_index + 1) % 60;

        /* Calculate rate adjustment */
        if (gen_ui->frames_recorded >= 60) {
          int oldest_idx = gen_ui->fps_index;
          int newest_idx = (gen_ui->fps_index + 59) % 60;
          gint64 time_span =
              gen_ui->fps_times[newest_idx] - gen_ui->fps_times[oldest_idx];

          if (time_span > 1000) {
            gen_ui->measured_fps = 59.0 * 1000000.0 / (double)time_span;
            double target_fps = gen_ctx_vdp_pal() ? 50.0 : 59.94;
            double fps_error = (gen_ui->measured_fps - target_fps) / target_fps;
            double buffer_ratio =
                (double)pending_samples / (double)gen_ctx_sound_threshold();
            double buffer_error = buffer_ratio - 1.0;
            double total_error = (fps_error * 0.25) + (buffer_error * 0.75);

            double adaptive_delta = gen_ui->rate_delta;
            if (buffer_error < -0.40)
              adaptive_delta = 0.08;
            else if (buffer_error < -0.20 && adaptive_delta < 0.05)
              adaptive_delta = 0.05;
            else if (buffer_error > 0.50 && adaptive_delta < 0.05)
              adaptive_delta = 0.05;

            double new_rate_adjust =
                gen_ui->rate_adjust * (1.0 + total_error * 0.25);
            double min_rate = 1.0 - adaptive_delta;
            double max_rate = 1.0 + adaptive_delta;
            if (new_rate_adjust < min_rate)
              new_rate_adjust = min_rate;
            if (new_rate_adjust > max_rate)
              new_rate_adjust = max_rate;

            gen_ui->rate_adjust =
                gen_ui->rate_adjust * 0.9 + new_rate_adjust * 0.1;

            if (gen_ui->debug_telemetry) {
              gen_ui->debug_counter++;
              if (gen_ui->debug_counter >= 60) {
                fprintf(stderr,
                        "DRC: FPS=%.2f (target=%.2f) buffer=%d/%d "
                        "rate_adjust=%.4f\n",
                        gen_ui->measured_fps, target_fps, pending_samples,
                        gen_ctx_sound_threshold(), gen_ui->rate_adjust);
                gen_ui->debug_counter = 0;
              }
            }
          }
        }
      }
    }

    /* Determine if we should request more frames from the emulation thread */
    gboolean should_request =
        (elapsed_us >= frame_duration_us) ||
        (pending_samples < (int)(gen_ctx_sound_threshold() * 0.80));

    if (should_request && !gen_ui->frame_requested) {
      /* Prepare frame parameters and signal emulation thread */
      ui_newframe(pending_samples);

      g_mutex_lock(&gen_ui->emu_mutex);
      gen_ui->frame_requested = TRUE;
      g_cond_signal(&gen_ui->emu_cond);
      g_mutex_unlock(&gen_ui->emu_mutex);
    }

    return G_SOURCE_CONTINUE;
  }

  /* No emulation thread - shouldn't happen, but return safely */
  return G_SOURCE_CONTINUE;
}

static void ui_newframe(int pending_samples)
{
  static int vmode = -1;
  static int pal = -1;
  static int skipcount = 0;
  static char frameplots[60];
  static unsigned int frameplots_i = 0;
  static unsigned int consecutive_skips = 0;
  unsigned int i;
  int fps;
  char fps_string[64];

  if (frameplots_i > gen_ctx_vdp_framerate())
    frameplots_i = 0;

  /* Check interlace mode from VDP register 12 bits 1-2:
     0 = normal (no interlace)
     1 = interlace mode 1 (doubled - even/odd fields identical)
     2 = invalid (treat as doubled)
     3 = interlace mode 2 (double resolution - even/odd fields different) */
  unsigned int interlace_mode = (gen_ctx_vdp_reg()[12] >> 1) & 3;

  if (interlace_mode && gen_ctx_vdp_oddframe()) {
    /* In interlace mode on odd field */
    if (interlace_mode == 3) {
      /* Mode 3 (double resolution): Keep plotfield from even field
         so we render both fields as a pair with different data */
    } else {
      /* Modes 1 & 2 (doubled/invalid): Skip odd fields because they're
         identical to even fields (both render with oddframe=0) */
      gen_ui->plotfield = FALSE;
    }
  } else {
    /* Not in interlace mode, or in interlace mode on even field */
    gen_ui->plotfield = FALSE;
    if (gen_ui->frameskip == 0) {
      /* Dynamic frame skipping based on audio buffer level.
         Use cached pending_samples from caller to avoid redundant polling. */

      /* Startup grace period: render all frames for first 120 frames (~2 sec)
         to allow audio buffer to stabilize before applying skip logic.
         Uses cpu68k_frames which resets on ROM load/reset. */
      if (gen_ctx_cpu68k_frames() < 120) {
        gen_ui->plotfield = TRUE;
        consecutive_skips = 0;
      } else {
        /* After startup: Use adaptive threshold based on samples per field.
           Allow rendering if buffer has at least 1 field worth of audio.
           This is much more achievable than 50% of threshold (2.5 fields). */
        unsigned int min_buffer = gen_ctx_sound_threshold() / 5; /* ~1 field */
        if (pending_samples >= (int)min_buffer) {
          gen_ui->plotfield = TRUE;
          consecutive_skips = 0;
        } else {
          /* Buffer is critically low - skip frame to let audio catch up.
             Force render every 2 frames max to maintain playable FPS. */
          consecutive_skips++;
          if (consecutive_skips >= 2) {
            gen_ui->plotfield = TRUE;
            consecutive_skips = 0;
          }
        }
      }
    } else {
      if (gen_ctx_cpu68k_frames() % (gen_ui->frameskip + 1) == 0)
        gen_ui->plotfield = TRUE;
    }
  }

  if (!gen_ui->plotfield) {
    skipcount++;
    frameplots[frameplots_i++] = 0;
    return;
  }

  /* Check for video mode changes */
  if (vmode != (int)(gen_ctx_vdp_reg()[1] & (1 << 3)) || pal != (int)gen_ctx_vdp_pal()) {
    vdp_setupvideo();
    vmode = gen_ctx_vdp_reg()[1] & (1 << 3);
    pal = gen_ctx_vdp_pal();
  }

  /* Calculate FPS */
  fps = 0;
  for (i = 0; i < gen_ctx_vdp_framerate(); i++) {
    if (frameplots[i])
      fps++;
  }
  frameplots[frameplots_i++] = 1;

  if (gen_ui->statusbar_enabled) {
    snprintf(fps_string, sizeof(fps_string), "FPS: %d", fps);
    gtk_label_set_text(GTK_LABEL(gen_ui->status_label), fps_string);
  }
  skipcount = 0;
}

void ui_line(int line)
{
  static uint8 gfx[320];
  unsigned int width = (gen_ctx_vdp_reg()[12] & 1) ? 320 : 256;

  if (!gen_ui->plotfield)
    return;
  if (line < 0 || line >= (int)gen_ctx_vdp_vislines())
    return;

  if (gen_ui->vdpsimple) {
    if (line == (int)(gen_ctx_vdp_vislines() >> 1))
      /* if we're in simple cell-based mode, plot when half way down screen */
      ui_simpleplot();
    return;
  }

  /* We are plotting this frame, and we're not doing a simple plot */
  switch ((gen_ctx_vdp_reg()[12] >> 1) & 3) {
  case 0: /* normal */
  case 1: /* interlace simply doubled up */
  case 2: /* invalid */
    vdp_renderline(line, gfx, 0);
    break;
  case 3: /* interlace with double resolution */
    vdp_renderline(line, gfx, gen_ctx_vdp_oddframe());
    break;
  }

  uiplot_checkpalcache(0);
  /* Convert 8-bit palette indices to 32-bit RGBA and write to newscreen */
  uiplot_convertdata32(gfx, (uint32 *)(gen_ui->newscreen + line * 384 * 4),
                       width);
}

void ui_endfield(void)
{
  static int counter = 0;

  if (gen_ui->plotfield) {
    ui_rendertoscreen(); /* plot newscreen to screen */

    /* NOTE: Frame rate limiting is handled by ui_tick_callback() using
       GdkFrameClock.

       IMPORTANT: Do NOT add g_usleep() or any blocking delays here!

       GTK4 uses an event-driven architecture. Blocking the main thread with
       g_usleep() prevents audio sample generation, GTK event processing, and
       causes stuttering.

       The console UI can use SDL_Delay() because it has a simple polling loop,
       but GTK4's GdkFrameClock provides VSync-aware timing via
       ui_tick_callback(), so additional delays here would be harmful. */
  }

  if (gen_ui->frameskip == 0) {
    /* dynamic frame skipping */
    counter++;
    if (gen_ctx_sound_feedback() >= 0) {
      gen_ui->actualskip = counter;
      counter = 0;
    }
  } else {
    gen_ui->actualskip = gen_ui->frameskip;
  }
}

static void ui_rendertoscreen(void)
{
  uint32 *newlinedata, *oldlinedata;
  unsigned int line;
  unsigned int nominalwidth = (gen_ctx_vdp_reg()[12] & 1) ? 320 : 256;
  unsigned int yoffset = (gen_ctx_vdp_reg()[1] & (1 << 3)) ? 0 : 8;
  unsigned int xoffset = (gen_ctx_vdp_reg()[12] & 1) ? 0 : 32;
  uint8 *screen;
  uint8 *write_buffer;   /* buffer to write to (opposite of display) */
  uint8 *display_buffer; /* buffer currently being displayed */
  uint32 *evenscreen;    /* interlace: lines 0,2,etc. */
  uint32 *oddscreen;     /*            lines 1,3,etc. */

  /* Double buffering: write to the buffer NOT currently being displayed
   * whichbank indicates which buffer is being displayed:
   *   0 = screen0 is displayed, write to screen1
   *   1 = screen1 is displayed, write to screen0 */
  int bank = atomic_load(&gen_ui->whichbank);
  if (bank == 0) {
    display_buffer = gen_ui->screen0;
    write_buffer = gen_ui->screen1;
  } else {
    display_buffer = gen_ui->screen1;
    write_buffer = gen_ui->screen0;
  }

  /* Render based on interlace mode */
  switch ((gen_ctx_vdp_reg()[12] >> 1) & 3) {
  case 0: /* normal */
  case 1: /* interlace simply doubled up */
  case 2: /* invalid */
    /* Apply upscaling if enabled, otherwise simple copy */
    if (gen_ui->filter_type != FILTER_NONE && gen_ui->scale_factor > 1) {
      /* Calculate scaled dimensions */
      unsigned int scaled_width = nominalwidth * gen_ui->scale_factor;
      unsigned int scaled_height = gen_ctx_vdp_vislines() * gen_ui->scale_factor;

      /* Apply the selected upscaling filter
       * Scale2x/3x/4x: read directly from newscreen using stride parameter
       * xBRZ: needs packed buffer (no stride support) */
      switch (gen_ui->filter_type) {
      case FILTER_SCALE2X:
        /* Read directly from newscreen with stride (384 pixels = 1536 bytes) */
        uiplot_scale2x_frame32((uint32 *)gen_ui->newscreen,
                               gen_ui->upscale_dst_buffer, nominalwidth,
                               gen_ctx_vdp_vislines(), 384 * 4, scaled_width * 4);
        break;
      case FILTER_SCALE3X:
        uiplot_scale3x_frame32((uint32 *)gen_ui->newscreen,
                               gen_ui->upscale_dst_buffer, nominalwidth,
                               gen_ctx_vdp_vislines(), 384 * 4, scaled_width * 4);
        break;
      case FILTER_SCALE4X:
        uiplot_scale4x_frame32((uint32 *)gen_ui->newscreen,
                               gen_ui->upscale_dst_buffer,
                               gen_ui->scale4x_temp_buffer, nominalwidth,
                               gen_ctx_vdp_vislines(), 384 * 4, scaled_width * 4);
        break;
      case FILTER_XBRZ2X:
      case FILTER_XBRZ3X:
      case FILTER_XBRZ4X:
        /* xBRZ needs packed input - copy with stride removal first */
        for (line = 0; line < gen_ctx_vdp_vislines(); line++) {
          newlinedata = (uint32 *)(gen_ui->newscreen + line * 384 * 4);
          memcpy(gen_ui->upscale_src_buffer + line * nominalwidth, newlinedata,
                 nominalwidth * sizeof(uint32));
        }
        if (gen_ui->filter_type == FILTER_XBRZ2X) {
          uiplot_xbrz_frame32(2, gen_ui->upscale_src_buffer,
                              gen_ui->upscale_dst_buffer, nominalwidth,
                              gen_ctx_vdp_vislines());
        } else if (gen_ui->filter_type == FILTER_XBRZ3X) {
          uiplot_xbrz_frame32(3, gen_ui->upscale_src_buffer,
                              gen_ui->upscale_dst_buffer, nominalwidth,
                              gen_ctx_vdp_vislines());
        } else {
          uiplot_xbrz_frame32(4, gen_ui->upscale_src_buffer,
                              gen_ui->upscale_dst_buffer, nominalwidth,
                              gen_ctx_vdp_vislines());
        }
        break;
      default:
        /* Fallback: simple copy without scaling */
        for (line = 0; line < gen_ctx_vdp_vislines(); line++) {
          newlinedata = (uint32 *)(gen_ui->newscreen + line * 384 * 4);
          memcpy(gen_ui->upscale_dst_buffer + line * scaled_width, newlinedata,
                 nominalwidth * sizeof(uint32));
        }
        break;
      }

      /* Copy scaled data to write buffer with proper offsets */
      unsigned int scaled_yoffset = yoffset * gen_ui->scale_factor;
      unsigned int scaled_xoffset = xoffset * gen_ui->scale_factor;
      for (line = 0; line < scaled_height; line++) {
        screen = write_buffer +
                 ((line + scaled_yoffset) * HMAXSIZE + scaled_xoffset) * 4;
        memcpy(screen, gen_ui->upscale_dst_buffer + line * scaled_width,
               scaled_width * sizeof(uint32));
      }
    } else {
      /* No upscaling - simple rendering */
      for (line = 0; line < gen_ctx_vdp_vislines(); line++) {
        newlinedata = (uint32 *)(gen_ui->newscreen + line * 384 * 4);
        screen = write_buffer + ((line + yoffset) * HMAXSIZE + xoffset) * 4;
        memcpy(screen, newlinedata, nominalwidth * 4);
      }
    }
    break;

  case 3: /* interlace with double resolution */
    /* Handle interlaced display - need data from display buffer for interlacing
     */
    for (line = 0; line < gen_ctx_vdp_vislines(); line++) {
      newlinedata = (uint32 *)(gen_ui->newscreen + line * 384 * 4);
      oldlinedata =
          (uint32 *)(display_buffer + ((line + yoffset) * 384 + xoffset) * 4);

      if (gen_ctx_vdp_oddframe()) {
        oddscreen = newlinedata;
        evenscreen = oldlinedata;
      } else {
        evenscreen = newlinedata;
        oddscreen = oldlinedata;
      }

      screen = write_buffer + ((line + yoffset) * 384 + xoffset) * 4;

      /* Apply interlace filter */
      switch (ui_interlace) {
      case DEINTERLACE_WEAVEFILTER:
        uiplot_irender32_weavefilter(evenscreen, oddscreen, screen,
                                     nominalwidth);
        break;
      case DEINTERLACE_BOB:
      case DEINTERLACE_WEAVE:
      default:
        memcpy(screen, newlinedata, nominalwidth * 4);
        break;
      }
    }
    break;
  }

  /* Swap buffers: the buffer we just wrote to becomes the display buffer
   * This ensures Cairo reads from a complete, stable frame while we render
   * the next frame to the other buffer. This eliminates tearing and stuttering.
   */
  int old_bank = atomic_load(&gen_ui->whichbank);
  atomic_store(&gen_ui->whichbank, (old_bank == 0) ? 1 : 0);
}

static void ui_simpleplot(void)
{
  unsigned int line;
  unsigned int width = (gen_ctx_vdp_reg()[12] & 1) ? 320 : 256;
  uint8 gfx[(320 + 16) * (240 + 16)];

  /* cell mode - entire frame done here */
  uiplot_checkpalcache(0);
  vdp_renderframe(gfx + (8 * (320 + 16)) + 8, 320 + 16); /* plot frame */

  for (line = 0; line < gen_ctx_vdp_vislines(); line++) {
    uiplot_convertdata32(gfx + 8 + (line + 8) * (320 + 16),
                         (uint32 *)(gen_ui->newscreen + line * 384 * 4), width);
  }
}

static void ui_sdl_events(void)
{
  SDL_Event event;

  while (SDL_PollEvent(&event)) {
    switch (event.type) {
    /* Gamepad hot-plug events */
    case SDL_EVENT_GAMEPAD_ADDED:
      ui_open_gamepad(event.gdevice.which);
      break;

    case SDL_EVENT_GAMEPAD_REMOVED:
      ui_close_gamepad(event.gdevice.which);
      break;

    /* Gamepad button events */
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
      ui_handle_gamepad_button(&event.gbutton);
      break;

    /* Gamepad axis events */
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
      ui_handle_gamepad_axis(&event.gaxis);
      break;
    }
  }
}

static gboolean ui_gtk4_driver_is_available(const char *driver_id)
{
  if (!driver_id || !*driver_id) {
    return TRUE;
  }
  if (g_ascii_strcasecmp(driver_id, "auto") == 0) {
    return TRUE;
  }

  int count = SDL_GetNumAudioDrivers();
  for (int i = 0; i < count; i++) {
    const char *candidate = SDL_GetAudioDriver(i);
    if (candidate && g_ascii_strcasecmp(candidate, driver_id) == 0) {
      return TRUE;
    }
  }
  return FALSE;
}

static char *ui_gtk4_normalize_audio_driver(const char *driver)
{
  if (!driver) {
    return g_strdup("auto");
  }

  char *copy = g_strdup(driver);
  g_strstrip(copy);
  if (*copy == '\0') {
    g_free(copy);
    return g_strdup("auto");
  }

  char *lower = g_ascii_strdown(copy, -1);
  g_free(copy);

  if (g_strcmp0(lower, "auto") == 0)
    return lower;
  if (g_strcmp0(lower, "pulse") == 0) {
    g_free(lower);
    return g_strdup("pulseaudio");
  }
  if (g_strcmp0(lower, "pipe-wire") == 0) {
    g_free(lower);
    return g_strdup("pipewire");
  }
  if (g_strcmp0(lower, "pw") == 0) {
    g_free(lower);
    return g_strdup("pipewire");
  }
  return lower;
}

static char *ui_gtk4_format_driver_label(const char *driver_id)
{
  if (!driver_id || !*driver_id)
    return g_strdup("Unknown");

  if (g_ascii_strcasecmp(driver_id, "pulseaudio") == 0)
    return g_strdup("PulseAudio");
  if (g_ascii_strcasecmp(driver_id, "pipewire") == 0)
    return g_strdup("PipeWire");
  if (g_ascii_strcasecmp(driver_id, "alsa") == 0)
    return g_strdup("ALSA");
  if (g_ascii_strcasecmp(driver_id, "jack") == 0)
    return g_strdup("JACK");
  if (g_ascii_strcasecmp(driver_id, "dummy") == 0)
    return g_strdup("Dummy (mute)");
  if (g_ascii_strcasecmp(driver_id, "directsound") == 0)
    return g_strdup("DirectSound");
  if (g_ascii_strcasecmp(driver_id, "wasapi") == 0)
    return g_strdup("WASAPI");
  if (g_ascii_strcasecmp(driver_id, "coreaudio") == 0)
    return g_strdup("CoreAudio");

  char *label = g_ascii_strdown(driver_id, -1);
  gboolean capitalize_next = TRUE;
  for (char *p = label; *p; ++p) {
    if (*p == '_' || *p == '-') {
      *p = ' ';
      capitalize_next = TRUE;
      continue;
    }
    if (capitalize_next && g_ascii_isalpha(*p)) {
      *p = g_ascii_toupper(*p);
      capitalize_next = FALSE;
    }
  }
  if (label[0])
    label[0] = g_ascii_toupper(label[0]);
  return label;
}

static void ui_gtk4_rebuild_audio_driver_model(void)
{
  if (!gen_ui)
    return;

  g_clear_object(&gen_ui->audio_driver_model);
  if (gen_ui->audio_driver_ids) {
    g_ptr_array_free(gen_ui->audio_driver_ids, TRUE);
    gen_ui->audio_driver_ids = nullptr;
  }

  gen_ui->audio_driver_model = gtk_string_list_new(nullptr);
  gen_ui->audio_driver_ids = g_ptr_array_new_with_free_func(g_free);

  gtk_string_list_append(gen_ui->audio_driver_model, "Automatic (SDL default)");
  g_ptr_array_add(gen_ui->audio_driver_ids, g_strdup("auto"));

  int count = SDL_GetNumAudioDrivers();
  for (int i = 0; i < count; i++) {
    const char *driver = SDL_GetAudioDriver(i);
    if (!driver || !*driver)
      continue;

    gboolean seen = FALSE;
    for (guint j = 0; j < gen_ui->audio_driver_ids->len; j++) {
      const char *existing = g_ptr_array_index(gen_ui->audio_driver_ids, j);
      if (g_ascii_strcasecmp(existing, driver) == 0) {
        seen = TRUE;
        break;
      }
    }
    if (seen)
      continue;

    char *label = ui_gtk4_format_driver_label(driver);
    gtk_string_list_append(gen_ui->audio_driver_model, label);
    g_ptr_array_add(gen_ui->audio_driver_ids, g_strdup(driver));
    g_free(label);
  }
}

static guint ui_gtk4_find_driver_index(const char *driver_id)
{
  if (!gen_ui || !gen_ui->audio_driver_ids)
    return GTK_INVALID_LIST_POSITION;

  const char *target = (driver_id && *driver_id) ? driver_id : "auto";
  for (guint i = 0; i < gen_ui->audio_driver_ids->len; i++) {
    const char *candidate = g_ptr_array_index(gen_ui->audio_driver_ids, i);
    if (g_ascii_strcasecmp(candidate, target) == 0)
      return i;
  }
  return GTK_INVALID_LIST_POSITION;
}

static void ui_gtk4_update_audio_backend_subtitle(void)
{
  if (!gen_ui || !gen_ui->audio_driver_row)
    return;

  char *subtitle = nullptr;
  if (!sound_on) {
    subtitle = g_strdup("Sound disabled");
  } else {
    const char *current = SDL_GetCurrentAudioDriver();
    if (!current) {
      subtitle = g_strdup("Active backend: (not initialised)");
    } else {
      char *label = ui_gtk4_format_driver_label(current);
      subtitle = g_strdup_printf("Active backend: %s", label);
      g_free(label);
    }
  }

  adw_action_row_set_subtitle(ADW_ACTION_ROW(gen_ui->audio_driver_row),
                              subtitle ? subtitle : "Active backend: Unknown");
  g_free(subtitle);
}

static gboolean ui_gtk4_apply_audio_driver(const char *requested,
                                           gboolean restart_audio,
                                           gboolean persist_config)
{
  if (!gen_ui)
    return FALSE;

  char *target = ui_gtk4_normalize_audio_driver(requested);
  if (!target)
    target = g_strdup("auto");

  if (g_strcmp0(target, "auto") != 0 && !ui_gtk4_driver_is_available(target)) {
    fprintf(stderr,
            "Requested audio backend '%s' is not available; falling back to "
            "SDL default.\n",
            target);
    g_free(target);
    target = g_strdup("auto");
  }

  const char *current =
      gen_ui->audio_driver_selection ? gen_ui->audio_driver_selection : "auto";
  gboolean driver_changed = (g_ascii_strcasecmp(current, target) != 0);

  /* Always set environment variable to ensure SDL uses correct driver.
   * This is critical at startup (restart_audio=FALSE) when sound_init()
   * is called later - SDL_AUDIODRIVER must already be configured. */
  if (g_strcmp0(target, "auto") == 0) {
    g_unsetenv("SDL_AUDIODRIVER");
  } else {
    g_setenv("SDL_AUDIODRIVER", target, TRUE);
  }

  /* Update selection tracking (needed even when driver hasn't changed,
   * as audio_driver_selection may be NULL on first run) */
  g_free(gen_ui->audio_driver_selection);
  gen_ui->audio_driver_selection = g_strdup(target);

  if (!driver_changed) {
    if (persist_config)
      gtkopts_setvalue("audio_driver", target);
    g_free(target);
    return TRUE;
  }

  char *previous = g_strdup(current);

  if (restart_audio) {
    sound_stop();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
  }

  gboolean restart_ok = TRUE;
  if (restart_audio && sound_on) {
    if (sound_start() != 0) {
      restart_ok = FALSE;
    } else if (!gen_ui->running) {
      soundp_pause();
    }
  }

  if (!restart_ok) {
    /* Restore environment variable to previous value */
    if (g_strcmp0(previous, "auto") == 0) {
      g_unsetenv("SDL_AUDIODRIVER");
    } else {
      g_setenv("SDL_AUDIODRIVER", previous, TRUE);
    }
    /* Restore selection tracking */
    g_free(gen_ui->audio_driver_selection);
    gen_ui->audio_driver_selection = g_strdup(previous);

    if (sound_on) {
      SDL_QuitSubSystem(SDL_INIT_AUDIO);
      if (sound_start() != 0) {
        ui_gtk4_messageerror("Failed to recover audio after backend change. "
                             "Sound output may be unavailable until restart.");
      } else if (!gen_ui->running) {
        soundp_pause();
      }
    }
    if (persist_config)
      gtkopts_setvalue("audio_driver", previous);
    ui_gtk4_update_audio_backend_subtitle();
    g_free(previous);
    g_free(target);
    return FALSE;
  }

  if (driver_changed) {
    fprintf(stderr, "Audio backend preference set to: %s\n",
            g_strcmp0(target, "auto") == 0 ? "auto (SDL default)" : target);
  }

  if (persist_config)
    gtkopts_setvalue("audio_driver", target);

  if (restart_audio)
    ui_gtk4_update_audio_backend_subtitle();

  g_free(previous);
  g_free(target);
  return TRUE;
}

static void ui_gtk4_on_audio_driver_changed(GObject *row_object,
                                            GParamSpec *pspec,
                                            gpointer user_data)
{
  (void)pspec;
  (void)user_data;

  if (!gen_ui || !gen_ui->audio_driver_ids)
    return;

  AdwComboRow *row = ADW_COMBO_ROW(row_object);
  guint index = adw_combo_row_get_selected(row);
  if (index == GTK_INVALID_LIST_POSITION)
    return;

  const char *selected_id = g_ptr_array_index(gen_ui->audio_driver_ids, index);
  const char *current =
      gen_ui->audio_driver_selection ? gen_ui->audio_driver_selection : "auto";

  if (g_ascii_strcasecmp(selected_id, current) == 0)
    return;

  if (!ui_gtk4_apply_audio_driver(selected_id, TRUE, TRUE)) {
    guint fallback = ui_gtk4_find_driver_index(current);
    g_signal_handlers_block_by_func(G_OBJECT(row),
                                    ui_gtk4_on_audio_driver_changed, nullptr);
    if (fallback != GTK_INVALID_LIST_POSITION) {
      adw_combo_row_set_selected(row, fallback);
    }
    g_signal_handlers_unblock_by_func(G_OBJECT(row),
                                      ui_gtk4_on_audio_driver_changed, nullptr);
  } else {
    ui_gtk4_update_audio_backend_subtitle();
  }
}

/* Scaler name to filter type mapping */
static const struct {
  const char *name;
  t_filter_type type;
  int scale;
} scaler_map[] = {
    {"none", FILTER_NONE, 1},     {"scale2x", FILTER_SCALE2X, 2},
    {"scale3x", FILTER_SCALE3X, 3}, {"scale4x", FILTER_SCALE4X, 4},
    {"xbrz2x", FILTER_XBRZ2X, 2},   {"xbrz3x", FILTER_XBRZ3X, 3},
    {"xbrz4x", FILTER_XBRZ4X, 4},   {nullptr, FILTER_NONE, 1}};

static void ui_gtk4_apply_scaler(const char *scaler_name)
{
  if (!gen_ui)
    return;

  /* Default to scale2x if not specified */
  if (!scaler_name || !*scaler_name)
    scaler_name = "scale2x";

  /* Find matching scaler */
  for (int i = 0; scaler_map[i].name; i++) {
    if (g_ascii_strcasecmp(scaler_name, scaler_map[i].name) == 0) {
      gen_ui->filter_type = scaler_map[i].type;
      gen_ui->scale_factor = scaler_map[i].scale;
      fprintf(stderr, "Video scaler set to: %s (%dx)\n", scaler_map[i].name,
              scaler_map[i].scale);
      return;
    }
  }

  /* Unknown scaler, default to scale2x */
  fprintf(stderr, "Unknown scaler '%s', defaulting to scale2x\n", scaler_name);
  gen_ui->filter_type = FILTER_SCALE2X;
  gen_ui->scale_factor = 2;
}

static void ui_gtk4_on_scaler_changed(GObject *row_object, GParamSpec *pspec,
                                      gpointer user_data)
{
  (void)pspec;
  (void)user_data;

  if (!gen_ui)
    return;

  AdwComboRow *row = ADW_COMBO_ROW(row_object);
  guint index = adw_combo_row_get_selected(row);
  if (index == GTK_INVALID_LIST_POSITION || index >= G_N_ELEMENTS(scaler_map) - 1)
    return;

  const char *scaler_name = scaler_map[index].name;
  ui_gtk4_apply_scaler(scaler_name);
  gtkopts_setvalue("scaler", scaler_name);

  /* Save config immediately */
  if (gen_ui->configfile)
    gtkopts_save(gen_ui->configfile);

  /* Force redraw */
  if (gen_ui->drawing_area)
    gtk_widget_queue_draw(gen_ui->drawing_area);
}

static guint ui_gtk4_find_scaler_index(t_filter_type filter_type)
{
  for (guint i = 0; scaler_map[i].name; i++) {
    if (scaler_map[i].type == filter_type)
      return i;
  }
  return 1; /* Default to scale2x (index 1) */
}

static void ui_gtk4_ensure_preferences_window(void)
{
  if (!gen_ui)
    return;

  if (gen_ui->prefs_window)
    return;

  AdwPreferencesWindow *prefs =
      ADW_PREFERENCES_WINDOW(adw_preferences_window_new());
  gtk_window_set_title(GTK_WINDOW(prefs), "Preferences");
  gtk_window_set_transient_for(GTK_WINDOW(prefs), GTK_WINDOW(gen_ui->window));
  gtk_window_set_modal(GTK_WINDOW(prefs), TRUE);
  gtk_window_set_hide_on_close(GTK_WINDOW(prefs), TRUE);

  GtkWidget *audio_page = adw_preferences_page_new();
  adw_preferences_page_set_title(ADW_PREFERENCES_PAGE(audio_page), "Audio");
  adw_preferences_page_set_icon_name(ADW_PREFERENCES_PAGE(audio_page),
                                     "audio-speakers-symbolic");

  GtkWidget *audio_group = adw_preferences_group_new();
  adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(audio_group), "Output");
  adw_preferences_group_set_description(
      ADW_PREFERENCES_GROUP(audio_group),
      "Select the SDL audio backend. Changes take effect immediately.");

  ui_gtk4_rebuild_audio_driver_model();

  AdwComboRow *row = ADW_COMBO_ROW(adw_combo_row_new());
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), "Audio Backend");
  adw_combo_row_set_model(row, G_LIST_MODEL(gen_ui->audio_driver_model));
  adw_combo_row_set_use_subtitle(row, TRUE);

  gen_ui->audio_driver_row = GTK_WIDGET(row);

  guint selected = ui_gtk4_find_driver_index(
      gen_ui->audio_driver_selection ? gen_ui->audio_driver_selection : "auto");
  if (selected != GTK_INVALID_LIST_POSITION) {
    adw_combo_row_set_selected(row, selected);
  }

  g_signal_connect(row, "notify::selected",
                   G_CALLBACK(ui_gtk4_on_audio_driver_changed), nullptr);

  adw_preferences_group_add(ADW_PREFERENCES_GROUP(audio_group),
                            GTK_WIDGET(row));
  adw_preferences_page_add(ADW_PREFERENCES_PAGE(audio_page),
                           ADW_PREFERENCES_GROUP(audio_group));
  adw_preferences_window_add(prefs, ADW_PREFERENCES_PAGE(audio_page));

  /* Video preferences page */
  GtkWidget *video_page = adw_preferences_page_new();
  adw_preferences_page_set_title(ADW_PREFERENCES_PAGE(video_page), "Video");
  adw_preferences_page_set_icon_name(ADW_PREFERENCES_PAGE(video_page),
                                     "video-display-symbolic");

  GtkWidget *video_group = adw_preferences_group_new();
  adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(video_group),
                                  "Upscaling");
  adw_preferences_group_set_description(
      ADW_PREFERENCES_GROUP(video_group),
      "Select the video upscaling filter. Scale2x/3x/4x are fast EPX-based "
      "algorithms. xBRZ provides higher quality but uses more CPU.");

  /* Build scaler model - list of display names */
  GtkStringList *scaler_model = gtk_string_list_new(nullptr);
  gtk_string_list_append(scaler_model, "None (Nearest Neighbor)");
  gtk_string_list_append(scaler_model, "Scale2x (2× - Fast)");
  gtk_string_list_append(scaler_model, "Scale3x (3× - Fast)");
  gtk_string_list_append(scaler_model, "Scale4x (4× - Fast)");
  gtk_string_list_append(scaler_model, "xBRZ 2× (High Quality)");
  gtk_string_list_append(scaler_model, "xBRZ 3× (High Quality)");
  gtk_string_list_append(scaler_model, "xBRZ 4× (High Quality)");

  AdwComboRow *scaler_row = ADW_COMBO_ROW(adw_combo_row_new());
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(scaler_row), "Scaler");
  adw_combo_row_set_model(scaler_row, G_LIST_MODEL(scaler_model));

  gen_ui->scaler_row = GTK_WIDGET(scaler_row);

  /* Set current selection */
  guint scaler_index = ui_gtk4_find_scaler_index(gen_ui->filter_type);
  adw_combo_row_set_selected(scaler_row, scaler_index);

  g_signal_connect(scaler_row, "notify::selected",
                   G_CALLBACK(ui_gtk4_on_scaler_changed), nullptr);

  adw_preferences_group_add(ADW_PREFERENCES_GROUP(video_group),
                            GTK_WIDGET(scaler_row));
  adw_preferences_page_add(ADW_PREFERENCES_PAGE(video_page),
                           ADW_PREFERENCES_GROUP(video_group));
  adw_preferences_window_add(prefs, ADW_PREFERENCES_PAGE(video_page));

  gen_ui->prefs_window = GTK_WIDGET(prefs);
  ui_gtk4_update_audio_backend_subtitle();
}

/*** Utility functions ***/

void ui_gtk4_newoptions(void)
{
  /* Called when options change */
}

void ui_gtk4_messageinfo(const char *msg)
{
  if (gen_ui && gen_ui->window) {
    AdwDialog *dialog = adw_alert_dialog_new(nullptr, msg);
    adw_alert_dialog_add_response(ADW_ALERT_DIALOG(dialog), "ok", "OK");
    adw_dialog_present(dialog, GTK_WIDGET(gen_ui->window));
  } else {
    fprintf(stderr, "INFO: %s\n", msg);
  }
}

void ui_gtk4_messageerror(const char *msg)
{
  if (gen_ui && gen_ui->window) {
    AdwDialog *dialog = adw_alert_dialog_new("Error", msg);
    adw_alert_dialog_add_response(ADW_ALERT_DIALOG(dialog), "ok", "OK");
    adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(dialog), "ok");
    adw_dialog_present(dialog, GTK_WIDGET(gen_ui->window));
  } else {
    fprintf(stderr, "ERROR: %s\n", msg);
  }
}

gboolean ui_gtk4_question(const char *msg)
{
  /* TODO: Implement question dialog */
  return FALSE;
}

/*** Logging functions (required by ui.h interface) ***/

void ui_log_debug3(const char *text, ...)
{
}
void ui_log_debug2(const char *text, ...)
{
}
void ui_log_debug1(const char *text, ...)
{
}

void ui_log_user(const char *text, ...)
{
  va_list ap;
  va_start(ap, text);
  vfprintf(stderr, text, ap);
  fputc('\n', stderr);
  va_end(ap);
}

void ui_log_verbose(const char *text, ...)
{
  va_list ap;
  va_start(ap, text);
  vfprintf(stderr, text, ap);
  fputc('\n', stderr);
  va_end(ap);
}

void ui_log_normal(const char *text, ...)
{
  va_list ap;
  va_start(ap, text);
  vfprintf(stderr, text, ap);
  fputc('\n', stderr);
  va_end(ap);
}

void ui_log_critical(const char *text, ...)
{
  va_list ap;
  va_start(ap, text);
  vfprintf(stderr, text, ap);
  fputc('\n', stderr);
  va_end(ap);
}

void ui_log_request(const char *text, ...)
{
  va_list ap;
  va_start(ap, text);
  vfprintf(stderr, text, ap);
  fputc('\n', stderr);
  va_end(ap);
}

void ui_err(const char *text, ...)
{
  va_list ap;
  char buffer[1024];
  va_start(ap, text);
  vsnprintf(buffer, sizeof(buffer), text, ap);
  va_end(ap);
  ui_gtk4_messageerror(buffer);
  fprintf(stderr, "FATAL ERROR: %s\n", buffer);
  exit(1);
}

void ui_musiclog(uint8 *data, unsigned int length)
{
  if (gen_ui->musicfile_fd != -1) {
    write(gen_ui->musicfile_fd, data, length);
  }
}

/*** gen_context Callback Implementations ***/

/* Line rendering callback - called by VDP for each scanline */
static void gtk4_cb_line(gen_context_t *ctx, int line)
{
  (void)ctx; /* Use gen_ui directly for now */
  static uint8 gfx[320];
  unsigned int width = (gen_ctx_vdp_reg()[12] & 1) ? 320 : 256;

  if (!gen_ui->plotfield)
    return;
  if (line < 0 || line >= (int)gen_ctx_vdp_vislines())
    return;

  if (gen_ui->vdpsimple) {
    if (line == (int)(gen_ctx_vdp_vislines() >> 1))
      ui_simpleplot();
    return;
  }

  switch ((gen_ctx_vdp_reg()[12] >> 1) & 3) {
  case 0:
  case 1:
  case 2:
    vdp_renderline(line, gfx, 0);
    break;
  case 3:
    vdp_renderline(line, gfx, gen_ctx_vdp_oddframe());
    break;
  }

  uiplot_checkpalcache(0);
  uiplot_convertdata32(gfx, (uint32 *)(gen_ui->newscreen + line * 384 * 4),
                       width);
}

/* End of field callback - called after each frame completes */
static void gtk4_cb_end_field(gen_context_t *ctx)
{
  (void)ctx;
  static int counter = 0;

  if (gen_ui->plotfield) {
    ui_rendertoscreen();
  }

  if (gen_ui->frameskip == 0) {
    counter++;
    if (gen_ctx_sound_feedback() >= 0) {
      gen_ui->actualskip = counter;
      counter = 0;
    }
  } else {
    gen_ui->actualskip = gen_ui->frameskip;
  }
}

/* Audio output callback */
static void gtk4_cb_audio_output(gen_context_t *ctx, const uint16 *left,
                                  const uint16 *right, unsigned int samples)
{
  (void)ctx;
  (void)left;
  (void)right;
  (void)samples;
  /* Audio is handled by soundp layer directly, this callback is for
   * informational purposes or future audio routing */
}

/* Logging callbacks */
static void gtk4_cb_log_debug(gen_context_t *ctx, const char *msg)
{
  (void)ctx;
  (void)msg;
  /* Debug logs are no-op unless debug mode is enabled */
}

static void gtk4_cb_log_user(gen_context_t *ctx, const char *msg)
{
  (void)ctx;
  fprintf(stderr, "%s\n", msg);
}

static void gtk4_cb_log_verbose(gen_context_t *ctx, const char *msg)
{
  (void)ctx;
  fprintf(stderr, "%s\n", msg);
}

static void gtk4_cb_log_normal(gen_context_t *ctx, const char *msg)
{
  (void)ctx;
  fprintf(stderr, "%s\n", msg);
}

static void gtk4_cb_log_critical(gen_context_t *ctx, const char *msg)
{
  (void)ctx;
  fprintf(stderr, "CRITICAL: %s\n", msg);
}

/* Music logging callback */
static void gtk4_cb_musiclog(gen_context_t *ctx, const uint8 *data,
                              unsigned int length)
{
  (void)ctx;
  if (gen_ui->musicfile_fd != -1) {
    write(gen_ui->musicfile_fd, data, length);
  }
}

/* Fatal error callback - must not return */
[[noreturn]] static void gtk4_cb_fatal_error(gen_context_t *ctx, const char *msg)
{
  (void)ctx;
  ui_gtk4_messageerror(msg);
  fprintf(stderr, "FATAL ERROR: %s\n", msg);
  exit(1);
}

/*** Emulation Thread Implementation ***/

static gpointer ui_emu_thread_func(gpointer data)
{
  GenUI *ui = (GenUI *)data;

  /* Frame timing for autonomous operation during UI blocking (e.g., resize)
   * NTSC: ~16.7ms, PAL: ~20ms. Use 18ms as safe middle ground. */
  const gint64 FRAME_TIMEOUT_US = 18000;

  while (ui->emu_thread_running) {
    g_mutex_lock(&ui->emu_mutex);

    /* Use timed wait instead of indefinite wait to prevent audio starvation
     * when the main thread is blocked (e.g., during fullscreen resize).
     * If timeout expires, we run a frame anyway to keep audio flowing. */
    gboolean frame_was_requested = ui->frame_requested;

    if (!frame_was_requested && ui->emu_thread_running) {
      gint64 end_time = g_get_monotonic_time() + FRAME_TIMEOUT_US;
      /* Wait for signal OR timeout */
      g_cond_wait_until(&ui->emu_cond, &ui->emu_mutex, end_time);
      frame_was_requested = ui->frame_requested;
    }

    if (!ui->emu_thread_running) {
      g_mutex_unlock(&ui->emu_mutex);
      break;
    }

    ui->frame_requested = FALSE;
    g_mutex_unlock(&ui->emu_mutex);

    /* Run frame if:
     * 1. Main thread requested it (normal operation)
     * 2. Timeout expired AND we're running (autonomous mode for audio)
     * In autonomous mode, audio keeps flowing even if UI is blocked */
    if (ui->running && ui->ctx != nullptr) {
      /* Check audio buffer - only run if buffer needs filling
       * This prevents runaway frame generation if UI is blocked long-term */
      int pending = soundp_samplesbuffered();
      int threshold = (int)gen_ctx_sound_threshold();

      /* Run frame if requested, or if audio buffer is getting low */
      if (frame_was_requested || pending < threshold * 2) {
        gen_core_run_frame(ui->ctx);
      }
    }

    /* Signal frame completion (main thread uses this to trigger redraw) */
    atomic_store(&ui->render_complete, 1);
  }

  return nullptr;
}

static void ui_start_emu_thread(void)
{
  g_mutex_init(&gen_ui->emu_mutex);
  g_cond_init(&gen_ui->emu_cond);

  gen_ui->emu_thread_running = TRUE;
  gen_ui->frame_requested = FALSE;
  atomic_store(&gen_ui->render_complete, 0);

  gen_ui->emu_thread = g_thread_new("generator-emu", ui_emu_thread_func, gen_ui);
  fprintf(stderr, "Emulation thread started\n");
}

static void ui_stop_emu_thread(void)
{
  if (gen_ui->emu_thread == nullptr)
    return;

  g_mutex_lock(&gen_ui->emu_mutex);
  gen_ui->emu_thread_running = FALSE;
  g_cond_signal(&gen_ui->emu_cond);
  g_mutex_unlock(&gen_ui->emu_mutex);

  g_thread_join(gen_ui->emu_thread);
  gen_ui->emu_thread = nullptr;

  g_mutex_clear(&gen_ui->emu_mutex);
  g_cond_clear(&gen_ui->emu_cond);

  fprintf(stderr, "Emulation thread stopped\n");
}

/*** SDL3 Gamepad Implementation ***/

static void ui_open_gamepad(SDL_JoystickID id)
{
  if (gen_ui->num_gamepads >= MAX_GAMEPADS)
    return;

  SDL_Gamepad *pad = SDL_OpenGamepad(id);
  if (!pad) {
    fprintf(stderr, "Failed to open gamepad %d: %s\n", (int)id, SDL_GetError());
    return;
  }

  /* Find free slot */
  for (int i = 0; i < MAX_GAMEPADS; i++) {
    if (gen_ui->gamepads[i].gamepad == nullptr) {
      gen_ui->gamepads[i].gamepad = pad;
      gen_ui->gamepads[i].id = id;
      /* Assign to player if slot available (0 or 1) */
      gen_ui->gamepads[i].player = (gen_ui->num_gamepads < 2) ? gen_ui->num_gamepads : -1;
      gen_ui->num_gamepads++;

      const char *name = SDL_GetGamepadName(pad);
      fprintf(stderr, "Gamepad connected: %s (player %d)\n",
              name ? name : "Unknown", gen_ui->gamepads[i].player + 1);
      return;
    }
  }
}

static void ui_close_gamepad(SDL_JoystickID id)
{
  for (int i = 0; i < MAX_GAMEPADS; i++) {
    if (gen_ui->gamepads[i].id == id) {
      fprintf(stderr, "Gamepad disconnected: player %d\n",
              gen_ui->gamepads[i].player + 1);

      SDL_CloseGamepad(gen_ui->gamepads[i].gamepad);
      gen_ui->gamepads[i].gamepad = nullptr;
      gen_ui->gamepads[i].id = 0;
      gen_ui->gamepads[i].player = -1;
      gen_ui->num_gamepads--;
      return;
    }
  }
}

static int ui_gamepad_id_to_player(SDL_JoystickID id)
{
  for (int i = 0; i < MAX_GAMEPADS; i++) {
    if (gen_ui->gamepads[i].gamepad != nullptr && gen_ui->gamepads[i].id == id) {
      return gen_ui->gamepads[i].player;
    }
  }
  return -1;
}

static void ui_handle_gamepad_button(SDL_GamepadButtonEvent *event)
{
  int player = ui_gamepad_id_to_player(event->which);
  if (player < 0 || player > 1)
    return;

  gboolean pressed = (event->down != 0);

  /* 6-button controller mapping for modern gamepads:
   * Genesis A = Gamepad A (SOUTH)
   * Genesis B = Gamepad B (EAST)
   * Genesis C = Gamepad X (WEST)
   * Genesis X = Gamepad Y (NORTH)
   * Genesis Y = Gamepad LB (LEFT_SHOULDER)
   * Genesis Z = Gamepad RB (RIGHT_SHOULDER)
   * Genesis Mode = Gamepad Back/Select (BACK)
   * Genesis Start = Gamepad Start
   */
  switch (event->button) {
  case SDL_GAMEPAD_BUTTON_SOUTH: /* Gamepad A -> Genesis A */
    mem68k_cont[player].a = pressed ? 1 : 0;
    break;
  case SDL_GAMEPAD_BUTTON_EAST: /* Gamepad B -> Genesis B */
    mem68k_cont[player].b = pressed ? 1 : 0;
    break;
  case SDL_GAMEPAD_BUTTON_WEST: /* Gamepad X -> Genesis C */
    mem68k_cont[player].c = pressed ? 1 : 0;
    break;
  case SDL_GAMEPAD_BUTTON_NORTH: /* Gamepad Y -> Genesis X */
    mem68k_cont[player].x = pressed ? 1 : 0;
    break;
  case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: /* Gamepad LB -> Genesis Y */
    mem68k_cont[player].y = pressed ? 1 : 0;
    break;
  case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: /* Gamepad RB -> Genesis Z */
    mem68k_cont[player].z = pressed ? 1 : 0;
    break;
  case SDL_GAMEPAD_BUTTON_BACK: /* Gamepad Back/Select -> Genesis Mode */
    mem68k_cont[player].mode = pressed ? 1 : 0;
    break;
  case SDL_GAMEPAD_BUTTON_START:
    mem68k_cont[player].start = pressed ? 1 : 0;
    break;
  case SDL_GAMEPAD_BUTTON_DPAD_UP:
    mem68k_cont[player].up = pressed ? 1 : 0;
    break;
  case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
    mem68k_cont[player].down = pressed ? 1 : 0;
    break;
  case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
    mem68k_cont[player].left = pressed ? 1 : 0;
    break;
  case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
    mem68k_cont[player].right = pressed ? 1 : 0;
    break;
  default:
    break;
  }
}

static void ui_handle_gamepad_axis(SDL_GamepadAxisEvent *event)
{
  int player = ui_gamepad_id_to_player(event->which);
  if (player < 0 || player > 1)
    return;

  const int deadzone = 8000;

  switch (event->axis) {
  case SDL_GAMEPAD_AXIS_LEFTX:
    if (event->value < -deadzone) {
      mem68k_cont[player].left = 1;
      mem68k_cont[player].right = 0;
    } else if (event->value > deadzone) {
      mem68k_cont[player].left = 0;
      mem68k_cont[player].right = 1;
    } else {
      mem68k_cont[player].left = 0;
      mem68k_cont[player].right = 0;
    }
    break;
  case SDL_GAMEPAD_AXIS_LEFTY:
    if (event->value < -deadzone) {
      mem68k_cont[player].up = 1;
      mem68k_cont[player].down = 0;
    } else if (event->value > deadzone) {
      mem68k_cont[player].up = 0;
      mem68k_cont[player].down = 1;
    } else {
      mem68k_cont[player].up = 0;
      mem68k_cont[player].down = 0;
    }
    break;
  default:
    break;
  }
}
