// ****************************************************************************
// * xBRZ configuration header                                                *
// * Part of the xBRZ project (GPL-3.0 License)                              *
// ****************************************************************************

#ifndef XBRZ_CONFIG_H_82574808657803287
#define XBRZ_CONFIG_H_82574808657803287

namespace xbrz {
struct ScalerCfg {
  // These are the default tuning parameters for the xBRZ algorithm
  // Adjust these to fine-tune edge detection sensitivity

  double luminanceWeight =
      1.0;  // Weight for luminance vs chrominance in color distance
  double equalColorTolerance =
      30.0;  // Tolerance for considering colors equal (0-255 scale)
  double dominantDirectionThreshold =
      3.6;  // Threshold for dominant blending direction
  double steepDirectionThreshold =
      2.2;  // Threshold for steep gradient detection
  double centerDirectionBias =
      4.0;  // Bias towards center direction in gradient analysis

  // Default constructor with sensible defaults for pixel art
  ScalerCfg() = default;

  ScalerCfg(double lumWeight, double eqColorTol, double domDirThresh = 3.6,
            double steepDirThresh = 2.2, double centerDirBias = 4.0)
      : luminanceWeight(lumWeight), equalColorTolerance(eqColorTol),
        dominantDirectionThreshold(domDirThresh),
        steepDirectionThreshold(steepDirThresh),
        centerDirectionBias(centerDirBias)
  {
  }
};
}  // namespace xbrz

#endif
