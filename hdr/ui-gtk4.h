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
#define MAX_SCALE_FACTOR 4
#define HMAXSIZE ((320 * MAX_SCALE_FACTOR) + 2 * HBORDER_MAX)
#define VMAXSIZE ((240 * MAX_SCALE_FACTOR) + 2 * VBORDER_MAX)

/* Upscaling filter types */
typedef enum {
  FILTER_NONE = 0,      /* Nearest neighbor (no filtering) */
  FILTER_SCALE2X = 1,   /* Scale2x/EPX algorithm */
  FILTER_SCALE3X = 2,   /* Scale3x algorithm */
  FILTER_SCALE4X = 3,   /* Scale4x algorithm */
  FILTER_XBRZ2X = 4,    /* xBRZ 2x - High quality */
  FILTER_XBRZ3X = 5,    /* xBRZ 3x - High quality */
  FILTER_XBRZ4X = 6     /* xBRZ 4x - High quality */
} t_filter_type;

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
  GtkWidget *audio_driver_row;
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

  /* Upscaling */
  t_filter_type filter_type;
  int scale_factor; /* 1-4 for scale factors */
  guint32 *upscale_src_buffer;   /* Pre-allocated source buffer for upscaling */
  guint32 *upscale_dst_buffer;   /* Pre-allocated destination buffer for upscaling */
  unsigned int upscale_buffer_size; /* Current allocated buffer size */

  /* Dynamic Rate Control */
  gboolean dynamic_rate_control;  /* Enable/disable dynamic rate control */
  double rate_adjust;              /* Current rate adjustment factor (1.0 = normal) */
  double rate_delta;               /* Maximum adjustment delta (default 0.005 = 0.5%) */
  gint64 fps_times[60];            /* Last 60 frame timestamps for FPS calculation */
  int fps_index;                   /* Current index in fps_times array */
  double measured_fps;             /* Measured FPS (rolling average) */
  int frames_recorded;             /* Number of frames recorded so far (max 60) */
  int debug_counter;               /* Counter for debug output */
  gint64 last_frame_time;          /* Last frame time for timing */

  /* SDL and rendering */
  void *screen; /* SDL_Surface pointer */
  guint8 *screen_buffers[3];
  guint8 *screen0;
  guint8 *screen1;
  guint8 *newscreen;
  volatile int whichbank;  /* Volatile to prevent race condition between render and draw threads */
  gboolean locksurface;
  gboolean plotfield;
  gboolean vdpsimple;

  /* Audio configuration */
  GtkStringList *audio_driver_model;
  GPtrArray *audio_driver_ids;
  char *audio_driver_selection;

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
