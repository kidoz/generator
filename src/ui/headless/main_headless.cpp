/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Headless Backend - Run emulator without UI for testing/benchmarking */

#include "emulator_core.hpp"
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <getopt.h>
#include <ctime>
#include <fstream>
#include <iostream>
#include <vector>
#include <unistd.h>

#include "ui.h"
#include "generator.h"

/* Per-frame canonical state fingerprints. */
#include "state_v3.hpp"

#include "uiplot_cram.h"

/* Version info */
#ifndef VERSION
#define VERSION "0.50"
#endif

/* Default number of frames to run */
#define DEFAULT_FRAMES 600

static int verbose_mode = 0;
static int quiet_mode = 0;

using namespace generator;

class HeadlessAudio : public IAudioBackend {
public:
  void output_samples(std::span<const uint16_t> left,
                      std::span<const uint16_t> right) override
  {
    // No-op for headless
  }
};

/* SOUND_SAMPLERATE is passed to C sources only (meson: language:['c']); provide
 * a C++ fallback so the WAV header is correct under the default build. */
#ifndef SOUND_SAMPLERATE
#define SOUND_SAMPLERATE 48000
#endif

/* CapturingAudio accumulates all rendered samples (interleaved L/R) so the
 * headless run can dump them to a WAV for deterministic A/B comparison of
 * audio-affecting changes (e.g. the GEMS sound-driver fixes). */
class CapturingAudio : public IAudioBackend {
public:
  void output_samples(std::span<const uint16_t> left,
                      std::span<const uint16_t> right) override
  {
    const size_t n = std::min(left.size(), right.size());
    for (size_t i = 0; i < n; ++i) {
      m_samples.push_back(left[i]);
      m_samples.push_back(right[i]);
    }
  }

  const std::vector<uint16_t> &samples() const
  {
    return m_samples;
  }

private:
  std::vector<uint16_t> m_samples;
};

/* FNV-1a 64-bit over the raw sample bytes — a cheap, non-crypto regression
 * fingerprint. Two runs of the same commit+ROM produce identical audio, so a
 * hash mismatch between commits means an audible change. */
static uint64_t fnv1a64(const uint8_t *data, size_t len)
{
  uint64_t h = 0xcbf29ce484222325ULL;
  for (size_t i = 0; i < len; ++i) {
    h ^= data[i];
    h *= 0x100000001b3ULL;
  }
  return h;
}

/* Incremental FNV-1a 64-bit for stream-style fingerprinting (video lines,
 * state blobs) where the data arrives in pieces. */
struct Fnv1a64 {
  uint64_t h = 0xcbf29ce484222325ULL;

  void update(const uint8_t *data, size_t len)
  {
    for (size_t i = 0; i < len; ++i) {
      h ^= data[i];
      h *= 0x100000001b3ULL;
    }
  }

  void update_u32(uint32_t v)
  {
    uint8_t b[4] = {(uint8_t)(v & 0xff), (uint8_t)((v >> 8) & 0xff),
                    (uint8_t)((v >> 16) & 0xff), (uint8_t)((v >> 24) & 0xff)};
    update(b, sizeof(b));
  }

  uint64_t value() const
  {
    return h;
  }
};

/* Write the captured samples to a canonical 44-byte-header stereo 16-bit WAV
 * and report the sample count, duration, and a checksum to stdout. Returns 0
 * on success, non-zero on file error. */
static int dump_audio_wav(const char *path,
                          const std::vector<uint16_t> &samples)
{
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    std::cerr << "Error: cannot open audio dump file: " << path << "\n";
    return 1;
  }

  const uint32_t data_bytes =
      static_cast<uint32_t>(samples.size() * sizeof(uint16_t));
  const uint16_t channels = 2;
  const uint16_t bits = 16;
  const uint32_t sample_rate = SOUND_SAMPLERATE;
  const uint16_t block_align = channels * (bits / 8);
  const uint32_t byte_rate = sample_rate * block_align;

  auto put16 = [&](uint16_t v) {
    out.put(static_cast<char>(v & 0xff));
    out.put(static_cast<char>((v >> 8) & 0xff));
  };
  auto put32 = [&](uint32_t v) {
    out.put(static_cast<char>(v & 0xff));
    out.put(static_cast<char>((v >> 8) & 0xff));
    out.put(static_cast<char>((v >> 16) & 0xff));
    out.put(static_cast<char>((v >> 24) & 0xff));
  };

  out.write("RIFF", 4);
  put32(36 + data_bytes);
  out.write("WAVE", 4);
  out.write("fmt ", 4);
  put32(16); /* PCM subchunk size */
  put16(1);  /* PCM format */
  put16(channels);
  put32(sample_rate);
  put32(byte_rate);
  put16(block_align);
  put16(bits);
  out.write("data", 4);
  put32(data_bytes);

  out.write(reinterpret_cast<const char *>(samples.data()),
            static_cast<std::streamsize>(data_bytes));

  if (!out) {
    std::cerr << "Error: failed writing audio dump: " << path << "\n";
    return 1;
  }

  const uint64_t hash =
      fnv1a64(reinterpret_cast<const uint8_t *>(samples.data()), data_bytes);
  printf("Audio dump: %s\n", path);
  printf("  samples: %zu (%.2f sec at %u Hz)\n", samples.size(),
         (double)(samples.size() / channels) / sample_rate, sample_rate);
  printf("  fnv1a64: %016llx\n", (unsigned long long)hash);
  return 0;
}

class HeadlessVideo : public IVideoBackend {
public:
  void render_line(int line, std::span<const uint8_t> pixels) override
  {
    // No-op
  }
  void present_field() override
  {
    // No-op
  }
};

/* CapturingVideo fingerprints the rendered video output. It hashes the raw
 * 8-bit paletted scanlines the core pushes through render_line — before any
 * backend palette conversion — so the fingerprint is backend-independent.
 * The hashed stream is, in emission order: line index (u32 LE) + width
 * bytes of pixels per line, then the field index (u32 LE) per
 * present_field, so a scanline shift also changes the hash. */
class CapturingVideo : public IVideoBackend {
public:
  void render_line(int line, std::span<const uint8_t> pixels) override
  {
    if (line < 0 || pixels.empty()) {
      return;
    }
    hash_line(line, pixels.data(), (unsigned int)pixels.size());
    if (m_dump && line < 240) {
      const unsigned int n = std::min((unsigned int)pixels.size(), 320u);
      for (unsigned int x = 0; x < n; x++) {
        m_dump_buf[(std::size_t)line * 320 + x] = pixels[x] & 0x3F;
      }
    }
  }

  void present_field() override
  {
    m_total.update_u32(m_frames);
    m_last_field = m_field.h;
    m_field = Fnv1a64{};
    if (m_dump && m_frames >= m_dump_start &&
        m_frames < m_dump_start + m_dump_count) {
      char path[512];
      std::snprintf(path, sizeof(path), "%s_%u.ppm", m_dump_prefix, m_frames);
      std::FILE *f = std::fopen(path, "wb");
      if (f) {
        /* RGB through the published CRAM snapshot, so the dump shows
         * what the game actually put on screen. */
        const uint16_t *cram = uiplot_cram_ptr();
        std::fprintf(f, "P6\n320 240\n255\n");
        for (std::size_t i = 0; i < 320u * 240; i++) {
          const uint16_t entry = cram[m_dump_buf[i] & 0x3F];
          const unsigned char rgb[3] = {
              (unsigned char)(((entry >> 1) & 7) * 255 / 7),
              (unsigned char)(((entry >> 5) & 7) * 255 / 7),
              (unsigned char)(((entry >> 9) & 7) * 255 / 7)};
          std::fwrite(rgb, 1, 3, f);
        }
        std::fclose(f);
      }
    }
    m_frames++;
  }

  void enable_frame_dump(const char *prefix, unsigned int start,
                         unsigned int count)
  {
    m_dump = true;
    m_dump_prefix = prefix;
    m_dump_start = start;
    m_dump_count = count;
  }

  unsigned int frames() const
  {
    return m_frames;
  }
  unsigned int lines() const
  {
    return m_lines;
  }
  uint64_t total_hash() const
  {
    return m_total.value();
  }
  bool m_dump = false;
  const char *m_dump_prefix = "";
  unsigned int m_dump_start = 0, m_dump_count = 0;
  std::array<uint8_t, 320u * 240> m_dump_buf{};

  uint64_t last_field_hash() const
  {
    return m_last_field;
  }

private:
  void hash_line(int line, const uint8_t *pixels, unsigned int width)
  {
    m_total.update_u32(static_cast<uint32_t>(line));
    m_total.update(pixels, width);
    m_field.update_u32(static_cast<uint32_t>(line));
    m_field.update(pixels, width);
    m_lines++;
  }

  Fnv1a64 m_total;
  Fnv1a64 m_field;
  uint64_t m_last_field = 0xcbf29ce484222325ULL;
  unsigned int m_frames = 0;
  unsigned int m_lines = 0;
};

/* Hash the emulator's save state after the run. The core's blob is fully
 * deterministic, so hashing it fingerprints the whole machine state at
 * once. Uses a temporary file because the save path is file-based. */
static int dump_state_hash_report(EmulatorCore &core)
{
  char tmpl[] = "/tmp/generator-headless-state-XXXXXX";
  const int fd = mkstemp(tmpl);
  if (fd < 0) {
    std::cerr << "Error: cannot create temporary state file\n";
    return 1;
  }
  close(fd);

  if (core.save_state(tmpl) != 0) {
    std::cerr << "Error: failed to save state for hashing\n";
    unlink(tmpl);
    return 1;
  }

  std::ifstream in(tmpl, std::ios::binary);
  if (!in) {
    std::cerr << "Error: cannot re-open temporary state file: " << tmpl << "\n";
    unlink(tmpl);
    return 1;
  }
  Fnv1a64 hash;
  char buf[8192];
  size_t total = 0;
  while (in.read(buf, sizeof(buf)) || in.gcount() > 0) {
    hash.update(reinterpret_cast<const uint8_t *>(buf),
                static_cast<size_t>(in.gcount()));
    total += static_cast<size_t>(in.gcount());
    if (!in)
      break;
  }
  unlink(tmpl);

  printf("State hash:\n");
  printf("  bytes: %zu\n", total);
  printf("  fnv1a64: %016llx\n", (unsigned long long)hash.value());
  return 0;
}

/* Canonical per-frame fingerprint: the Z80 RAM (WZ80 chunk of a state v3
 * blob), the set the reference oracle publishes too. */
static std::optional<uint64_t> z80_ram_fingerprint(EmulatorCore &core)
{
  char tmpl[] = "/tmp/generator-headless-frame-XXXXXX";
  const int fd = mkstemp(tmpl);
  if (fd < 0) {
    return std::nullopt;
  }
  close(fd);
  if (core.save_state(tmpl) != 0) {
    unlink(tmpl);
    return std::nullopt;
  }
  std::ifstream in(tmpl, std::ios::binary);
  std::vector<uint8_t> blob((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
  unlink(tmpl);
  auto chunks = generator::StateV3::deserialize(blob);
  if (!chunks) {
    return std::nullopt;
  }
  Fnv1a64 hash;
  for (const auto &chunk : *chunks) {
    if (chunk.id == generator::fourcc("WZ80")) {
      hash.update(chunk.data.data(), chunk.data.size());
      return hash.value();
    }
  }
  return std::nullopt;
}

class HeadlessLogger : public ILogger {
public:
  void log(LogLevel level, std::string_view message) override
  {
    switch (level) {
    case LogLevel::Debug3:
    case LogLevel::Debug2:
    case LogLevel::Debug1:
      return;
    case LogLevel::Verbose:
      if (!verbose_mode || quiet_mode)
        return;
      std::cout << "[VERBOSE] " << message << "\n";
      break;
    case LogLevel::User:
    case LogLevel::Normal:
      if (quiet_mode)
        return;
      std::cout << "[INFO] " << message << "\n";
      break;
    case LogLevel::Critical:
    default:
      std::cerr << "[ERROR] " << message << "\n";
      break;
    }
  }
};

static void print_usage(const char *progname)
{
  printf("Usage: %s [OPTIONS] <rom-file>\n", progname);
  printf("\n");
  printf("Run Genesis emulator in headless mode (no display, no audio).\n");
  printf("Useful for benchmarking, testing, and automated processing.\n");
  printf("\n");
  printf("Options:\n");
  printf("  -h, --help          Show this help message\n");
  printf("  -v, --version       Show version information\n");
  printf("  -f, --frames N      Run N frames (default: %d)\n", DEFAULT_FRAMES);
  printf("  -l, --load-state F  Load state from file before running\n");
  printf("  -s, --save-state F  Save state to file after running\n");
  printf(
      "  -D, --dump-audio F  Dump rendered audio to a 16-bit stereo WAV and\n");
  printf("                      print a checksum (deterministic; for A/B "
         "diffs)\n");
  printf("  -I, --hold-input B  Hold buttons for the whole run (u,d,l,r,"
         "start,a,b,c,\n");
  printf("                      x,y,z,mode; comma separated, e.g. "
         "--hold-input start)\n");
  printf("  -T, --tap-input B   Tap buttons 8 frames of every 40 so "
         "edge-detected\n");
  printf("                      menus advance (same button names)\n");
  printf("  --dump-video-hash   Fingerprint the rendered video (per-line VDP "
         "output)\n");
  printf("                      and print FNV-1a checksums after the run\n");
  printf("  --dump-state-hash   Save the final machine state to a temporary "
         "file and\n");
  printf("                      print an FNV-1a checksum of the blob\n");
  printf("  --hash-per-frame    Print a per-frame FNV-1a video fingerprint "
         "after each\n");
  printf("  --hash-state-per-frame  Print a per-frame FNV-1a of the Z80 "
         "RAM\n");
  printf("                          (the set the reference oracle "
         "publishes)\n");
  printf("                      frame (fine-grained first-divergence diffs "
         "between\n");
  printf("                      cores/builds; implies video capture)\n");
  printf("  -V, --verbose       Enable verbose output\n");
  printf("  -q, --quiet         Suppress all output except errors\n");
}

static void print_version(void)
{
  printf("Generator Headless Backend v%s\n", VERSION);
  printf("Emulation core: cycle-accurate\n");
  printf("Sega Genesis/Mega Drive Emulator\n");
  printf("See AUTHORS.md for copyright attribution.\n");
}

int main(int argc, char *argv[])
{
  const char *rom_file = nullptr;
  const char *load_state_file = nullptr;
  const char *save_state_file = nullptr;
  const char *dump_audio_file = nullptr;
  const char *hold_input = nullptr;
  const char *tap_input = nullptr;
  const char *dump_frames = nullptr;
  const char *zram_log = nullptr;
  int dump_video_hash = 0;
  int dump_state_hash = 0;
  int hash_per_frame = 0;
  int hash_state_per_frame = 0;
  unsigned int num_frames = DEFAULT_FRAMES;
  int opt;

  static struct option long_options[] = {
      {"help", no_argument, 0, 'h'},
      {"version", no_argument, 0, 'v'},
      {"frames", required_argument, 0, 'f'},
      {"verbose", no_argument, 0, 'V'},
      {"quiet", no_argument, 0, 'q'},
      {"load-state", required_argument, 0, 'l'},
      {"save-state", required_argument, 0, 's'},
      {"dump-audio", required_argument, 0, 'D'},
      {"hold-input", required_argument, 0, 'I'},
      {"tap-input", required_argument, 0, 'T'},
      {"dump-frames", required_argument, 0, 'F'},
      {"zram-log", required_argument, 0, 'Z'},
      {"dump-video-hash", no_argument, &dump_video_hash, 1},
      {"dump-state-hash", no_argument, &dump_state_hash, 1},
      {"hash-per-frame", no_argument, &hash_per_frame, 1},
      {"hash-state-per-frame", no_argument, &hash_state_per_frame, 1},
      {0, 0, 0, 0}};

  while ((opt = getopt_long(argc, argv, "hvf:Vql:s:D:I:T:F:Z:", long_options,
                            nullptr)) != -1) {
    switch (opt) {
    case 'h':
      print_usage(argv[0]);
      return 0;
    case 'v':
      print_version();
      return 0;
    case 'f':
      num_frames = (unsigned int)atoi(optarg);
      if (num_frames == 0) {
        std::cerr << "Error: Invalid frame count\n";
        return 1;
      }
      break;
    case 'l':
      load_state_file = optarg;
      break;
    case 's':
      save_state_file = optarg;
      break;
    case 'D':
      dump_audio_file = optarg;
      break;
    case 'I':
      hold_input = optarg;
      break;
    case 'T':
      tap_input = optarg;
      break;
    case 'F':
      dump_frames = optarg;
      break;
    case 'Z':
      zram_log = optarg;
      break;
    case 'V':
      verbose_mode = 1;
      break;
    case 'q':
      quiet_mode = 1;
      break;
    case 0:
      /* Long option that sets its own flag (--dump-video-hash,
       * --dump-state-hash); nothing further to do. */
      break;
    default:
      print_usage(argv[0]);
      return 1;
    }
  }

  if (optind >= argc) {
    std::cerr << "Error: No ROM file specified\n";
    print_usage(argv[0]);
    return 1;
  }
  rom_file = argv[optind];

  if (!quiet_mode) {
    print_version();
    printf("\nROM file: %s\nFrames to run: %u\n\n", rom_file, num_frames);
  }

  try {
    /* Build a capturing audio backend when --dump-audio was requested, so the
     * rendered samples are available for the post-run WAV dump; otherwise the
     * plain no-op headless backend. Likewise for --dump-video-hash. */
    CapturingAudio *capture = nullptr;
    CapturingVideo *capture_video = nullptr;
    std::unique_ptr<IAudioBackend> audio;
    if (dump_audio_file != nullptr) {
      auto cap = std::make_unique<CapturingAudio>();
      capture = cap.get();
      audio = std::move(cap);
    } else {
      audio = std::make_unique<HeadlessAudio>();
    }
    std::unique_ptr<IVideoBackend> video;
    if (dump_video_hash || hash_per_frame || dump_frames != nullptr) {
      auto cap = std::make_unique<CapturingVideo>();
      capture_video = cap.get();
      video = std::move(cap);
    } else {
      video = std::make_unique<HeadlessVideo>();
    }

    auto core = std::make_unique<EmulatorCore>(
        std::move(audio), std::move(video), std::make_shared<HeadlessLogger>());
    auto res = core->load_rom(rom_file);
    if (!res) {
      std::cerr << "Error: Failed to load ROM: " << res.error() << "\n";
      return 1;
    }

    if (!quiet_mode) {
      const t_cartinfo *info = core->rom_info();
      if (info != nullptr) {
        printf("Loaded: %s\n", info->name_overseas[0] ? info->name_overseas
                                                      : info->name_domestic);
        printf("Region: %s%s%s\n", info->flag_japan ? "J" : "",
               info->flag_usa ? "U" : "", info->flag_europe ? "E" : "");
      }
    }

    if (load_state_file != nullptr) {
      if (core->load_state(load_state_file) != 0) {
        std::cerr << "Error: Failed to load state from: " << load_state_file
                  << "\n";
        return 1;
      }
      if (!quiet_mode)
        printf("State loaded from: %s\n", load_state_file);
    }

    if (dump_frames != nullptr && capture_video != nullptr) {
      /* PREFIX:START:COUNT */
      char prefix[400];
      unsigned int start = 0, count = 0;
      if (std::sscanf(dump_frames, "%399[^:]:%u:%u", prefix, &start, &count) ==
          3) {
        capture_video->enable_frame_dump(strdup(prefix), start, count);
        if (!quiet_mode)
          printf("Dumping frames %u..%u to %s_*\n", start, start + count,
                 prefix);
      }
    }

    if (zram_log != nullptr) {
      core->debug_log_zram_to(zram_log);
      if (!quiet_mode)
        printf("Z80-RAM write log: %s\n", zram_log);
    }

    if (!quiet_mode)
      if (zram_log != nullptr) {
        core->debug_log_zram_to(zram_log);
        printf("Z80-RAM write log: %s\n", zram_log);
      }

    printf("\nRunning %u frames...\n", num_frames);

    clock_t start_time = clock();
    unsigned int frame = 0;

    /* Scripted input for automated runs, so audio/video comparisons can
     * reach gameplay: --hold-input keeps the buttons down from frame 0
     * (no press edges — games with edge detection never see these fire),
     * --tap-input presses them 8 frames of every 40 so menus advance.
     * Buttons: u,d,l,r,start,a,b,c,x,y,z,mode, comma separated. */
    unsigned int hold_up = 0, hold_down = 0, hold_left = 0, hold_right = 0;
    unsigned int hold_start = 0, hold_a = 0, hold_b = 0, hold_c = 0;
    unsigned int hold_x = 0, hold_y = 0, hold_z = 0, hold_mode = 0;
    unsigned int tap_up = 0, tap_down = 0, tap_left = 0, tap_right = 0;
    unsigned int tap_start = 0, tap_a = 0, tap_b = 0, tap_c = 0;
    unsigned int tap_x = 0, tap_y = 0, tap_z = 0, tap_mode = 0;
    for (int mode = 0; mode < 2; mode++) {
      const char *spec = mode == 0 ? hold_input : tap_input;
      if (spec == nullptr) {
        continue;
      }
      unsigned int *const dst[12] = {mode == 0 ? &hold_up : &tap_up,
                                     mode == 0 ? &hold_down : &tap_down,
                                     mode == 0 ? &hold_left : &tap_left,
                                     mode == 0 ? &hold_right : &tap_right,
                                     mode == 0 ? &hold_start : &tap_start,
                                     mode == 0 ? &hold_a : &tap_a,
                                     mode == 0 ? &hold_b : &tap_b,
                                     mode == 0 ? &hold_c : &tap_c,
                                     mode == 0 ? &hold_x : &tap_x,
                                     mode == 0 ? &hold_y : &tap_y,
                                     mode == 0 ? &hold_z : &tap_z,
                                     mode == 0 ? &hold_mode : &tap_mode};
      for (const char *p = spec; *p != '\0'; p++) {
        if (*p == ',' || *p == ' ' || *p == '+') {
          continue;
        }
        const char *q = p;
        while (*q != '\0' && *q != ',' && *q != ' ' && *q != '+') {
          q++;
        }
        const std::string name(p, (std::size_t)(q - p));
        int idx = -1;
        if (name == "u" || name == "up") {
          idx = 0;
        } else if (name == "d" || name == "down") {
          idx = 1;
        } else if (name == "l" || name == "left") {
          idx = 2;
        } else if (name == "r" || name == "right") {
          idx = 3;
        } else if (name == "start") {
          idx = 4;
        } else if (name == "a") {
          idx = 5;
        } else if (name == "b") {
          idx = 6;
        } else if (name == "c") {
          idx = 7;
        } else if (name == "x") {
          idx = 8;
        } else if (name == "y") {
          idx = 9;
        } else if (name == "z") {
          idx = 10;
        } else if (name == "mode") {
          idx = 11;
        } else {
          std::cerr << "Error: unknown input button '" << name << "'\n";
          return 1;
        }
        *dst[idx] = 1;
        if (*q == '\0') {
          break;
        }
        p = q;
      }
    }

    for (; frame < num_frames; frame++) {
      const unsigned int tap = (frame % 40) < 8 ? 1u : 0u;
      core->set_input(0, hold_up | (tap_up & tap), hold_down | (tap_down & tap),
                      hold_left | (tap_left & tap),
                      hold_right | (tap_right & tap),
                      hold_start | (tap_start & tap), hold_a | (tap_a & tap),
                      hold_b | (tap_b & tap), hold_c | (tap_c & tap),
                      hold_x | (tap_x & tap), hold_y | (tap_y & tap),
                      hold_z | (tap_z & tap), hold_mode | (tap_mode & tap));
      core->run_frame();

      if (gen_quit) { /* signal-safe shutdown flag */
        if (!quiet_mode)
          printf("Quit signal received at frame %u\n", frame);
        break;
      }

      if (hash_per_frame && capture_video != nullptr) {
        printf("frame %u fnv1a64 %016llx\n", frame,
               (unsigned long long)capture_video->last_field_hash());
      }
      if (hash_state_per_frame) {
        const auto fp = z80_ram_fingerprint(*core);
        if (fp) {
          printf("frame %u fnv1a64 %016llx\n", frame, (unsigned long long)*fp);
        }
      }

      if (verbose_mode && !quiet_mode && frame > 0 &&
          (frame % (core->framerate() * 10)) == 0) {
        printf("Frame %u / %u (%.1f%%)\n", frame, num_frames,
               100.0 * frame / num_frames);
      }
    }

    clock_t end_time = clock();
    double elapsed = (double)(end_time - start_time) / CLOCKS_PER_SEC;

    if (!quiet_mode) {
      printf("\nCompleted %u frames in %.2f seconds\n", frame, elapsed);
      printf("Average: %.2f frames/sec (%.2fx realtime at %uhz)\n",
             frame / elapsed, (frame / elapsed) / core->framerate(),
             core->framerate());
    }

    if (dump_audio_file != nullptr && capture != nullptr) {
      /* Audio capture is deterministic across runs of the same commit+ROM, so
       * the WAV + checksum support A/B comparison of audio-affecting changes.
       */
      if (!capture->samples().empty()) {
        if (dump_audio_wav(dump_audio_file, capture->samples()) != 0)
          return 1;
      } else if (!quiet_mode) {
        printf("Audio dump skipped: no samples captured (audio disabled?)\n");
      }
    }

    if (capture_video != nullptr) {
      printf("Video hash:\n");
      printf("  frames: %u, lines: %u\n", capture_video->frames(),
             capture_video->lines());
      printf("  fnv1a64 (all frames): %016llx\n",
             (unsigned long long)capture_video->total_hash());
      printf("  fnv1a64 (final frame): %016llx\n",
             (unsigned long long)capture_video->last_field_hash());
    }

    if (dump_state_hash) {
      if (dump_state_hash_report(*core) != 0)
        return 1;
    }

    if (save_state_file != nullptr) {
      if (core->save_state(save_state_file) != 0) {
        std::cerr << "Error: Failed to save state to: " << save_state_file
                  << "\n";
        return 1;
      }
      if (!quiet_mode)
        printf("State saved to: %s\n", save_state_file);
    }
  } catch (const std::exception &e) {
    std::cerr << "Fatal error: " << e.what() << "\n";
    return 1;
  }

  return 0;
}

/* UI interface stubs. This backend does its own driving in main(), so the
   entry points exist only to satisfy the ui.h contract. The per-scanline
   callbacks that used to be stubbed here are gone: the core emits through
   CapturingVideo/HeadlessVideo instead. */
int ui_init(int argc, char *argv[])
{
  return 0;
}
int ui_loop(void)
{
  return 0;
}
void ui_final(void)
{
}
[[noreturn]] void ui_err(const char *text, ...)
{
  va_list args;
  va_start(args, text);
  vfprintf(stderr, text, args);
  va_end(args);
  fputc('\n', stderr);
  exit(1);
}
