/* Forward declarations for state functions */
#include "state.h"

/* Pointer casts: the MAME-style registrations pass pointers to whatever the
 * chip state fields are (INT32, int, unsigned); the transfer layer works on
 * fixed unsigned types. C allowed the implicit conversion; C++ requires the
 * cast. The transfer functions move raw bytes, so signedness is irrelevant. */
#define state_save_register_UINT8(mod, ins, name, val, size) \
  state_transfer8(mod, name, ins, (uint8 *)(val), size);
#define state_save_register_UINT16(mod, ins, name, val, size) \
  state_transfer16(mod, name, ins, (uint16 *)(val), size);
#define state_save_register_UINT32(mod, ins, name, val, size) \
  state_transfer32(mod, name, ins, (uint32 *)(val), size);

#define state_save_register_INT8(mod, ins, name, val, size) \
  state_transfer8(mod, name, ins, (uint8 *)(val), size);
#define state_save_register_INT16(mod, ins, name, val, size) \
  state_transfer16(mod, name, ins, (uint16 *)(val), size);
#define state_save_register_INT32(mod, ins, name, val, size) \
  state_transfer32(mod, name, ins, (uint32 *)(val), size);

#define state_save_register_int(mod, ins, name, val) \
  state_transfer32(mod, name, ins, (uint32 *)(val), 1);

#define state_save_register_func_postload(a) a();
