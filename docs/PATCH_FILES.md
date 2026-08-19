# Patch Files

Generator supports Genecyst patch files. The format is assumed to be:

```text
aabbcc:ddee<space><space><space><space><description>\r\n
```

- `aabbcc` — 24-bit hex address to write to
- `ddee` — 16-bit data to write
- Four spaces separate this information from the description

Each line is DOS-style ended with a CR, LF. Generator will read in Unix LF-only
line endings, but will always write in DOS format to maintain compatibility.
