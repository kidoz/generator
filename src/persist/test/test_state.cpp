/* Unit tests for state save/load functionality */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>

/* Include project headers for proper type definitions */
#include "generator.h"
#include "state.h"
#include "cpu68k.h"
#include "cpuz80.h"

/* VDP state now lives in the class; this test provides the instance the
 * state.cpp serialization (compiled in below) references. */
#include "vdp.hpp"

/* the Vdp instance and vdp_setupvideo now come from vdp.cpp, linked below */
using generator::vdp;

/* Provide mock implementations for dependencies not needed in tests */

/* Mock gen_loglevel for logging macros */
unsigned int gen_loglevel = 0;

/* Mock leafname for slot-based operations */
char gen_leafname[256] = "test_rom";

/* Mock cartinfo */
t_cartinfo gen_cartinfo;

/* 68K CPU state - pointers to match header declarations */
uint8 cpu68k_ram_storage[0x10000];
uint8 *cpu68k_ram = cpu68k_ram_storage;
static uint8 rom_storage[0x200000];
uint8 *cpu68k_rom = rom_storage;
unsigned int cpu68k_clocks = 0;
t_regs regs;

/* Z80 CPU state (cmz80 backend) - pointers to match header declarations */
uint8 cpuz80_ram_storage[LEN_SRAM];
uint8 *cpuz80_ram = cpuz80_ram_storage;
uint8 cpuz80_active;
uint8 cpuz80_resetting;
uint32 cpuz80_bank;
CONTEXTMZ80 cpuz80_z80;

/* Mock other globals */
unsigned int gen_debugmode = 0;
unsigned int gen_autodetect = 0;
unsigned int gen_modifiedrom = 0;
t_musiclog gen_musiclog = musiclog_off;
volatile sig_atomic_t gen_quit = 0;

/* Mock functions */
void gen_reset(void) {
  memset(vdp.vdp_vram, 0, sizeof(vdp.vdp_vram));
  memset(vdp.vdp_cram, 0, sizeof(vdp.vdp_cram));
  memset(vdp.vdp_vsram, 0, sizeof(vdp.vdp_vsram));
  memset(vdp.vdp_reg, 0, sizeof(vdp.vdp_reg));
  memset(cpu68k_ram_storage, 0, sizeof(cpu68k_ram_storage));
  memset(cpuz80_ram_storage, 0, sizeof(cpuz80_ram_storage));
  memset(&regs, 0, sizeof(regs));
  memset(&cpuz80_z80, 0, sizeof(cpuz80_z80));
  vdp.vdp_pal = 0;
  vdp.vdp_overseas = 0;
  vdp.vdp_ctrlflag = 0;
  vdp.vdp_code = (t_code)0;
  vdp.vdp_first = 0;
  vdp.vdp_second = 0;
  vdp.vdp_dmabytes = 0;
  vdp.vdp_address = 0;
  cpuz80_active = 0;
  cpuz80_resetting = 0;
  cpuz80_bank = 0;
}

void event_freeze(unsigned int bytes) { (void)bytes; }
void event_freeze_clocks(unsigned int clocks) { (void)clocks; }
void cpuz80_updatecontext(void) {
  /* Mock - no-op */
}

void YM2612_save_state(void) {
  /* Mock - no-op for testing */
}

void SN76496_save_state(void) {
  /* Mock - no-op for testing */
}

void ui_err(const char *msg, ...) {
  fprintf(stderr, "ERROR: %s\n", msg);
  exit(1);
}

/* Test counters */
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(cond, msg) \
  do { \
    if (!(cond)) { \
      fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); \
      tests_failed++; \
    } else { \
      tests_passed++; \
    } \
  } while (0)

/* Helper to create a temporary file path */
static char *get_temp_file(const char *suffix) {
  static char path[256];
  snprintf(path, sizeof(path), "/tmp/test_state_%d%s", getpid(), suffix);
  return path;
}

/* Test: Full save/load cycle */
static void test_save_load_cycle(void) {
  char *filename = get_temp_file("_cycle.gt0");

  /* Set up known state */
  vdp.vdp_reg[0] = 0x04;
  vdp.vdp_reg[1] = 0x44;
  vdp.vdp_pal = 1;
  regs.pc = 0x00000200;
  regs.sp = 0x00FFFE00;
  regs.regs[0] = 0xDEADBEEF;
  cpu68k_ram[0] = 0xAB;
  cpu68k_ram[1] = 0xCD;
  cpuz80_z80.z80af = 0x1234;
  cpuz80_z80.z80pc = 0x0000;

  /* Save state */
  int save_result = state_savefile(filename);
  TEST_ASSERT(save_result == 0, "state_savefile failed");

  /* Clear state */
  gen_reset();
  TEST_ASSERT(regs.pc == 0, "gen_reset should clear PC");
  TEST_ASSERT(vdp.vdp_reg[0] == 0, "gen_reset should clear VDP regs");

  /* Load state */
  int load_result = state_loadfile(filename);
  TEST_ASSERT(load_result == 0, "state_loadfile failed");

  /* Verify state was restored */
  TEST_ASSERT(vdp.vdp_reg[0] == 0x04, "VDP reg 0 not restored");
  TEST_ASSERT(vdp.vdp_reg[1] == 0x44, "VDP reg 1 not restored");
  TEST_ASSERT(vdp.vdp_pal == 1, "VDP pal not restored");
  TEST_ASSERT(regs.pc == 0x00000200, "68K PC not restored");
  TEST_ASSERT(regs.sp == 0x00FFFE00, "68K SP not restored");
  TEST_ASSERT(regs.regs[0] == 0xDEADBEEF, "68K D0 not restored");
  TEST_ASSERT(cpu68k_ram[0] == 0xAB, "68K RAM[0] not restored");
  TEST_ASSERT(cpu68k_ram[1] == 0xCD, "68K RAM[1] not restored");
  TEST_ASSERT(cpuz80_z80.z80af == 0x1234, "Z80 AF not restored");

  unlink(filename);
}

/* Test: Version check with invalid version */
static void test_version_check_invalid(void) {
  char *filename = get_temp_file("_badver.gt0");

  /* Create a state file with version 99 */
  FILE *f = fopen(filename, "wb");
  TEST_ASSERT(f != nullptr, "Failed to create test file");
  if (!f) return;

  /* Write header */
  fprintf(f, "Generator 0.35 saved state\n");

  /* Write version major = 99 (invalid) */
  /* mod: "ver\0", name: "major\0", instance: 0, bytes: 1, size: 1, data: 99 */
  fwrite("ver", 4, 1, f);
  fwrite("major", 6, 1, f);
  uint8 header[] = {0, 1, 0, 0, 0, 1}; /* instance=0, bytes=1, size=1 */
  fwrite(header, 6, 1, f);
  uint8 bad_version = 99;
  fwrite(&bad_version, 1, 1, f);

  /* Write version minor = 0 */
  fwrite("ver", 4, 1, f);
  fwrite("minor", 6, 1, f);
  fwrite(header, 6, 1, f);
  uint8 minor = 0;
  fwrite(&minor, 1, 1, f);

  fclose(f);

  /* Set up some state that should NOT be modified */
  regs.pc = 0xCAFEBABE;
  vdp.vdp_reg[0] = 0xFF;

  /* Try to load - should fail */
  int result = state_loadfile(filename);
  TEST_ASSERT(result == -1, "state_loadfile should fail for invalid version");

  /* Verify state was NOT modified (version check before gen_reset) */
  TEST_ASSERT(regs.pc == 0xCAFEBABE, "PC should not be modified on version error");
  TEST_ASSERT(vdp.vdp_reg[0] == 0xFF, "VDP regs should not be modified on version error");

  unlink(filename);
}

/* Test: state_date function */
static void test_state_date(void) {
  char temp_base[256];
  snprintf(temp_base, sizeof(temp_base), "/tmp/test_state_%d", getpid());
  snprintf(gen_leafname, sizeof(gen_leafname), "%s", temp_base);

  /* Non-existent file should return 0 */
  time_t date = state_date(5);
  TEST_ASSERT(date == 0, "state_date should return 0 for non-existent file");

  /* Create the file */
  char slotfile[256];
  snprintf(slotfile, sizeof(slotfile), "%s.gt5", temp_base);
  FILE *f = fopen(slotfile, "w");
  TEST_ASSERT(f != nullptr, "Failed to create test file");
  if (f) {
    fprintf(f, "test");
    fclose(f);

    date = state_date(5);
    TEST_ASSERT(date != 0, "state_date should return non-zero for existing file");

    unlink(slotfile);
  }
}

/* Test: Truncated/corrupt file handling */
static void test_corrupt_file(void) {
  char *filename = get_temp_file("_corrupt.gt0");

  /* Create a truncated file */
  FILE *f = fopen(filename, "wb");
  TEST_ASSERT(f != nullptr, "Failed to create test file");
  if (!f) return;

  fprintf(f, "Generator 0.35 saved state\n");
  /* Write incomplete block */
  fwrite("ver", 4, 1, f);
  /* Missing rest of data */
  fclose(f);

  /* Set up state that should NOT be modified */
  regs.pc = 0x12345678;

  int result = state_loadfile(filename);
  TEST_ASSERT(result == -1, "state_loadfile should fail for truncated file");

  /* State should be preserved */
  TEST_ASSERT(regs.pc == 0x12345678, "PC should not be modified on corrupt file");

  unlink(filename);
}

/* Test: slot-based save/load */
static void test_slot_operations(void) {
  snprintf(gen_leafname, sizeof(gen_leafname), "/tmp/test_state_%d", getpid());

  /* Set up state */
  regs.pc = 0xABCD1234;
  vdp.vdp_reg[5] = 0x77;

  /* Save to slot 3 */
  int save_result = state_save(3);
  TEST_ASSERT(save_result == 0, "state_save(3) failed");

  /* Verify slot file exists */
  time_t date = state_date(3);
  TEST_ASSERT(date != 0, "Slot 3 file should exist after save");

  /* Clear and reload */
  gen_reset();
  int load_result = state_load(3);
  TEST_ASSERT(load_result == 0, "state_load(3) failed");

  /* Verify restoration */
  TEST_ASSERT(regs.pc == 0xABCD1234, "PC not restored from slot");
  TEST_ASSERT(vdp.vdp_reg[5] == 0x77, "VDP reg not restored from slot");

  /* Clean up */
  char slotfile[256];
  snprintf(slotfile, sizeof(slotfile), "%s.gt3", gen_leafname);
  unlink(slotfile);
}

/* Test: Non-existent file handling */
static void test_nonexistent_file(void) {
  /* Set up state that should NOT be modified */
  regs.pc = 0x11223344;

  int result = state_loadfile("/nonexistent/path/to/file.gt0");
  TEST_ASSERT(result == -1, "state_loadfile should fail for non-existent file");

  /* State should be preserved */
  TEST_ASSERT(regs.pc == 0x11223344, "PC should not be modified on missing file");
}

/* Test: Large data preservation (VRAM, RAM) */
static void test_large_data_preservation(void) {
  char *filename = get_temp_file("_large.gt0");

  /* Fill VRAM with pattern */
  for (int i = 0; i < LEN_VRAM; i++) {
    vdp.vdp_vram[i] = (uint8)(i & 0xFF);
  }

  /* Fill 68K RAM with pattern */
  for (int i = 0; i < 0x10000; i++) {
    cpu68k_ram[i] = (uint8)((i >> 8) ^ (i & 0xFF));
  }

  /* Save state */
  int save_result = state_savefile(filename);
  TEST_ASSERT(save_result == 0, "state_savefile failed for large data");

  /* Clear state */
  gen_reset();

  /* Load state */
  int load_result = state_loadfile(filename);
  TEST_ASSERT(load_result == 0, "state_loadfile failed for large data");

  /* Verify VRAM pattern */
  int vram_ok = 1;
  for (int i = 0; i < LEN_VRAM; i++) {
    if (vdp.vdp_vram[i] != (uint8)(i & 0xFF)) {
      vram_ok = 0;
      break;
    }
  }
  TEST_ASSERT(vram_ok, "VRAM pattern not preserved");

  /* Verify 68K RAM pattern */
  int ram_ok = 1;
  for (int i = 0; i < 0x10000; i++) {
    if (cpu68k_ram[i] != (uint8)((i >> 8) ^ (i & 0xFF))) {
      ram_ok = 0;
      break;
    }
  }
  TEST_ASSERT(ram_ok, "68K RAM pattern not preserved");

  unlink(filename);
}

/* Test: Z80 register preservation */
static void test_z80_registers(void) {
  char *filename = get_temp_file("_z80.gt0");

  /* Set up Z80 state */
  cpuz80_z80.z80af = 0xABCD;
  cpuz80_z80.z80bc = 0x1234;
  cpuz80_z80.z80de = 0x5678;
  cpuz80_z80.z80hl = 0x9ABC;
  cpuz80_z80.z80afprime = 0xDEF0;
  cpuz80_z80.z80bcprime = 0x1111;
  cpuz80_z80.z80deprime = 0x2222;
  cpuz80_z80.z80hlprime = 0x3333;
  cpuz80_z80.z80ix = 0x4444;
  cpuz80_z80.z80iy = 0x5555;
  cpuz80_z80.z80sp = 0xFFFE;
  cpuz80_z80.z80pc = 0x0100;
  cpuz80_z80.z80i = 0x00;
  cpuz80_z80.z80r = 0x7F;
  cpuz80_active = 1;
  cpuz80_bank = 0x80000;

  /* Save state */
  int save_result = state_savefile(filename);
  TEST_ASSERT(save_result == 0, "state_savefile failed for Z80");

  /* Clear state */
  gen_reset();

  /* Load state */
  int load_result = state_loadfile(filename);
  TEST_ASSERT(load_result == 0, "state_loadfile failed for Z80");

  /* Verify Z80 state */
  TEST_ASSERT(cpuz80_z80.z80af == 0xABCD, "Z80 AF not restored");
  TEST_ASSERT(cpuz80_z80.z80bc == 0x1234, "Z80 BC not restored");
  TEST_ASSERT(cpuz80_z80.z80de == 0x5678, "Z80 DE not restored");
  TEST_ASSERT(cpuz80_z80.z80hl == 0x9ABC, "Z80 HL not restored");
  TEST_ASSERT(cpuz80_z80.z80afprime == 0xDEF0, "Z80 AF' not restored");
  TEST_ASSERT(cpuz80_z80.z80ix == 0x4444, "Z80 IX not restored");
  TEST_ASSERT(cpuz80_z80.z80iy == 0x5555, "Z80 IY not restored");
  TEST_ASSERT(cpuz80_z80.z80sp == 0xFFFE, "Z80 SP not restored");
  TEST_ASSERT(cpuz80_z80.z80pc == 0x0100, "Z80 PC not restored");
  TEST_ASSERT(cpuz80_active == 1, "Z80 active flag not restored");
  TEST_ASSERT(cpuz80_bank == 0x80000, "Z80 bank not restored");

  unlink(filename);
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;

  printf("Running state.c unit tests...\n\n");

  /* Run tests */
  test_save_load_cycle();
  test_version_check_invalid();
  test_state_date();
  test_corrupt_file();
  test_slot_operations();
  test_nonexistent_file();
  test_large_data_preservation();
  test_z80_registers();

  /* Summary */
  printf("\n========================================\n");
  printf("Tests passed: %d\n", tests_passed);
  printf("Tests failed: %d\n", tests_failed);
  printf("========================================\n");

  return tests_failed > 0 ? 1 : 0;
}
