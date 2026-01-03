extern uint32 uiplot_palcache[192];

void uiplot_setshifts(int redshift, int greenshift, int blueshift);
void uiplot_setmasks(uint32 redmask, uint32 greenmask, uint32 bluemask);
void uiplot_checkpalcache(int flag);
void uiplot_convertdata16(uint8 *indata, uint16 *outdata, unsigned int pixels);
void uiplot_convertdata32(uint8 *indata, uint32 *outdata, unsigned int pixels);
void uiplot_render16_x1(uint16 *linedata, uint16 *olddata, uint8 *screen,
                        unsigned int pixels);
void uiplot_render32_x1(uint32 *linedata, uint32 *olddata, uint8 *screen,
                        unsigned int pixels);
void uiplot_render16_x2(uint16 *linedata, uint16 *olddata, uint8 *screen,
                        unsigned int linewidth, unsigned int pixels);
void uiplot_render32_x2(uint32 *linedata, uint32 *olddata, uint8 *screen,
                        unsigned int linewidth, unsigned int pixels);
void uiplot_render16_x2h(uint16 *linedata, uint16 *olddata, uint8 *screen,
                         unsigned int pixels);
void uiplot_render32_x2h(uint32 *linedata, uint32 *olddata, uint8 *screen,
                         unsigned int pixels);
void uiplot_irender16_weavefilter(uint16 *evendata, uint16 *odddata,
                                  uint8 *screen, unsigned int pixels);
void uiplot_irender32_weavefilter(uint32 *evendata, uint32 *odddata,
                                  uint8 *screen, unsigned int pixels);

/* Scale2x/EPX upscaling algorithms
   src_pitch and dst_pitch are in bytes (stride of source/dest buffers) */
void uiplot_scale2x_frame32(uint32 *srcdata, uint32 *dstdata,
                            unsigned int src_width, unsigned int src_height,
                            unsigned int src_pitch, unsigned int dst_pitch);
void uiplot_scale3x_frame32(uint32 *srcdata, uint32 *dstdata,
                            unsigned int src_width, unsigned int src_height,
                            unsigned int src_pitch, unsigned int dst_pitch);
void uiplot_scale4x_frame32(uint32 *srcdata, uint32 *dstdata, uint32 *temp,
                            unsigned int src_width, unsigned int src_height,
                            unsigned int src_pitch, unsigned int dst_pitch);

/* xBRZ high-quality upscaling algorithms */
void uiplot_xbrz_frame32(int factor, uint32 *srcdata, uint32 *dstdata,
                         unsigned int src_width, unsigned int src_height);
