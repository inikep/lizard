Introduction
-------------------------

LZ5 is a modification of [LZ4] which gives a better ratio at cost of slower compression and decompression speed. 
**In my experiments there is no open-source bytewise compressor that gives better ratio than lz5hc.**
The improvement in compression ratio is caused mainly because of:
- 22-bit dictionary instead of 16-bit in LZ4
- using 4 new parsers (including an optimal parser) optimized for a bigger dictionary
- support for 3-byte long matches (MINMATCH = 3)
- a special 1-byte codeword for the last occured offset

[LZ4]: https://github.com/Cyan4973/lz4


The codewords description
-------------------------

LZ5 uses different output codewords and is not compatible with LZ4. LZ4 output codewords are 3 byte long (24-bit) and look as follows:
- LLLL_MMMM OOOOOOOO OOOOOOOO - 16-bit offset, 4-bit match length, 4-bit literal length 

LZ5 uses 4 types of codewords from 1 to 4 bytes long:
- [1_OO_LL_MMM] [OOOOOOOO] - 10-bit offset, 3-bit match length, 2-bit literal length
- [00_LLL_MMM] [OOOOOOOO] [OOOOOOOO] - 16-bit offset, 3-bit match length, 3-bit literal length
- [010_LL_MMM] [OOOOOOOO] [OOOOOOOO] [OOOOOOOO] - 24-bit offset, 3-bit match length, 2-bit literal length
- [011_LL_MMM] - last offset, 3-bit match length, 2-bit literal length

1, 00, 010, 011 can be seen as Huffman codes and are selected according to frequences of given codewords for my test files. 
Match lengths have always 3-bits (MMM) and literal lengths are usually 2-bits (LL) because it gives better ratio than any other division of 5-bits remaining bits. 
So we can encode values 0-7 (3-bits) for matches (what means length of 3-10 for MINMATCH=3). But 7 is reserved as a flag signaling that a match is equal or longer
that 10 bytes. So e.g. 30 is encoded as a flag 7 (match length=10) and a next byte 30-10=20. I tried many different variants (e.g. separate match lenghts and literal lenghts)
but these codewords were the best. 


Benchmarks
-------------------------

In our experiments decompression speed of LZ5 is from 600-1600 MB/s. It's slower than LZ4 but much faster than zstd and brotli.
With the compresion ratio is opposite: LZ5 is better than LZ4 but worse than zstd and brotli.

| Compressor name             | Compression| Decompress.| Compr. size | Ratio |
| ---------------             | -----------| -----------| ----------- | ----- |
| memcpy                      |  8533 MB/s |  8533 MB/s |   104857600 |100.00 |
| lz4 r131                    |   480 MB/s |  2275 MB/s |    64872315 | 61.87 |
| lz4hc r131 -1               |    82 MB/s |  1896 MB/s |    59448496 | 56.69 |
| lz4hc r131 -3               |    54 MB/s |  1932 MB/s |    56343753 | 53.73 |
| lz4hc r131 -5               |    41 MB/s |  1969 MB/s |    55271312 | 52.71 |
| lz4hc r131 -7               |    31 MB/s |  1969 MB/s |    54889301 | 52.35 |
| lz4hc r131 -9               |    24 MB/s |  1969 MB/s |    54773517 | 52.24 |
| lz4hc r131 -11              |    20 MB/s |  1969 MB/s |    54751363 | 52.21 |
| lz4hc r131 -13              |    17 MB/s |  1969 MB/s |    54744790 | 52.21 |
| lz4hc r131 -15              |    14 MB/s |  2007 MB/s |    54741827 | 52.21 |
| lz5 v1.3.3                  |   191 MB/s |   892 MB/s |    56183327 | 53.58 |
| lz5hc v1.3.3 level 1        |   468 MB/s |  1682 MB/s |    68770655 | 65.58 |
| lz5hc v1.3.3 level 2        |   337 MB/s |  1574 MB/s |    65201626 | 62.18 |
| lz5hc v1.3.3 level 3        |   232 MB/s |  1330 MB/s |    61423270 | 58.58 |
| lz5hc v1.3.3 level 4        |   129 MB/s |   894 MB/s |    55011906 | 52.46 |
| lz5hc v1.3.3 level 5        |    99 MB/s |   840 MB/s |    52790905 | 50.35 |
| lz5hc v1.3.3 level 6        |    41 MB/s |   894 MB/s |    52561673 | 50.13 |
| lz5hc v1.3.3 level 7        |    35 MB/s |   875 MB/s |    50947061 | 48.59 |
| lz5hc v1.3.3 level 8        |    23 MB/s |   812 MB/s |    50049555 | 47.73 |
| lz5hc v1.3.3 level 9        |    17 MB/s |   727 MB/s |    48718531 | 46.46 |
| lz5hc v1.3.3 level 10       |    13 MB/s |   728 MB/s |    48109030 | 45.88 |
| lz5hc v1.3.3 level 11       |  9.18 MB/s |   719 MB/s |    47438817 | 45.24 |
| lz5hc v1.3.3 level 12       |  7.96 MB/s |   752 MB/s |    47063261 | 44.88 |
| lz5hc v1.3.3 level 13       |  5.43 MB/s |   762 MB/s |    46718698 | 44.55 |
| lz5hc v1.3.3 level 14       |  4.34 MB/s |   756 MB/s |    46484969 | 44.33 |
| lz5hc v1.3.3 level 15       |  1.96 MB/s |   760 MB/s |    46227364 | 44.09 |
| lz5hc v1.3.3 level 16       |  0.81 MB/s |   681 MB/s |    46125742 | 43.99 |
| lz5hc v1.3.3 level 17       |  0.39 MB/s |   679 MB/s |    46050114 | 43.92 |
| lz5hc v1.3.3 level 18       |  0.16 MB/s |   541 MB/s |    46008853 | 43.88 |
| zstd v0.4.1 level 1         |   249 MB/s |   537 MB/s |    51160301 | 48.79 |
| zstd v0.4.1 level 2         |   183 MB/s |   505 MB/s |    49719335 | 47.42 |
| zstd v0.4.1 level 5         |    72 MB/s |   461 MB/s |    46389082 | 44.24 |
| zstd v0.4.1 level 9         |    17 MB/s |   474 MB/s |    43892280 | 41.86 |
| zstd v0.4.1 level 13        |    10 MB/s |   487 MB/s |    42321163 | 40.36 |
| zstd v0.4.1 level 17        |  1.97 MB/s |   476 MB/s |    42009876 | 40.06 |
| zstd v0.4.1 level 20        |  1.70 MB/s |   459 MB/s |    41880158 | 39.94 |
| brotli 2015-10-29 -1        |    86 MB/s |   208 MB/s |    47882059 | 45.66 |
| brotli 2015-10-29 -3        |    60 MB/s |   214 MB/s |    47451223 | 45.25 |
| brotli 2015-10-29 -5        |    17 MB/s |   217 MB/s |    43363897 | 41.36 |
| brotli 2015-10-29 -7        |  4.80 MB/s |   227 MB/s |    41222719 | 39.31 |
| brotli 2015-10-29 -9        |  2.23 MB/s |   222 MB/s |    40839209 | 38.95 |

The above results are obtained with [lzbench] using 1 core of Intel Core i5-4300U, Windows 10 64-bit (MinGW-w64 compilation under gcc 4.8.3) with 3 iterations. 
The ["win81"] input file (100 MB) is a concatanation of carefully selected files from installed version of Windows 8.1 64-bit. 

[lzbench]: https://github.com/inikep/lzbench
["win81"]: https://docs.google.com/uc?id=0BwX7dtyRLxThRzBwT0xkUy1TMFE&export=download
