/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "uiplot_cram.h"

static const uint16_t s_default_cram[64] = {0};
static const uint8_t s_default_dirty[64] = {0};
static const uint16_t *s_cram = s_default_cram;
static const uint8_t *s_dirty = s_default_dirty;

void uiplot_set_cram(const uint16_t *cram, const uint8_t *dirty)
{
  s_cram = cram != nullptr ? cram : s_default_cram;
  s_dirty = dirty != nullptr ? dirty : s_default_dirty;
}

const uint16_t *uiplot_cram_ptr(void)
{
  return s_cram;
}

const uint8_t *uiplot_cram_dirty_ptr(void)
{
  return s_dirty;
}
