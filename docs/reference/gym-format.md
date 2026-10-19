# GYM sound log format

This page specifies the Genecyst GYM sound log format that Generator supports
for compatibility.

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

GYM has no defined way to carry PCM sample data, and Generator does not write
any into it: with only 1/60th-of-a-second accuracy, a player cannot know when
to play the samples. For the reasoning and for Generator's alternative, see
[Why Generator has its own GNM format](../explanation/gnm-format-design.md).
