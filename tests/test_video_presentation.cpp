/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "video_presentation.hpp"
#include "gtkopts.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <vector>

using namespace generator::gtkui;

namespace {

class OptionsScope {
public:
  OptionsScope() : previous_(gtkopts_conf)
  {
    gtkopts_conf = nullptr;
  }
  ~OptionsScope()
  {
    clear();
    gtkopts_conf = previous_;
  }
  static void clear()
  {
    while (gtkopts_conf) {
      auto *next = gtkopts_conf->next;
      std::free(gtkopts_conf->key);
      std::free(gtkopts_conf->value);
      std::free(gtkopts_conf);
      gtkopts_conf = next;
    }
  }

private:
  t_conf *previous_;
};

std::span<const uint8_t> bytes(const std::vector<uint32_t> &pixels)
{
  return {reinterpret_cast<const uint8_t *>(pixels.data()), pixels.size() * 4};
}

}  // namespace

TEST_CASE("video layout corrects H32 and H40 fields to the same TV aspect",
          "[video]")
{
  VideoSettings settings;
  for (int width : {256, 320}) {
    for (int height : {224, 240}) {
      auto rect = settings.bounds(1000, 600, width, height);
      CHECK(rect.width == Catch::Approx(800));
      CHECK(rect.height == Catch::Approx(600));
      CHECK(rect.x == Catch::Approx(100));
      CHECK(rect.y == Catch::Approx(0));
    }
  }
  settings.aspect = AspectRatio::PIXELS;
  auto rect = settings.bounds(1000, 600, 320, 224);
  CHECK(rect.width / rect.height == Catch::Approx(320.0 / 224));
  settings.aspect = AspectRatio::STRETCH;
  rect = settings.bounds(1000, 600, 256, 240);
  CHECK(rect.width == 1000);
  CHECK(rect.height == 600);
}

TEST_CASE("integer scaling uses native rows and keeps small windows usable",
          "[video]")
{
  VideoSettings settings;
  settings.scaling = ScalingMode::INTEGER;
  auto rect = settings.bounds(1000, 700, 320, 224);
  CHECK(rect.height == 672);
  CHECK(rect.width == 896);
  CHECK(rect.x == 52);
  CHECK(rect.y == 14);
  settings.aspect = AspectRatio::PIXELS;
  rect = settings.bounds(1000, 700, 256, 224);
  CHECK(rect.width == 768);
  CHECK(rect.height == 672);
  rect = settings.bounds(100, 80, 320, 224);
  CHECK(rect.width == Catch::Approx(100));
  CHECK(rect.height == Catch::Approx(70));
  CHECK(rect.y == 5);
  CHECK(settings.bounds(0, 600, 320, 224).width == 0);
  CHECK(settings.bounds(800, 600, 0, 224).width == 0);
}

TEST_CASE(
    "all image filters respect padded source rows and retain opaque colours",
    "[video]")
{
  VideoFrameScaler scaler;
  constexpr uint32_t colour = 0xff3973b5;
  for (int width : {256, 320}) {
    for (int height : {224, 240}) {
      int stride = width + 7;
      std::vector<uint32_t> source(static_cast<size_t>(stride) * height,
                                   0xffee0000);
      for (int y = 0; y < height; ++y)
        std::fill_n(source.begin() + y * stride, width, colour);
      for (size_t index = 0; index < scaler_options.size(); ++index) {
        constexpr std::array factors{1, 2, 3, 4, 2, 3, 4, 5, 6};
        INFO(scaler_options[index].value << " " << width << "x" << height);
        auto frame = scaler.apply(bytes(source), width, height, stride * 4,
                                  static_cast<Scaler>(index));
        REQUIRE_FALSE(frame.pixels.empty());
        CHECK(frame.width == width * factors[index]);
        CHECK(frame.height == height * factors[index]);
        CHECK(std::ranges::all_of(
            frame.pixels, [](uint32_t pixel) { return pixel == colour; }));
      }
    }
  }
}

TEST_CASE("Scale2x processes edges rather than just duplicating pixels",
          "[video]")
{
  VideoFrameScaler scaler;
  std::vector<uint32_t> source{0xff000000, 0xffffffff, 0xff000000,
                               0xffffffff, 0xff000000, 0xff000000,
                               0xff000000, 0xff000000, 0xff000000};
  auto frame = scaler.apply(bytes(source), 3, 3, 12, Scaler::SCALE2X);
  REQUIRE(frame.width == 6);
  CHECK(frame.pixels[2 * 6 + 2] == 0xffffffff);
  CHECK(frame.pixels[3 * 6 + 3] == 0xff000000);
  CHECK(source[4] == 0xff000000);
  CHECK(scaler.apply(bytes(source).first(8), 3, 3, 12, Scaler::XBRZ6X)
            .pixels.empty());
  CHECK(scaler.apply(bytes(source), 3, 3, 8, Scaler::NONE).pixels.empty());
  CHECK(
      scaler.apply(bytes(source), 321, 240, 1284, Scaler::NONE).pixels.empty());
}

TEST_CASE("video options persist and invalid saved values fall back safely",
          "[video]")
{
  OptionsScope scope;
  CHECK(VideoSettings::load() == VideoSettings{});
  VideoSettings custom{AspectRatio::PIXELS, ScalingMode::INTEGER,
                       Sampling::LINEAR, Scaler::XBRZ6X};
  REQUIRE(custom.save());
  CHECK(VideoSettings::load() == custom);
  auto path =
      std::filesystem::current_path() /
      ("video-options-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()) +
       ".conf");
  REQUIRE(gtkopts_save(path.c_str()) == 0);
  OptionsScope::clear();
  REQUIRE(gtkopts_load(path.c_str()) == 0);
  std::filesystem::remove(path);
  CHECK(VideoSettings::load() == custom);
  REQUIRE(gtkopts_setvalue("scaler", "unknown-filter") == 0);
  REQUIRE(gtkopts_setvalue("video_aspect", "invalid") == 0);
  auto restored = VideoSettings::load();
  CHECK(restored.scaler == Scaler::NONE);
  CHECK(restored.aspect == AspectRatio::TV);
}
