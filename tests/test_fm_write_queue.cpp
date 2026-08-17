// Unit tests for the timestamped YM2612 write queue
// (src/audio/ym2612/fm_write_queue.cpp).
//
// The queue preserves push order (load-bearing: the YM2612 has addr/data
// latch pairs) and drains entries only up to a position limit, letting the
// mixer split a scanline's render window at each write's sample position.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

#include "fm_write_queue.hpp"

namespace {

uint8_t pop_port;
uint8_t pop_val;

bool pop(uint16_t limit)
{
  return fmq_pop(limit, &pop_port, &pop_val) != 0;
}

}  // namespace

TEST_CASE("fmq pop returns entries in strict push order", "[fmq]")
{
  fmq_reset();
  fmq_push(100, 1, 0xAA);
  fmq_push(100, 3, 0xBB);
  fmq_push(200, 1, 0xCC);

  REQUIRE(pop(FMQ_FRAC_ONE));
  REQUIRE(pop_port == 1);
  REQUIRE(pop_val == 0xAA);
  REQUIRE(pop(FMQ_FRAC_ONE));
  REQUIRE(pop_port == 3);
  REQUIRE(pop_val == 0xBB);
  REQUIRE(pop(FMQ_FRAC_ONE));
  REQUIRE(pop_port == 1);
  REQUIRE(pop_val == 0xCC);
  REQUIRE_FALSE(pop(FMQ_FRAC_ONE));  // empty
}

TEST_CASE("fmq pop respects the position limit", "[fmq]")
{
  fmq_reset();
  fmq_push(1000, 1, 0x11);
  fmq_push(3000, 2, 0x22);

  // Limit below the first entry: nothing pops.
  REQUIRE_FALSE(pop(999));

  // Limit at/below the first entry only.
  REQUIRE(pop(1000));
  REQUIRE(pop_port == 1);
  REQUIRE(pop_val == 0x11);

  // Second entry still beyond a smaller limit.
  REQUIRE_FALSE(pop(2000));

  // Now it fits.
  REQUIRE(pop(3000));
  REQUIRE(pop_port == 2);
  REQUIRE(pop_val == 0x22);
}

TEST_CASE("fmq push clamps out-of-range positions", "[fmq]")
{
  fmq_reset();
  fmq_push(FMQ_FRAC_ONE, 1, 0x01);      // == FRAC_ONE -> clamps to last slot
  fmq_push(FMQ_FRAC_ONE + 5, 2, 0x02);  // beyond -> clamps

  REQUIRE(fmq_peek_pos() == FMQ_FRAC_ONE - 1);
  // Clamped entries are still drainable at the max limit.
  REQUIRE(pop(FMQ_FRAC_ONE));
  REQUIRE(pop(FMQ_FRAC_ONE));
  REQUIRE_FALSE(pop(FMQ_FRAC_ONE));
}

TEST_CASE("fmq peek reports the oldest position without popping", "[fmq]")
{
  fmq_reset();
  REQUIRE(fmq_peek_pos() == FMQ_FRAC_ONE);  // empty sentinel

  fmq_push(1500, 1, 0x42);
  fmq_push(200, 2, 0x43);  // newer entry must not affect the peek
  REQUIRE(fmq_peek_pos() == 1500);

  REQUIRE(pop(FMQ_FRAC_ONE));
  REQUIRE(fmq_peek_pos() == 200);
}

TEST_CASE("fmq reset empties the queue and clears overflow", "[fmq]")
{
  fmq_reset();
  for (int i = 0; i < 500; ++i)  // force overflow at capacity 256
    fmq_push(static_cast<uint16_t>(i), 1, static_cast<uint8_t>(i));
  REQUIRE(fmq_overflowed());

  fmq_reset();
  REQUIRE_FALSE(fmq_overflowed());
  REQUIRE(fmq_peek_pos() == FMQ_FRAC_ONE);
  REQUIRE_FALSE(pop(FMQ_FRAC_ONE));
}

TEST_CASE("fmq overflow drops pushes but preserves earlier order", "[fmq]")
{
  fmq_reset();
  for (int i = 0; i < 256; ++i)  // fill exactly to capacity
    fmq_push(static_cast<uint16_t>(i * 4), 1, static_cast<uint8_t>(i));
  REQUIRE_FALSE(fmq_overflowed());

  fmq_push(10, 2, 0xFF);  // overflow -> dropped
  REQUIRE(fmq_overflowed());

  // The 256 original entries still drain in order with their values.
  for (int i = 0; i < 256; ++i) {
    REQUIRE(pop(FMQ_FRAC_ONE));
    REQUIRE(pop_val == static_cast<uint8_t>(i));
  }
  REQUIRE_FALSE(pop(FMQ_FRAC_ONE));
}

TEST_CASE("fmq wraps the ring across capacity without reordering", "[fmq]")
{
  fmq_reset();
  // Fill and drain in slices so the ring index wraps several times.
  uint8_t seq = 0;
  for (int round = 0; round < 8; ++round) {
    for (int i = 0; i < 100; ++i) {
      fmq_push(static_cast<uint16_t>(i), 1, seq);
      ++seq;
    }
    for (int i = 0; i < 100; ++i) {
      REQUIRE(pop(FMQ_FRAC_ONE));
      REQUIRE(pop_val == static_cast<uint8_t>(seq - 100 + i));
    }
  }
  REQUIRE_FALSE(pop(FMQ_FRAC_ONE));
  REQUIRE_FALSE(fmq_overflowed());
}
