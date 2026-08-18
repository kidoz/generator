/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Shared ui_err implementation for backends with no error UI of their own */

#include "ui.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

/* The gtkmm and NodalKit backends carried identical copies of this. A
 * backend that can show the user a dialog should define ui_err itself and
 * drop this translation unit from its source list; the console backend
 * already does, because it has to restore the terminal before printing. */
[[noreturn]] void ui_err(const char *text, ...)
{
  va_list ap;

  va_start(ap, text);
  fprintf(stderr, "FATAL ERROR: ");
  vfprintf(stderr, text, ap);
  fprintf(stderr, "\n");
  va_end(ap);

  exit(1);
}
