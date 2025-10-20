/* Generator is (c) James Ponder, 1997-2001 http://www.squish.net/generator/ */
/* GTK4/libadwaita user interface header */

#ifndef UI_GTK4_H
#define UI_GTK4_H

#include <gtk/gtk.h>
#include <adwaita.h>

#define HBORDER_MAX 32
#define HBORDER_DEFAULT 8
#define VBORDER_MAX 32
#define VBORDER_DEFAULT 8
#define HMAXSIZE (320 + 2 * HBORDER_MAX)
#define VMAXSIZE (240 + 2 * VBORDER_MAX)

typedef struct {
  unsigned int a;
  unsigned int b;
  unsigned int c;
  unsigned int up;
  unsigned int down;
  unsigned int left;
  unsigned int right;
  unsigned int start;
} t_gtk4keys;

typedef struct {
  AdwApplication *app;
  AdwApplicationWindow *window;
  GtkWidget *header_bar;
  GtkWidget *drawing_area;
  GtkWidget *status_label;
  GMenuModel *menu_model;

  /* Dialogs */
  GtkWidget *prefs_window;
  GtkWidget *about_dialog;
  GtkWidget *console_window;

  /* State */
  gboolean running;
  guint8 frameskip;
  guint8 actualskip;
  gboolean statusbar_enabled;
  int screen_mode; /* 0=100%, 1=200%, 2=fullscreen */
  unsigned int hborder;
  unsigned int vborder;

  /* SDL and rendering */
  void *screen; /* SDL_Surface pointer */
  guint8 *screen_buffers[3];
  guint8 *screen0;
  guint8 *screen1;
  guint8 *newscreen;
  int whichbank;
  gboolean locksurface;
  gboolean plotfield;
  gboolean vdpsimple;

  /* Input */
  t_gtk4keys controllers[2];
  int joysticks;
  void *js_handles[2]; /* SDL_Joystick pointers */

  /* Recording */
  int musicfile_fd;
  void *avi; /* t_avi pointer */
  guint8 *avi_video;
  guint8 *avi_audio;

  /* Config */
  char *configfile;
  char *initload;
} GenUI;

/* Global UI instance */
extern GenUI *gen_ui;

/* UI Functions */
void ui_gtk4_newoptions(void);
void ui_gtk4_messageinfo(const char *msg);
void ui_gtk4_messageerror(const char *msg);
gboolean ui_gtk4_question(const char *msg);

/* Action callbacks */
void ui_action_open_rom(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void ui_action_save_rom(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void ui_action_load_state(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void ui_action_save_state(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void ui_action_reset(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void ui_action_soft_reset(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void ui_action_pause(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void ui_action_preferences(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void ui_action_about(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void ui_action_quit(GSimpleAction *action, GVariant *parameter, gpointer user_data);

/* Rendering */
void ui_gtk4_sizechange(void);
void ui_gtk4_rendertoscreen(void);

#endif /* UI_GTK4_H */
