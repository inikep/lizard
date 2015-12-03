Introduction
-------------------------

LZ5 is a modification of [LZ4] which gives a better ratio at cost of slower compression and decompression speed. 
This is caused mainly because of 22-bit dictionary instead of 16-bit in LZ4.

LZ5 uses different output codewords and is not compatible with LZ4. LZ4 output codewords are 3 byte long (24-bit) and look as follows:
- LLLL_MMMM OOOOOOOO OOOOOOOO - 16-bit offset, 4-bit match length, 4-bit literal length 

LZ5 uses 4 types of codewords from 1 to 4 bytes long:
- 1_OO_LL_MMM OOOOOOOO - 10-bit offset, 3-bit match length, 2-bit literal length
- 00_LLL_MMM OOOOOOOO OOOOOOOO - 16-bit offset, 3-bit match length, 3-bit literal length
- 010_LL_MMM OOOOOOOO OOOOOOOO OOOOOOOO - 24-bit offset, 3-bit match length, 2-bit literal length 
- 011_LL_MMM - last offset, 3-bit match length, 2-bit literal length

[LZ4]: https://github.com/Cyan4973/lz4

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
| lz5 r132                    |   180 MB/s |   877 MB/s |    56183327 | 53.58 |
| lz5hc r132 level 1          |   453 MB/s |  1649 MB/s |    68770655 | 65.58 |
| lz5hc r132 level 2          |   341 MB/s |  1533 MB/s |    65201626 | 62.18 |
| lz5hc r132 level 3          |   222 MB/s |  1267 MB/s |    61423270 | 58.58 |
| lz5hc r132 level 4          |   122 MB/s |   892 MB/s |    55011906 | 52.46 |
| lz5hc r132 level 5          |    92 MB/s |   784 MB/s |    52790905 | 50.35 |
| lz5hc r132 level 6          |    40 MB/s |   872 MB/s |    52561673 | 50.13 |
| lz5hc r132 level 7          |    30 MB/s |   825 MB/s |    50947061 | 48.59 |
| lz5hc r132 level 8          |    21 MB/s |   771 MB/s |    50049555 | 47.73 |
| lz5hc r132 level 9          |    16 MB/s |   702 MB/s |    48718531 | 46.46 |
| lz5hc r132 level 10         |    12 MB/s |   670 MB/s |    48109030 | 45.88 |
| lz5hc r132 level 11         |  6.60 MB/s |   592 MB/s |    47639520 | 45.43 |
| lz5hc r132 level 12         |  3.22 MB/s |   670 MB/s |    47461368 | 45.26 |
| zstd_HC v0.3.6 level 1      |   250 MB/s |   529 MB/s |    51230550 | 48.86 |
| zstd_HC v0.3.6 level 2      |   186 MB/s |   498 MB/s |    49678572 | 47.38 |
| zstd_HC v0.3.6 level 3      |    90 MB/s |   484 MB/s |    48838293 | 46.58 |
| zstd_HC v0.3.6 level 5      |    61 MB/s |   467 MB/s |    46480999 | 44.33 |
| zstd_HC v0.3.6 level 7      |    28 MB/s |   480 MB/s |    44803941 | 42.73 |
| zstd_HC v0.3.6 level 9      |    15 MB/s |   497 MB/s |    43899996 | 41.87 |
| zstd_HC v0.3.6 level 12     |    11 MB/s |   505 MB/s |    42402232 | 40.44 |
| zstd_HC v0.3.6 level 16     |  2.29 MB/s |   499 MB/s |    42122327 | 40.17 |
| zstd_HC v0.3.6 level 20     |  1.65 MB/s |   454 MB/s |    41884658 | 39.94 |
| brotli 2015-10-29 -1        |    86 MB/s |   208 MB/s |    47882059 | 45.66 |
| brotli 2015-10-29 -3        |    60 MB/s |   214 MB/s |    47451223 | 45.25 |
| brotli 2015-10-29 -5        |    17 MB/s |   217 MB/s |    43363897 | 41.36 |
| brotli 2015-10-29 -7        |  4.80 MB/s |   227 MB/s |    41222719 | 39.31 |
| brotli 2015-10-29 -9        |  2.23 MB/s |   222 MB/s |    40839209 | 38.95 |

The above results are obtained with [lzbench] using 1 core of Intel Core i5-4300U, Windows 10 64-bit (MinGW-w64 compilation under gcc 4.8.3) with 3 iterations. 
The ["win81"] input file (100 MB) is a concatanation of carefully selected files from installed version of Windows 8.1 64-bit. 

[lzbench]: https://github.com/inikep/lzbench
["win81"]: https://docs.google.com/uc?id=0BwX7dtyRLxThRzBwT0xkUy1TMFE&export=download
