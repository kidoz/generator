/* xBRZ C wrapper implementation - bridges C and C++ code */

#include "xbrz_wrapper.h"
#include "xbrz.h"

extern "C" {

void xbrz_scale(int factor, const uint32_t *src, uint32_t *trg, int src_width,
                int src_height)
{
  if (factor < 2 || factor > xbrz::SCALE_FACTOR_MAX) {
    return;  // Invalid scale factor
  }

  // Use default configuration optimized for pixel art
  xbrz::ScalerCfg cfg;

  // RGB format (no alpha channel)
  xbrz::scale(static_cast<size_t>(factor), src, trg, src_width, src_height,
              xbrz::ColorFormat::RGB, cfg);
}

void xbrz_scale_custom(int factor, const uint32_t *src, uint32_t *trg,
                       int src_width, int src_height, double luminance_weight,
                       double equal_color_tolerance)
{
  if (factor < 2 || factor > xbrz::SCALE_FACTOR_MAX) {
    return;  // Invalid scale factor
  }

  // Custom configuration
  xbrz::ScalerCfg cfg(luminance_weight, equal_color_tolerance);

  xbrz::scale(static_cast<size_t>(factor), src, trg, src_width, src_height,
              xbrz::ColorFormat::RGB, cfg);
}

}  // extern "C"
