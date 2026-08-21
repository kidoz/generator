/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Process entry point.
 *
 * main() hands straight over to the UI backend, which owns the machine
 * through EmulatorCore. The globals defined here are process-wide settings
 * the backends and the logging macros read -- no emulation state reaches
 * this file. */

#include "generator.h"
#include "ui.h"

#include <csignal>

/*** variables externed in generator.h ***/

volatile sig_atomic_t gen_quit = 0; /* Signal-safe flag for clean shutdown */
#include <cstdlib>
unsigned int gen_loglevel = /* normal + critical */
    std::getenv("GEN_LOG_VERBOSE") != nullptr ? GEN_LOG_VERBOSE
                                              : GEN_LOG_NORMAL;
unsigned int gen_modifiedrom = 0; /* set when a patch is applied */

/*** Signal handler ***/

/* POSIX-compliant signal handler - only sets a flag
 *
 * IMPORTANT: Signal handlers MUST only call async-signal-safe functions.
 * Functions like ui_final(), exit(), malloc(), printf(), LOG_*() are NOT safe.
 * Setting a volatile sig_atomic_t variable is the only safe operation.
 *
 * The main loop checks gen_quit and performs proper cleanup when it's set.
 */
static void gen_sighandler(int signum)
{
  /* Only operation allowed in signal handler: set atomic flag */
  gen_quit = 1;

  /* Re-install signal handler for non-BSD systems */
  signal(signum, gen_sighandler);
}

/*** Program entry point ***/
/* Not compiled when building as library (headless mode, which brings its
 * own main() and only wants the globals above) */
#ifndef GENERATOR_LIB_ONLY

int main(int argc, char *argv[])
{
  int retval;

  /* initialise user interface */
  if ((retval = ui_init(argc, argv)))
    return retval;

  /* Install signal handlers for graceful shutdown */
  signal(SIGINT, gen_sighandler);  /* Ctrl+C */
  signal(SIGTERM, gen_sighandler); /* kill command / systemd stop */

  /* enter user interface loop */
  return ui_loop();
}

#endif /* GENERATOR_LIB_ONLY */
