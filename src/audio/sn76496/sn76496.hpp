/* This code comes from MAME 0.53 and according to changes log was written
   by Nicola Salmoria.  The MAME license says:

   VI. Reuse of Source Code
   --------------------------
   This chapter might not apply to specific portions of MAME (e.g., CPU
   emulators) which bear different copyright notices.
   The source code cannot be used in a commercial product without the written
   authorization of the authors. Use in non-commercial products is allowed,
   and indeed encouraged.  If you use portions of the MAME source code in your
   program, however, you must make the full source code freely available as
   well.
   Usage of the _information_ contained in the source code is free for any use.
   However, given the amount of time and energy it took to collect this
   information, if you find new information we would appreciate if you made it
   freely available as well.

*/

#ifndef SN76496_HPP
#define SN76496_HPP

#include "machine.h"

#include <cstdint>

namespace generator {

inline constexpr int MAX_76496 = 1;

/* SN76489 / SN76496 programmable tone/noise generator (TMS9919-compatible).
 *
 * C++ port of the MAME-derived C core: the chip state that used to live in
 * the global `struct SN76496 sn[]` array is owned by the class. The
 * transitional global array and the extern "C" free functions below keep the
 * existing mixer/save-state call sites working until those subsystems are
 * repointed onto System-owned instances; they are the compatibility ABI, not
 * the API. Data members stay public because the save-state layer and the
 * characterization tests access them directly; that coupling is removed when
 * serialization moves into the class in a later rewrite phase.
 */
class SN76496 {
public:
  int SampleRate;
  unsigned int UpdateStep;
  int VolTable[16]; /* volume table */
  int Register[8];  /* registers */
  int LastRegister; /* last register written */
  int Volume[4];    /* volume of voice 0-2 and noise */
  unsigned int RNG; /* noise generator */
  int NoiseFB;      /* noise feedback mask */
  int Period[4];
  int Count[4];
  int Output[4];

  int init(int clock, int gain, int sample_rate);
  void write(int data);
  void update(uint16 *buffer, int length);
  void save_state(int chip_index);

private:
  void set_clock(int clock);
  void set_gain(int gain);
  static int parity(unsigned int val);
};

extern SN76496 sn[MAX_76496];

} // namespace generator

#ifdef __cplusplus
extern "C" {
#endif

/* Transitional C ABI over the array above - see class comment. */
int SN76496Init(int chip, int clock, int gain, int sample_rate);
void SN76496Write(int chip, int data);
void SN76496Update(int chip, uint16 *buffer, int length);
void SN76496_save_state(void);

#ifdef __cplusplus
}
#endif

#endif /* SN76496_HPP */
