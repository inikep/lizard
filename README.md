Introduction
-------------------------

LZ5 is a modification of [LZ4] which gives a better ratio at cost of slower compression and decompression speed. 
This is caused mainly because of 22-bit dictionary instead of 16-bit in LZ4.

LZ5 uses different output codewords and is not compatible with LZ4. LZ4 output codewords are 3 byte long (24-bit) and look as follows:
- LLLL_MMMM OOOOOOOO OOOOOOOO - 16-bit offset, 4-bit match length, 4-bit literal length 

LZ5 uses 3 types of codewords from 2 to 4 bytes long:
- 1_OO_LL_MMM OOOOOOOO - 10-bit offset, 3-bit match length, 2-bit literal length
- 00_LLL_MMM OOOOOOOO OOOOOOOO - 16-bit offset, 3-bit match length, 3-bit literal length
- 010_LL_MMM OOOOOOOO OOOOOOOO OOOOOOOO - 24-bit offset, 3-bit match length, 2-bit literal length 
- 011_LL_MMM - last offset, 3-bit match length, 2-bit literal length

[LZ4]: https://github.com/Cyan4973/lz4

Benchmarks
-------------------------

In our experiments decompression speed of LZ5 is from 650-950 MB/s. It's slower than LZ4 but much faster than zstd and brotli.
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
| lz5 r131                    |   195 MB/s |   939 MB/s |    55884927 | 53.30 |
| lz5hc r131 -1               |    32 MB/s |   742 MB/s |    52927122 | 50.48 |
| lz5hc r131 -3               |    20 MB/s |   716 MB/s |    50970192 | 48.61 |
| lz5hc r131 -5               |    10 MB/s |   701 MB/s |    49970285 | 47.66 |
| lz5hc r131 -7               |  5.54 MB/s |   682 MB/s |    49541511 | 47.25 |
| lz5hc r131 -9               |  2.69 MB/s |   673 MB/s |    49346894 | 47.06 |
| lz5hc r131 -11              |  1.36 MB/s |   664 MB/s |    49266526 | 46.98 |
| zstd v0.3                   |   257 MB/s |   547 MB/s |    51231016 | 48.86 |
| zstd_HC v0.3 -1             |   257 MB/s |   553 MB/s |    51231016 | 48.86 |
| zstd_HC v0.3 -3             |    76 MB/s |   417 MB/s |    46774383 | 44.61 |
| zstd_HC v0.3 -5             |    40 MB/s |   476 MB/s |    45628362 | 43.51 |
| zstd_HC v0.3 -9             |    14 MB/s |   485 MB/s |    44840562 | 42.76 |
| zstd_HC v0.3 -13            |  9.34 MB/s |   469 MB/s |    43114895 | 41.12 |
| zstd_HC v0.3 -17            |  6.02 MB/s |   463 MB/s |    42989971 | 41.00 |
| zstd_HC v0.3 -21            |  3.35 MB/s |   461 MB/s |    42956964 | 40.97 |
| zstd_HC v0.3 -23            |  2.33 MB/s |   463 MB/s |    42934217 | 40.95 |
| brotli 2015-10-29 -1        |    86 MB/s |   208 MB/s |    47882059 | 45.66 |
| brotli 2015-10-29 -3        |    60 MB/s |   214 MB/s |    47451223 | 45.25 |
| brotli 2015-10-29 -5        |    17 MB/s |   217 MB/s |    43363897 | 41.36 |
| brotli 2015-10-29 -7        |  4.80 MB/s |   227 MB/s |    41222719 | 39.31 |
| brotli 2015-10-29 -9        |  2.23 MB/s |   222 MB/s |    40839209 | 38.95 |

The above results are obtained with [lzbench] using 1 core of Intel Core i5-4300U, Windows 10 64-bit (MinGW-w64 compilation under gcc 4.8.3) with 3 iterations. 
The ["win81"] input file (100 MB) is a concatanation of carefully selected files from installed version of Windows 8.1 64-bit. 

[lzbench]: https://github.com/inikep/lzbench
["win81"]: https://docs.google.com/uc?id=0BwX7dtyRLxThRzBwT0xkUy1TMFE&export=download
