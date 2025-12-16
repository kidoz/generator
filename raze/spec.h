struct RomModule {
  const char *name;
  int address;
  int size;
};

int readroms(UBYTE *, const char *, struct RomModule *);
