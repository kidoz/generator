# GNM sound log format

This page specifies the GNM sound log format: the file header, the stream
encoding, and how to decode it. For why the format exists and how Generator
writes it, see [Why Generator has its own GNM format](../explanation/gnm-format-design.md).

Unlike GYM, GNM records PCM sample data with sub-field timing accuracy.

## Header

| Offset | Value                                  |
| ------ | -------------------------------------- |
| 0      | `'G'`                                  |
| 1      | `'N'`                                  |
| 2      | `'M'`                                  |
| 3      | Fields per second (e.g. 50 or 60)      |

## Stream decoding

```c
interval = 0;
while (!EOF) {
  data = readbyte(); // readbyte() reads the next byte from the file
  if ((data & 0xF0) == 0) {
    switch (data) {
      case 0: /* start of field marker (1/50th or 1/60th second elapsed) */
              sampsperfield = (readbyte() << 8) + readbyte();
      case 1: /* FM port 0 */
              ymport = 0; ymreg = readbyte(); ymdata = readbyte();
      case 2: /* FM port 1 */
              ymport = 1; ymreg = readbyte(); ymdata = readbyte();
      case 3: /* PSG */
              write readbyte() to psg port;
      case 4: /* escaped sample data and mark of interval */
              ymport = 0; ymreg = 0x2A; ymdata = readbyte(); interval = 1;
      case 5: /* no sample data this interval */
              interval = 1; /* no change of register 0x2A */
    }
  } else {
    /* unescaped sample data and mark of interval */
    ymport = 0; ymreg = 0x2A; ymdata = data; interval = 1;
  }
  if (interval) {
    interval = 0;
    update sound;
  }
}
```

## Encoding

Each field starts with a `$0` byte marker followed by two bytes that indicate
the number of intervals for this field.

Each interval is marked by a sample. The samples are placed directly in the
stream except for byte values `$0` to `$f`, in which case they are escaped with
a `$4` byte. If there was no sample written in the interval period then a `$5`
byte is inserted.
