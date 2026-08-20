#ifndef GENERATOR_GENSOUNDP_H
#define GENERATOR_GENSOUNDP_H

#include "machine.h"

/* Audio output platform layer.
 *
 * The core produces samples and hands them to an IAudioBackend; a backend
 * that plays them on real hardware calls through here. The few numbers the
 * platform and the UI pacing loops have to agree on live in this header —
 * they used to come from the sound coordinator that owned the mixer, which
 * no longer exists. */

/* Output sample rate. Meson passes -DSOUND_SAMPLERATE from the audio-rate
 * option; the fallback keeps the header usable on its own. */
#ifndef SOUND_SAMPLERATE
#define SOUND_SAMPLERATE 48000
#endif

/* Buffer sizing is fixed rather than rate-dependent so a single stack
 * buffer covers every supported configuration: the highest rate we accept
 * at the slowest field rate (50 Hz PAL). */
#define SOUND_MAXRATE 96000
#define SOUND_BUFFER_SAMPLES (SOUND_MAXRATE / 50)

/* Latency policy, shared by the platform layer and the UI field pacing.
 * A backend lets a field through early when the queue is close to running
 * dry and skips one when it is well past the threshold, so the emulation
 * rate tracks the sound hardware rather than the wall clock alone. Fields
 * are counted at the PAL rate: it is the slower of the two, so the
 * threshold is a floor in time under NTSC as well. */
#define SOUNDP_MIN_FIELDS 5
#define SOUNDP_SAMPLES_PER_FIELD (SOUND_SAMPLERATE / 50)
#define SOUNDP_THRESHOLD (SOUNDP_SAMPLES_PER_FIELD * SOUNDP_MIN_FIELDS)

int soundp_start(void);
void soundp_stop(void);
void soundp_pause(void);
void soundp_resume(void);
int soundp_samplesbuffered(void);
void soundp_output(const uint16 *left, const uint16 *right,
                   unsigned int samples);
int soundp_reset(void); /* Full audio subsystem restart */

#endif /* GENERATOR_GENSOUNDP_H */
