#ifndef GENERATOR_VDP_H
#define GENERATOR_VDP_H

typedef enum {
  cd_vram_fetch,
  cd_vram_store,
  cd_2,
  cd_cram_store,
  cd_vsram_fetch,
  cd_vsram_store,
  cd_6,
  cd_7,
  cd_cram_fetch
} t_code;


void vdp_reset(void);
int vdp_init(void);
uint16 vdp_status(void);
void vdp_storectrl(uint16 data);
void vdp_storedata(uint16 data);
uint16 vdp_fetchdata(void);
void vdp_renderline(unsigned int line, uint8 *linedata, unsigned int odd);
void vdp_renderline_interlace2(unsigned int line, uint8 *linedata);
void vdp_showregs(void);
void vdp_describe(void);
void vdp_spritelist(void);
void vdp_endfield(void);
void vdp_renderframe(uint8 *framedata, unsigned int lineoffset);
void vdp_setupvideo(void);
uint8 vdp_gethpos(void);

void vdp_fifo_drain(int count);

#define LEN_CRAM 128
#define LEN_VSRAM 80
#define LEN_VRAM 64 * 1024

/* C11: Verify VDP memory sizes at compile time */
#ifndef GENERATOR_BUILD_TOOL
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
static_assert(LEN_CRAM == 128, "Genesis CRAM must be 128 bytes");
static_assert(LEN_VSRAM == 80, "Genesis VSRAM must be 80 bytes");
static_assert(LEN_VRAM == 65536, "Genesis VRAM must be 64KB");
#endif
#endif

/* an estimate of the total cell width including HBLANK, for calculations */
#define TOTAL_CELLWIDTH 64


#endif /* GENERATOR_VDP_H */
