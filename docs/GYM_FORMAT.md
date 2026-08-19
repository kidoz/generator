# GYM Log Format

The Genecyst GYM sound log format, supported by Generator for compatibility.

## Stream decoding

```c
while (!EOF)
  switch (readbyte()) // readbyte() reads the next byte from the file
  {
    case 0: 1/60th of a second has elapsed;
    case 1: ymport = 0; ymreg = readbyte(); ymdata = readbyte();
    case 2: ymport = 1; ymreg = readbyte(); ymdata = readbyte();
    case 3: write readbyte() to psg port;
  }
```

## Sample data

Generator does not put sample data into GYM logs, because it's impossible for a
player to know when to play the samples with only the accuracy of 1/60th of a
second. See the [GNM log format](GNM_FORMAT.md) instead.
