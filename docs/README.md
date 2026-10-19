# Generator documentation

This documentation follows the [Diátaxis](https://diataxis.fr/) framework.
Every page belongs to exactly one of four types, so you can go straight to
the kind of help you need.

|                 | Learning                                                   | Working                                                 |
| --------------- | ---------------------------------------------------------- | ------------------------------------------------------- |
| **Practical**   | [Tutorials](#tutorials): lessons that take you by the hand | [How-to guides](#how-to-guides): steps for a real task  |
| **Theoretical** | [Explanation](#explanation): background and reasoning      | [Reference](#reference): facts about formats and output |

## Tutorials

Learning-oriented lessons. There are none yet; start with the how-to guides
below.

## How-to guides

Task-oriented steps for common jobs.

- [Convert a recording to MPEG](how-to/convert-recordings-to-mpeg.md):
  turn an AVI recording into an MPEG file on Linux or Windows.

## Reference

Information-oriented descriptions of the file formats and output Generator
supports.

- [GNM sound log format](reference/gnm-format.md): header, stream decoding,
  and encoding.
- [GYM sound log format](reference/gym-format.md): the Genecyst format and its
  stream encoding.
- [AVI output formats](reference/avi-output.md): the two AVI formats Generator
  writes and the frame rates MPEG-2 supports.
- [Genecyst patch files](reference/patch-files.md): the patch file format
  Generator supports.

## Explanation

Understanding-oriented background on design decisions.

- [Why Generator has its own GNM format](explanation/gnm-format-design.md):
  the limitations of GYM, the interval design, and how Generator writes GNM.

`screenshots/` holds images referenced from the top-level [README](../README.md).

See also the top-level [README.md](../README.md), [INSTALL.md](../INSTALL.md),
and [CHANGELOG.md](../CHANGELOG.md).
