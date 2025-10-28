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
#include <pwd.h>

#include <gtk/gtk.h>
#include <adwaita.h>
#include "SDL.h"

#include "generator.h"
#include "snprintf.h"
#include "ui.h"
#include "ui-gtk4.h"
#include "ui-console.h"
#include "uiplot.h"
#include "gtkopts.h"
#include "vdp.h"
#include "gensound.h"
#include "gensoundp.h"
#include "cpu68k.h"
#include "mem68k.h"
#include "cpuz80.h"
#include "event.h"
#include "state.h"
#include "initcart.h"
#include "patch.h"
#include "dib.h"
#include "avi.h"

/* Global UI instance */
GenUI *gen_ui = NULL;

/* Interlace mode */
t_interlace ui_interlace = DEINTERLACE_WEAVEFILTER;

/* Key names for configuration */
const char *ui_gtk4_keys[] = {
  "a", "b", "c", "start", "left", "right", "up", "down"
};

/*** Forward declarations ***/
static void ui_usage(void);
static void ui_activate(GtkApplication *app, gpointer user_data);
static void ui_startup(GtkApplication *app, gpointer user_data);
static void ui_shutdown(GtkApplication *app, gpointer user_data);
static void ui_create_main_window(GtkApplication *app);
static void ui_setup_actions(GtkApplication *app);
static void ui_draw_callback(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data);
static gboolean ui_key_pressed(GtkEventControllerKey *controller, guint keyval, guint keycode, GdkModifierType state, gpointer user_data);
static gboolean ui_key_released(GtkEventControllerKey *controller, guint keyval, guint keycode, GdkModifierType state, gpointer user_data);
static gboolean ui_tick_callback(GtkWidget *widget, GdkFrameClock *frame_clock, gpointer user_data);
static void ui_newframe(void);
static void ui_simpleplot(void);
static void ui_sdl_events(void);
static void ui_rendertoscreen(void);
static void ui_load_config(void);
static void ui_save_config(void);

/*** Program entry point ***/

int ui_init(int argc, char *argv[])
{
  int ch;
  struct passwd *passwd;
  struct stat statbuf;
  int i;
  const char *name;

  fprintf(stderr, "Generator is (c) James Ponder 1997-2003, all rights "
          "reserved. v" VERSION "\n\n");
  fprintf(stderr, "GTK4/libadwaita UI version\n\n");

  /* Allocate UI structure */
  gen_ui = g_new0(GenUI, 1);
  gen_ui->hborder = HBORDER_DEFAULT;
  gen_ui->vborder = VBORDER_DEFAULT;
  gen_ui->statusbar_enabled = TRUE;
  gen_ui->frameskip = 0;

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
  if (gen_ui->configfile == NULL) {
    passwd = getpwuid(getuid());
    if (passwd == NULL) {
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

  /* Initialize SDL for video and joystick */
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) < 0) {
    fprintf(stderr, "Couldn't initialise SDL: %s\n", SDL_GetError());
    return -1;
  }

  /* Setup joysticks */
  gen_ui->joysticks = SDL_NumJoysticks();
  fprintf(stderr, "%d joysticks detected\n", gen_ui->joysticks);
  for (i = 0; i < gen_ui->joysticks && i < 2; i++) {
    gen_ui->js_handles[i] = SDL_JoystickOpen(i);
    if (gen_ui->js_handles[i]) {
      name = SDL_JoystickName(gen_ui->js_handles[i]);
      fprintf(stderr, "Joystick %d: %s\n", i, name ? name : "Unknown Joystick");
    }
  }
  SDL_JoystickEventState(SDL_ENABLE);

  /* Initialize screen buffers */
  memset(gen_ui->screen_buffers[0], 0, 4 * HMAXSIZE * VMAXSIZE);
  memset(gen_ui->screen_buffers[1], 0, 4 * HMAXSIZE * VMAXSIZE);
  gen_ui->screen0 = gen_ui->screen_buffers[0];
  gen_ui->screen1 = gen_ui->screen_buffers[1];
  gen_ui->newscreen = gen_ui->screen_buffers[2];
  gen_ui->whichbank = 0;
  gen_ui->musicfile_fd = -1;

  /* Set up color conversion for Cairo CAIRO_FORMAT_RGB24
     Format: 32-bit value 0xXXRRGGBB (XX=unused, RR=red, GG=green, BB=blue)
     Red in bits 16-23, Green in bits 8-15, Blue in bits 0-7 */
  uiplot_setshifts(16, 8, 0);  /* red=16, green=8, blue=0 */
  uiplot_setmasks(0x00FF0000, 0x0000FF00, 0x000000FF);  /* 8-bit per channel */

  /* Create GTK application with NON_UNIQUE flag to allow multiple instances */
  gen_ui->app = adw_application_new("net.squish.generator", G_APPLICATION_NON_UNIQUE);
  g_signal_connect(gen_ui->app, "activate", G_CALLBACK(ui_activate), NULL);
  g_signal_connect(gen_ui->app, "startup", G_CALLBACK(ui_startup), NULL);
  g_signal_connect(gen_ui->app, "shutdown", G_CALLBACK(ui_shutdown), NULL);

  fprintf(stderr, "GTK4/libadwaita UI initialized. Use the menu to quit properly.\n");
  return 0;
}

static void ui_usage(void)
{
  fprintf(stderr, "generator [options] [<rom>]\n\n");
  fprintf(stderr, "  -d                     debug mode\n");
  fprintf(stderr, "  -w <work dir>          set work directory\n");
  fprintf(stderr, "  -c <config file>       use alternative config file\n\n");
  fprintf(stderr, "  ROM types supported: .rom or .smd interleaved (autodetected)\n");
  exit(1);
}

void ui_final(void)
{
  if (gen_ui) {
    SDL_Quit();
    g_free(gen_ui->configfile);
    g_free(gen_ui->initload);
    g_free(gen_ui);
    gen_ui = NULL;
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
    gen_loadmemrom(initcart, initcart_len);
  }

  /* Run GTK application */
  int status = g_application_run(G_APPLICATION(gen_ui->app), 0, NULL);
  g_object_unref(gen_ui->app);
  return status;
}

/*** GTK Application callbacks ***/

static void ui_startup(GtkApplication *app, gpointer user_data)
{
  /* Setup application-level actions and menus */
  ui_setup_actions(app);
}

static void ui_activate(GtkApplication *app, gpointer user_data)
{
  /* Create and show main window */
  if (gen_ui->window == NULL) {
    ui_create_main_window(app);
  }
  gtk_window_present(GTK_WINDOW(gen_ui->window));
}

static void ui_shutdown(GtkApplication *app, gpointer user_data)
{
  /* Save configuration */
  if (gtkopts_save(gen_ui->configfile) != 0) {
    fprintf(stderr, "Failed to save configuration\n");
  }
}

/*** Action setup ***/

static void ui_setup_actions(GtkApplication *app)
{
  static const GActionEntry app_entries[] = {
    { "open-rom", ui_action_open_rom, NULL, NULL, NULL },
    { "save-rom", ui_action_save_rom, NULL, NULL, NULL },
    { "load-state", ui_action_load_state, NULL, NULL, NULL },
    { "save-state", ui_action_save_state, NULL, NULL, NULL },
    { "reset", ui_action_reset, NULL, NULL, NULL },
    { "soft-reset", ui_action_soft_reset, NULL, NULL, NULL },
    { "pause", ui_action_pause, NULL, "false", NULL },
    { "preferences", ui_action_preferences, NULL, NULL, NULL },
    { "about", ui_action_about, NULL, NULL, NULL },
    { "quit", ui_action_quit, NULL, NULL, NULL }
  };

  g_action_map_add_action_entries(G_ACTION_MAP(app), app_entries,
                                   G_N_ELEMENTS(app_entries), app);

  /* Set keyboard accelerators */
  const char *open_accels[] = { "<Ctrl>O", NULL };
  const char *load_state_accels[] = { "<Ctrl>L", NULL };
  const char *save_state_accels[] = { "<Ctrl>S", NULL };
  const char *reset_accels[] = { "F5", NULL };
  const char *pause_accels[] = { "space", NULL };
  const char *quit_accels[] = { "<Ctrl>Q", NULL };

  gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.open-rom", open_accels);
  gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.load-state", load_state_accels);
  gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.save-state", save_state_accels);
  gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.reset", reset_accels);
  gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.pause", pause_accels);
  gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.quit", quit_accels);
}

/*** Main window creation ***/

static void ui_create_main_window(GtkApplication *app)
{
  GtkWidget *toolbar_view, *content_box, *menu_button, *open_button, *pause_button;
  GMenu *primary_menu;
  GtkEventController *key_controller;

  /* Create main window */
  gen_ui->window = ADW_APPLICATION_WINDOW(adw_application_window_new(GTK_APPLICATION(app)));
  gtk_window_set_title(GTK_WINDOW(gen_ui->window), "Generator");
  gtk_window_set_default_size(GTK_WINDOW(gen_ui->window), 640, 480);

  /* Create toolbar view (HIG recommended pattern for header bars) */
  toolbar_view = adw_toolbar_view_new();

  /* Create header bar */
  gen_ui->header_bar = adw_header_bar_new();
  adw_header_bar_set_show_back_button(ADW_HEADER_BAR(gen_ui->header_bar), FALSE);
  adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar_view), gen_ui->header_bar);

  /* Add primary action button (Open ROM) to header bar start */
  open_button = gtk_button_new_with_label("Open ROM");
  gtk_actionable_set_action_name(GTK_ACTIONABLE(open_button), "app.open-rom");
  gtk_widget_add_css_class(open_button, "suggested-action");
  adw_header_bar_pack_start(ADW_HEADER_BAR(gen_ui->header_bar), open_button);

  /* Add pause/resume toggle button */
  pause_button = gtk_toggle_button_new();
  gtk_button_set_icon_name(GTK_BUTTON(pause_button), "media-playback-pause-symbolic");
  gtk_actionable_set_action_name(GTK_ACTIONABLE(pause_button), "app.pause");
  gtk_widget_set_tooltip_text(pause_button, "Pause (Space)");
  adw_header_bar_pack_start(ADW_HEADER_BAR(gen_ui->header_bar), pause_button);

  /* Create primary menu (HIG pattern: contains app-wide actions) */
  primary_menu = g_menu_new();

  /* File operations section */
  GMenu *file_section = g_menu_new();
  g_menu_append(file_section, "_Save ROM As…", "app.save-rom");
  g_menu_append_section(primary_menu, NULL, G_MENU_MODEL(file_section));

  /* State management section */
  GMenu *state_section = g_menu_new();
  g_menu_append(state_section, "_Load State…", "app.load-state");
  g_menu_append(state_section, "_Save State…", "app.save-state");
  g_menu_append_section(primary_menu, "Save States", G_MENU_MODEL(state_section));

  /* Emulation control section */
  GMenu *emulation_section = g_menu_new();
  g_menu_append(emulation_section, "_Reset", "app.reset");
  g_menu_append(emulation_section, "_Soft Reset", "app.soft-reset");
  g_menu_append_section(primary_menu, "Emulation", G_MENU_MODEL(emulation_section));

  /* App section (preferences, about, quit) */
  GMenu *app_section = g_menu_new();
  g_menu_append(app_section, "_Preferences", "app.preferences");
  g_menu_append(app_section, "_About Generator", "app.about");
  g_menu_append_section(primary_menu, NULL, G_MENU_MODEL(app_section));

  /* Add primary menu button to header bar end */
  menu_button = gtk_menu_button_new();
  gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(menu_button), "open-menu-symbolic");
  gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(menu_button), G_MENU_MODEL(primary_menu));
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
                                   ui_draw_callback, NULL, NULL);
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
  g_signal_connect(key_controller, "key-pressed", G_CALLBACK(ui_key_pressed), NULL);
  g_signal_connect(key_controller, "key-released", G_CALLBACK(ui_key_released), NULL);
  gtk_widget_add_controller(GTK_WIDGET(gen_ui->window), key_controller);

  /* Add tick callback for emulation loop */
  gtk_widget_add_tick_callback(GTK_WIDGET(gen_ui->window), ui_tick_callback, NULL, NULL);

  /* Show window */
  gtk_window_present(GTK_WINDOW(gen_ui->window));
}

/*** File dialog callbacks ***/

static void on_open_rom_response(GObject *source, GAsyncResult *result, gpointer data)
{
  GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
  GFile *file = gtk_file_dialog_open_finish(dialog, result, NULL);
  if (file) {
    char *filename = g_file_get_path(file);
    char *error = gen_loadimage(filename);
    if (error) {
      ui_gtk4_messageerror(error);
    } else {
      /* ROM loaded successfully, start emulation */
      gen_ui->running = TRUE;
    }
    g_free(filename);
    g_object_unref(file);
  }
}

static void on_save_rom_response(GObject *source, GAsyncResult *result, gpointer data)
{
  GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
  GFile *file = gtk_file_dialog_save_finish(dialog, result, NULL);
  if (file) {
    char *filename = g_file_get_path(file);
    /* TODO: Implement ROM save functionality */
    g_free(filename);
    g_object_unref(file);
  }
}

static void on_load_state_response(GObject *source, GAsyncResult *result, gpointer data)
{
  GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
  GFile *file = gtk_file_dialog_open_finish(dialog, result, NULL);
  if (file) {
    char *filename = g_file_get_path(file);
    if (state_loadfile(filename) != 0) {
      ui_gtk4_messageerror("Failed to load state");
    }
    g_free(filename);
    g_object_unref(file);
  }
}

static void on_save_state_response(GObject *source, GAsyncResult *result, gpointer data)
{
  GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
  GFile *file = gtk_file_dialog_save_finish(dialog, result, NULL);
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

void ui_action_open_rom(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
  GtkFileDialog *dialog;

  dialog = gtk_file_dialog_new();
  gtk_file_dialog_set_title(dialog, "Open ROM");

  gtk_file_dialog_open(dialog, GTK_WINDOW(gen_ui->window), NULL,
                        on_open_rom_response, NULL);
}

void ui_action_save_rom(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
  GtkFileDialog *dialog;

  dialog = gtk_file_dialog_new();
  gtk_file_dialog_set_title(dialog, "Save ROM");

  gtk_file_dialog_save(dialog, GTK_WINDOW(gen_ui->window), NULL,
                        on_save_rom_response, NULL);
}

void ui_action_load_state(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
  GtkFileDialog *dialog;

  dialog = gtk_file_dialog_new();
  gtk_file_dialog_set_title(dialog, "Load State");

  gtk_file_dialog_open(dialog, GTK_WINDOW(gen_ui->window), NULL,
                        on_load_state_response, NULL);
}

void ui_action_save_state(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
  GtkFileDialog *dialog;

  dialog = gtk_file_dialog_new();
  gtk_file_dialog_set_title(dialog, "Save State");

  gtk_file_dialog_save(dialog, GTK_WINDOW(gen_ui->window), NULL,
                        on_save_state_response, NULL);
}

void ui_action_reset(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
  gen_reset();
}

void ui_action_soft_reset(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
  gen_softreset();
}

void ui_action_pause(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
  GVariant *state = g_action_get_state(G_ACTION(action));
  gboolean paused = g_variant_get_boolean(state);
  g_variant_unref(state);

  paused = !paused;
  gen_ui->running = !paused;
  g_simple_action_set_state(action, g_variant_new_boolean(paused));
}

void ui_action_preferences(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
  /* TODO: Implement preferences dialog */
  ui_gtk4_messageinfo("Preferences dialog not yet implemented");
}

void ui_action_about(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
  adw_show_about_dialog((GtkWidget *)gen_ui->window,
                         "application-name", "Generator",
                         "application-icon", "applications-games",
                         "version", VERSION,
                         "developer-name", "James Ponder",
                         "website", "http://www.squish.net/generator/",
                         "copyright", "© 1997-2003 James Ponder",
                         "license-type", GTK_LICENSE_GPL_2_0,
                         "comments", "Sega Genesis / Mega Drive Emulator",
                         NULL);
}

void ui_action_quit(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
  gen_quit = 1;
  g_application_quit(G_APPLICATION(gen_ui->app));
}

/*** Drawing and rendering ***/

static void ui_draw_callback(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data)
{
  cairo_surface_t *surface;
  uint8 *screen_data;
  unsigned int display_width = (vdp_reg[12] & 1) ? 320 : 256;
  unsigned int display_height = vdp_vislines;
  unsigned int xoffset = (vdp_reg[12] & 1) ? 0 : 32;
  unsigned int yoffset = (vdp_reg[1] & (1 << 3)) ? 0 : 8;

  if (!gen_ui || !gen_ui->screen0 || !gen_ui->screen1)
    return;

  /* Double buffering: display the buffer indicated by whichbank
   * whichbank is updated atomically by ui_rendertoscreen() after each frame
   * completes, so we're always reading from a stable, complete frame. */
  screen_data = (gen_ui->whichbank == 0) ? gen_ui->screen0 : gen_ui->screen1;

  /* Create Cairo image surface from emulator buffer
     Format: CAIRO_FORMAT_RGB24 which is 32-bit with unused alpha */
  surface = cairo_image_surface_create_for_data(
    screen_data + (yoffset * 384 + xoffset) * 4,  /* offset into buffer */
    CAIRO_FORMAT_RGB24,
    display_width,
    display_height,
    384 * 4  /* stride: full buffer width including borders */
  );

  /* Scale to fit drawing area while maintaining aspect ratio */
  cairo_scale(cr, (double)width / display_width, (double)height / display_height);

  /* Draw the emulator screen */
  cairo_set_source_surface(cr, surface, 0, 0);
  cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
  cairo_paint(cr);

  cairo_surface_destroy(surface);
}

/*** Input handling ***/

static gboolean ui_key_pressed(GtkEventControllerKey *controller, guint keyval,
                                guint keycode, GdkModifierType state, gpointer user_data)
{
  /* TODO: Implement emulator key handling */
  return FALSE;
}

static gboolean ui_key_released(GtkEventControllerKey *controller, guint keyval,
                                 guint keycode, GdkModifierType state, gpointer user_data)
{
  /* TODO: Implement emulator key handling */
  return FALSE;
}

/*** Emulation loop ***/

static gboolean ui_tick_callback(GtkWidget *widget, GdkFrameClock *frame_clock, gpointer user_data)
{
  static gint64 last_frame_time = 0;
  gint64 current_time;
  gint64 elapsed_us;
  gint64 frame_duration_us;
  int pending_samples;

  if (!gen_ui->running || gen_quit)
    return G_SOURCE_CONTINUE;

  /* Get current time in microseconds
     Note: GdkFrameClock provides VSync-aware timing automatically.
     This callback is invoked in sync with the display refresh rate. */
  current_time = g_get_monotonic_time();

  if (last_frame_time == 0) {
    last_frame_time = current_time;
  }

  /* Calculate frame duration based on Genesis timing
     NTSC: 60Hz = 16666.67 microseconds per frame
     PAL:  50Hz = 20000 microseconds per frame */
  frame_duration_us = vdp_pal ? 20000 : 16667;

  elapsed_us = current_time - last_frame_time;

  /* Check sound buffer status to prevent overflow */
  pending_samples = soundp_samplesbuffered();

  /* If sound buffer is too full (more than 2x threshold), skip this frame
     to let audio catch up and prevent buffer overflow.
     This provides backpressure to maintain audio/video sync. */
  if (pending_samples > (int)(sound_threshold * 2)) {
    return G_SOURCE_CONTINUE;
  }

  /* Only execute frame if enough time has elapsed.
     With GdkFrameClock's VSync-aware callbacks, this provides smooth rendering
     without tearing while maintaining correct emulation speed. */
  if (elapsed_us >= frame_duration_us) {
    ui_sdl_events();
    ui_newframe();
    event_doframe();
    gtk_widget_queue_draw(gen_ui->drawing_area);

    /* Update last frame time
       Note: We use current_time rather than += frame_duration_us
       to avoid accumulating timing errors over long sessions */
    last_frame_time = current_time;
  }

  /* Callback will run again on next display refresh, but will skip execution
     if not enough time has elapsed for proper emulation timing */
  return G_SOURCE_CONTINUE;
}

static void ui_newframe(void)
{
  static int vmode = -1;
  static int pal = -1;
  static int skipcount = 0;
  static char frameplots[60];
  static unsigned int frameplots_i = 0;
  unsigned int i;
  int fps;
  char fps_string[64];

  if (frameplots_i > vdp_framerate)
    frameplots_i = 0;

  /* Check interlace mode from VDP register 12 bits 1-2:
     0 = normal (no interlace)
     1 = interlace mode 1 (doubled - even/odd fields identical)
     2 = invalid (treat as doubled)
     3 = interlace mode 2 (double resolution - even/odd fields different) */
  unsigned int interlace_mode = (vdp_reg[12] >> 1) & 3;

  if (interlace_mode && vdp_oddframe) {
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
      /* Dynamic frameskip: always plot unless sound is way ahead
         sound_feedback: -1 = need more sound, 0 = have enough sound */
      gen_ui->plotfield = TRUE;  /* Always render in dynamic mode */
    } else {
      if (cpu68k_frames % (gen_ui->frameskip + 1) == 0)
        gen_ui->plotfield = TRUE;
    }
  }

  if (!gen_ui->plotfield) {
    skipcount++;
    frameplots[frameplots_i++] = 0;
    return;
  }

  /* Check for video mode changes */
  if (vmode != (int)(vdp_reg[1] & (1 << 3)) || pal != (int)vdp_pal) {
    vdp_setupvideo();
    vmode = vdp_reg[1] & (1 << 3);
    pal = vdp_pal;
  }

  /* Calculate FPS */
  fps = 0;
  for (i = 0; i < vdp_framerate; i++) {
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
  unsigned int width = (vdp_reg[12] & 1) ? 320 : 256;

  if (!gen_ui->plotfield)
    return;
  if (line < 0 || line >= (int)vdp_vislines)
    return;

  if (gen_ui->vdpsimple) {
    if (line == (int)(vdp_vislines >> 1))
      /* if we're in simple cell-based mode, plot when half way down screen */
      ui_simpleplot();
    return;
  }

  /* We are plotting this frame, and we're not doing a simple plot */
  switch ((vdp_reg[12] >> 1) & 3) {
  case 0:                    /* normal */
  case 1:                    /* interlace simply doubled up */
  case 2:                    /* invalid */
    vdp_renderline(line, gfx, 0);
    break;
  case 3:                    /* interlace with double resolution */
    vdp_renderline(line, gfx, vdp_oddframe);
    break;
  }

  uiplot_checkpalcache(0);
  /* Convert 8-bit palette indices to 32-bit RGBA and write to newscreen */
  uiplot_convertdata32(gfx, (uint32 *)(gen_ui->newscreen + line * 384 * 4), width);
}

void ui_endfield(void)
{
  static int counter = 0;
  static uint64_t last_frame_time = 0;
  static uint64_t frame_count = 0;
  uint64_t current_time;
  uint64_t elapsed_time;
  uint32_t target_frame_time_ms;
  uint32_t delay_needed;

  if (gen_ui->plotfield) {
    ui_rendertoscreen();        /* plot newscreen to screen */

    /* Frame rate limiting - ensure we don't run faster than target framerate
       Genesis timing: NTSC = 60 Hz (16.67ms), PAL = 50 Hz (20ms) */
    frame_count++;
    current_time = g_get_monotonic_time() / 1000;  /* Convert microseconds to milliseconds */

    if (last_frame_time == 0) {
      last_frame_time = current_time;
    } else {
      /* Calculate target frame time in milliseconds
         vdp_framerate is set in vdp.c: 60 for NTSC, 50 for PAL */
      target_frame_time_ms = 1000 / vdp_framerate;  /* 16ms for NTSC, 20ms for PAL */

      elapsed_time = current_time - last_frame_time;

      /* If we're running too fast, delay to maintain proper frame rate */
      if (elapsed_time < target_frame_time_ms) {
        delay_needed = target_frame_time_ms - elapsed_time;
        /* Cap delay at 2x target time to avoid hanging if timing goes wrong */
        if (delay_needed > 0 && delay_needed < target_frame_time_ms * 2) {
          g_usleep(delay_needed * 1000);  /* g_usleep takes microseconds */
          current_time = g_get_monotonic_time() / 1000;
        }
      }

      last_frame_time = current_time;
    }
  }

  if (gen_ui->frameskip == 0) {
    /* dynamic frame skipping */
    counter++;
    if (sound_feedback >= 0) {
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
  unsigned int nominalwidth = (vdp_reg[12] & 1) ? 320 : 256;
  unsigned int yoffset = (vdp_reg[1] & (1 << 3)) ? 0 : 8;
  unsigned int xoffset = (vdp_reg[12] & 1) ? 0 : 32;
  uint8 *screen;
  uint8 *write_buffer;          /* buffer to write to (opposite of display) */
  uint8 *display_buffer;        /* buffer currently being displayed */
  uint32 *evenscreen;           /* interlace: lines 0,2,etc. */
  uint32 *oddscreen;            /*            lines 1,3,etc. */

  /* Double buffering: write to the buffer NOT currently being displayed
   * whichbank indicates which buffer is being displayed:
   *   0 = screen0 is displayed, write to screen1
   *   1 = screen1 is displayed, write to screen0 */
  if (gen_ui->whichbank == 0) {
    display_buffer = gen_ui->screen0;
    write_buffer = gen_ui->screen1;
  } else {
    display_buffer = gen_ui->screen1;
    write_buffer = gen_ui->screen0;
  }

  /* Render based on interlace mode */
  switch ((vdp_reg[12] >> 1) & 3) {
  case 0:                    /* normal */
  case 1:                    /* interlace simply doubled up */
  case 2:                    /* invalid */
    /* Simple rendering - copy newscreen to the write buffer */
    for (line = 0; line < vdp_vislines; line++) {
      newlinedata = (uint32 *)(gen_ui->newscreen + line * 384 * 4);
      screen = write_buffer + ((line + yoffset) * 384 + xoffset) * 4;
      memcpy(screen, newlinedata, nominalwidth * 4);
    }
    break;

  case 3:                    /* interlace with double resolution */
    /* Handle interlaced display - need data from display buffer for interlacing */
    for (line = 0; line < vdp_vislines; line++) {
      newlinedata = (uint32 *)(gen_ui->newscreen + line * 384 * 4);
      oldlinedata = (uint32 *)(display_buffer + ((line + yoffset) * 384 + xoffset) * 4);

      if (vdp_oddframe) {
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
        uiplot_irender32_weavefilter(evenscreen, oddscreen, screen, nominalwidth);
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
   * the next frame to the other buffer. This eliminates tearing and stuttering. */
  gen_ui->whichbank = (gen_ui->whichbank == 0) ? 1 : 0;
}

static void ui_simpleplot(void)
{
  unsigned int line;
  unsigned int width = (vdp_reg[12] & 1) ? 320 : 256;
  uint8 gfx[(320 + 16) * (240 + 16)];

  /* cell mode - entire frame done here */
  uiplot_checkpalcache(0);
  vdp_renderframe(gfx + (8 * (320 + 16)) + 8, 320 + 16);    /* plot frame */

  for (line = 0; line < vdp_vislines; line++) {
    uiplot_convertdata32(gfx + 8 + (line + 8) * (320 + 16),
                         (uint32 *)(gen_ui->newscreen + line * 384 * 4), width);
  }
}

static void ui_sdl_events(void)
{
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    /* TODO: Handle SDL joystick events */
  }
}

/*** Utility functions ***/

void ui_gtk4_newoptions(void)
{
  /* Called when options change */
}

void ui_gtk4_messageinfo(const char *msg)
{
  if (gen_ui && gen_ui->window) {
    AdwDialog *dialog = adw_alert_dialog_new(NULL, msg);
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

void ui_log_debug3(const char *text, ...) { }
void ui_log_debug2(const char *text, ...) { }
void ui_log_debug1(const char *text, ...) { }

void ui_log_user(const char *text, ...)
{
  va_list ap;
  va_start(ap, text);
  vfprintf(stderr, text, ap);
  va_end(ap);
}

void ui_log_verbose(const char *text, ...)
{
  va_list ap;
  va_start(ap, text);
  vfprintf(stderr, text, ap);
  va_end(ap);
}

void ui_log_normal(const char *text, ...)
{
  va_list ap;
  va_start(ap, text);
  vfprintf(stderr, text, ap);
  va_end(ap);
}

void ui_log_critical(const char *text, ...)
{
  va_list ap;
  va_start(ap, text);
  vfprintf(stderr, text, ap);
  va_end(ap);
}

void ui_log_request(const char *text, ...)
{
  va_list ap;
  va_start(ap, text);
  vfprintf(stderr, text, ap);
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
}

void ui_musiclog(uint8 *data, unsigned int length)
{
  if (gen_ui->musicfile_fd != -1) {
    write(gen_ui->musicfile_fd, data, length);
  }
}
