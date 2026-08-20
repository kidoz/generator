/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Null sound platform - no audio output for headless mode */

#include "machine.h"
#include "gensoundp.h"

/*** soundp_start - start sound hardware (no-op) ***/

int soundp_start(void)
{
  /* No audio in headless mode */
  return 0;
}

/*** soundp_stop - stop sound hardware (no-op) ***/

void soundp_stop(void)
{
  /* No audio in headless mode */
}

/*** soundp_pause - pause audio playback (no-op) ***/

void soundp_pause(void)
{
  /* No audio in headless mode */
}

/*** soundp_resume - resume audio playback (no-op) ***/

void soundp_resume(void)
{
  /* No audio in headless mode */
}

/*** soundp_samplesbuffered - always return 0 for headless ***/

int soundp_samplesbuffered(void)
{
  /* Return 0 to indicate empty buffer - this allows frame pacing
     to run at maximum speed in headless mode */
  return 0;
}

/*** soundp_output - discard samples ***/
/* The core delivers its samples straight to the registered IAudioBackend,
 * so nothing reaches the platform layer in a headless run; this exists only
 * to satisfy the link. */
void soundp_output(const uint16 *, const uint16 *, unsigned int)
{
}

/*** soundp_reset - reset audio subsystem (no-op) ***/

int soundp_reset(void)
{
  /* No audio in headless mode */
  return 0;
}
