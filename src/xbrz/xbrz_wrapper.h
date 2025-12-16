/* xBRZ C wrapper header - allows C code to call C++ xBRZ functions */

#ifndef XBRZ_WRAPPER_H
#define XBRZ_WRAPPER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* C-compatible wrapper functions for xBRZ scaling */

/**
 * Scale an image using xBRZ algorithm
 *
 * @param factor Scale factor (2-6)
 * @param src Source image data (32-bit ARGB pixels)
 * @param trg Target image buffer (must be pre-allocated: src_width * factor ×
 * src_height * factor)
 * @param src_width Source image width in pixels
 * @param src_height Source image height in pixels
 *
 * Note: Assumes RGB color format (upper 8 bits unused)
 */
void xbrz_scale(int factor, const uint32_t *src, uint32_t *trg, int src_width,
                int src_height);

/**
 * Scale with custom configuration
 *
 * @param factor Scale factor (2-6)
 * @param src Source image data
 * @param trg Target image buffer
 * @param src_width Source width
 * @param src_height Source height
 * @param luminance_weight Weight for luminance vs chrominance (default: 1.0)
 * @param equal_color_tolerance Tolerance for color equality (default: 30.0)
 */
void xbrz_scale_custom(int factor, const uint32_t *src, uint32_t *trg,
                       int src_width, int src_height, double luminance_weight,
                       double equal_color_tolerance);

#ifdef __cplusplus
}
#endif

#endif /* XBRZ_WRAPPER_H */
