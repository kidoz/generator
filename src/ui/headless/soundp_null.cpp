/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Null sound platform - no audio output for headless mode */

extern "C" {
#include "machine.h"
#include "gensoundp.h"
#include "gen_context.h"      /* g_ctx */
#include "gen_ui_callbacks.h" /* GEN_UI_CALL */
}

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

/*** soundp_output - forward samples to the audio_output UI callback ***/
/* Unlike the other no-ops here, the rendered samples are forwarded to the
 * registered audio backend via the audio_output UI callback (the standard
 * delivery seam). The headless no-op backend discards them; the capturing
 * backend (--dump-audio) accumulates them for deterministic A/B comparison.
 * Previously this discarded samples, leaving the audio_output seam dead. */
void soundp_output(uint16 *left, uint16 *right, unsigned int samples)
{
  GEN_UI_CALL(g_ctx, audio_output, left, right, samples);
}

/*** soundp_reset - reset audio subsystem (no-op) ***/

int soundp_reset(void)
{
  /* No audio in headless mode */
  return 0;
}
