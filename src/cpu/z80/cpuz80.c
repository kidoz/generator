#include "generator.h"

#ifdef RAZE
#include "cpuz80-raze.c"
#elif defined(CPPZ80)
/* CPPZ80 provides its own C ABI implementation in cpuz80-cppz80.cpp */
#else
#ifdef CMZ80
#include "cpuz80-mz80.c"
#else
#error "No z80 defined"
#endif
#endif
