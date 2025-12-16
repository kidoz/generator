int soundp_start(void);
void soundp_stop(void);
void soundp_pause(void);
void soundp_resume(void);
int soundp_samplesbuffered(void);
void soundp_output(uint16 *left, uint16 *right, unsigned int samples);
