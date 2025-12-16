#include <stdio.h>
#include "raze.h"
#include "spec.h"

FILE *F;

int readroms(UBYTE *ROM, const char *basename, struct RomModule *romp)
{
  FILE *F;
  char name[24];


  while (romp->name) {
    sprintf(name, "%s/%s", basename, romp->name);

    if (!(F = fopen(name, "rb"))) {
      printf("Unable to open file %s\n", name);
      return -1;
    }

    if (fread(ROM + romp->address, 1, romp->size, F) != romp->size) {
      printf("Unable to read file %s\n", name);
      fclose(F);
      return -1;
    }
    fclose(F);

    romp++;
  }

  return 0;
}
