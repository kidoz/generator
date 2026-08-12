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

extern "C" {
#include "ui.h"
}

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
  printf("  -V, --verbose       Enable verbose output\n");
  printf("  -q, --quiet         Suppress all output except errors\n");
}

static void print_version(void)
{
  printf("Generator Headless Backend v%s\n", VERSION);
  printf("Sega Genesis/Mega Drive Emulator\n");
  printf("See AUTHORS.md for copyright attribution.\n");
}

int main(int argc, char *argv[])
{
  const char *rom_file = nullptr;
  const char *load_state_file = nullptr;
  const char *save_state_file = nullptr;
  const char *dump_audio_file = nullptr;
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
      {0, 0, 0, 0}};

  while ((opt = getopt_long(argc, argv, "hvf:Vql:s:D:", long_options,
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
    case 'V':
      verbose_mode = 1;
      break;
    case 'q':
      quiet_mode = 1;
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
     * plain no-op headless backend. */
    CapturingAudio *capture = nullptr;
    std::unique_ptr<IAudioBackend> audio;
    if (dump_audio_file != nullptr) {
      auto cap = std::make_unique<CapturingAudio>();
      capture = cap.get();
      audio = std::move(cap);
    } else {
      audio = std::make_unique<HeadlessAudio>();
    }

    auto core = std::make_unique<EmulatorCore>(
        std::move(audio), std::make_unique<HeadlessVideo>(),
        std::make_shared<HeadlessLogger>());

    auto res = core->load_rom(rom_file);
    if (!res) {
      std::cerr << "Error: Failed to load ROM: " << res.error() << "\n";
      return 1;
    }

    if (!quiet_mode) {
      const gen_cartinfo_t *info = gen_core_get_rom_info(core->get_context());
      if (info != nullptr) {
        printf("Loaded: %s\n", info->name_overseas[0] ? info->name_overseas
                                                      : info->name_domestic);
        printf("Region: %s%s%s\n", info->flag_japan ? "J" : "",
               info->flag_usa ? "U" : "", info->flag_europe ? "E" : "");
      }
    }

    if (load_state_file != nullptr) {
      if (gen_core_load_state(core->get_context(), load_state_file) != 0) {
        std::cerr << "Error: Failed to load state from: " << load_state_file
                  << "\n";
        return 1;
      }
      if (!quiet_mode)
        printf("State loaded from: %s\n", load_state_file);
    }

    if (!quiet_mode)
      printf("\nRunning %u frames...\n", num_frames);

    clock_t start_time = clock();
    unsigned int frame = 0;

    for (; frame < num_frames; frame++) {
      core->run_frame();

      if (core->get_context()->quit) {
        if (!quiet_mode)
          printf("Quit signal received at frame %u\n", frame);
        break;
      }

      if (verbose_mode && !quiet_mode && frame > 0 &&
          (frame % (gen_core_get_framerate(core->get_context()) * 10)) == 0) {
        printf("Frame %u / %u (%.1f%%)\n", frame, num_frames,
               100.0 * frame / num_frames);
      }
    }

    clock_t end_time = clock();
    double elapsed = (double)(end_time - start_time) / CLOCKS_PER_SEC;

    if (!quiet_mode) {
      printf("\nCompleted %u frames in %.2f seconds\n", frame, elapsed);
      printf("Average: %.2f frames/sec (%.2fx realtime at %uhz)\n",
             frame / elapsed,
             (frame / elapsed) / gen_core_get_framerate(core->get_context()),
             gen_core_get_framerate(core->get_context()));
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

    if (save_state_file != nullptr) {
      if (gen_core_save_state(core->get_context(), save_state_file) != 0) {
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

/* UI interface stubs required by existing code. */
int ui_init(int argc, char *argv[])
{
  return 0;
}
int ui_loop(void)
{
  return 0;
}
void ui_line(int line)
{
}
void ui_endfield(void)
{
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
void ui_musiclog(uint8_t *data, unsigned int length)
{
}