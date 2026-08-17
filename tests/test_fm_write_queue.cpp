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

bool pop(generator::FmWriteQueue &queue, uint16_t limit)
{
  return queue.pop(limit, &pop_port, &pop_val);
}

}  // namespace

TEST_CASE("fmq pop returns entries in strict push order", "[fmq]")
{
  generator::FmWriteQueue queue;
  queue.push(100, 1, 0xAA);
  queue.push(100, 3, 0xBB);
  queue.push(200, 1, 0xCC);

  REQUIRE(pop(queue, FMQ_FRAC_ONE));
  REQUIRE(pop_port == 1);
  REQUIRE(pop_val == 0xAA);
  REQUIRE(pop(queue, FMQ_FRAC_ONE));
  REQUIRE(pop_port == 3);
  REQUIRE(pop_val == 0xBB);
  REQUIRE(pop(queue, FMQ_FRAC_ONE));
  REQUIRE(pop_port == 1);
  REQUIRE(pop_val == 0xCC);
  REQUIRE_FALSE(pop(queue, FMQ_FRAC_ONE));  // empty
}

TEST_CASE("fmq pop respects the position limit", "[fmq]")
{
  generator::FmWriteQueue queue;
  queue.push(1000, 1, 0x11);
  queue.push(3000, 2, 0x22);

  // Limit below the first entry: nothing pops.
  REQUIRE_FALSE(pop(queue, 999));

  // Limit at/below the first entry only.
  REQUIRE(pop(queue, 1000));
  REQUIRE(pop_port == 1);
  REQUIRE(pop_val == 0x11);

  // Second entry still beyond a smaller limit.
  REQUIRE_FALSE(pop(queue, 2000));

  // Now it fits.
  REQUIRE(pop(queue, 3000));
  REQUIRE(pop_port == 2);
  REQUIRE(pop_val == 0x22);
}

TEST_CASE("fmq push clamps out-of-range positions", "[fmq]")
{
  generator::FmWriteQueue queue;
  queue.push(FMQ_FRAC_ONE, 1, 0x01);      // == FRAC_ONE -> clamps to last slot
  queue.push(FMQ_FRAC_ONE + 5, 2, 0x02);  // beyond -> clamps

  REQUIRE(queue.peek_pos() == FMQ_FRAC_ONE - 1);
  // Clamped entries are still drainable at the max limit.
  REQUIRE(pop(queue, FMQ_FRAC_ONE));
  REQUIRE(pop(queue, FMQ_FRAC_ONE));
  REQUIRE_FALSE(pop(queue, FMQ_FRAC_ONE));
}

TEST_CASE("fmq peek reports the oldest position without popping", "[fmq]")
{
  generator::FmWriteQueue queue;
  REQUIRE(queue.peek_pos() == FMQ_FRAC_ONE);  // empty sentinel

  queue.push(1500, 1, 0x42);
  queue.push(200, 2, 0x43);  // newer entry must not affect the peek
  REQUIRE(queue.peek_pos() == 1500);

  REQUIRE(pop(queue, FMQ_FRAC_ONE));
  REQUIRE(queue.peek_pos() == 200);
}

TEST_CASE("fmq reset empties the queue and clears overflow", "[fmq]")
{
  generator::FmWriteQueue queue;
  for (int i = 0; i < 500; ++i)  // force overflow at capacity 256
    queue.push(static_cast<uint16_t>(i), 1, static_cast<uint8_t>(i));
  REQUIRE(queue.overflowed());

  queue.reset();
  REQUIRE_FALSE(queue.overflowed());
  REQUIRE(queue.peek_pos() == FMQ_FRAC_ONE);
  REQUIRE_FALSE(pop(queue, FMQ_FRAC_ONE));
}

TEST_CASE("fmq overflow drops pushes but preserves earlier order", "[fmq]")
{
  generator::FmWriteQueue queue;
  for (int i = 0; i < 256; ++i)  // fill exactly to capacity
    queue.push(static_cast<uint16_t>(i * 4), 1, static_cast<uint8_t>(i));
  REQUIRE_FALSE(queue.overflowed());

  queue.push(10, 2, 0xFF);  // overflow -> dropped
  REQUIRE(queue.overflowed());

  // The 256 original entries still drain in order with their values.
  for (int i = 0; i < 256; ++i) {
    REQUIRE(pop(queue, FMQ_FRAC_ONE));
    REQUIRE(pop_val == static_cast<uint8_t>(i));
  }
  REQUIRE_FALSE(pop(queue, FMQ_FRAC_ONE));
}

TEST_CASE("fmq wraps the ring across capacity without reordering", "[fmq]")
{
  generator::FmWriteQueue queue;
  // Fill and drain in slices so the ring index wraps several times.
  uint8_t seq = 0;
  for (int round = 0; round < 8; ++round) {
    for (int i = 0; i < 100; ++i) {
      queue.push(static_cast<uint16_t>(i), 1, seq);
      ++seq;
    }
    for (int i = 0; i < 100; ++i) {
      REQUIRE(pop(queue, FMQ_FRAC_ONE));
      REQUIRE(pop_val == static_cast<uint8_t>(seq - 100 + i));
    }
  }
  REQUIRE_FALSE(pop(queue, FMQ_FRAC_ONE));
  REQUIRE_FALSE(queue.overflowed());
}

TEST_CASE("FM write queue instances are independent", "[fmq]")
{
  generator::FmWriteQueue first;
  generator::FmWriteQueue second;

  first.push(100, 1, 0xAA);

  REQUIRE(first.peek_pos() == 100);
  REQUIRE(second.peek_pos() == FMQ_FRAC_ONE);
  REQUIRE_FALSE(pop(second, FMQ_FRAC_ONE));
}
