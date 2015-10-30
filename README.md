LZ5 is a modification of LZ4 which gives a better ratio at cost of slower compression and decompression speed. This is caused mainly because of 22-bit dictionary instead of 16-bit in LZ4.
LZ5 uses different output codewords and is not compatible with LZ4.

LZ4 output codewords are 3 byte long (24-bit) and look as follows:
 LLLL_MMMM OOOOOOOO OOOOOOOO - 16-bit offset, 4-bit match length, 4-bit literal length 

LZ5 uses 3 types of codewords from 2 to 4 bytes long:
 1_OO_LL_MMM OOOOOOOO          - 10-bit offset, 3-bit match length, 2-bit literal length
 00__LLL_MMM OOOOOOOO OOOOOOOO - 16-bit offset, 3-bit match length, 3-bit literal length
 01__LLL_MMM OOOOOOOO OOOOOOOO OOOOOOOO - 24-bit offset, 3-bit match length, 3-bit literal length 



LZ5 - Extremely fast compression
================================

LZ5 is lossless compression algorithm, 
providing compression speed at 400 MB/s per core, 
scalable with multi-cores CPU. 
It features an extremely fast decoder, 
with speed in multiple GB/s per core, 
typically reaching RAM speed limits on multi-core systems.

Speed can be tuned dynamically, selecting an "acceleration" factor
which trades compression ratio for more speed up.
On the other end, a high compression derivative, LZ5_HC, is also provided,
trading CPU time for improved compression ratio.
All versions feature the same decompression speed.

LZ5 library is provided as open-source software using BSD license.s


|Branch      |Status   |
|------------|---------|
|master      | [![Build Status][travisMasterBadge]][travisLink] [![Build status][AppveyorMasterBadge]][AppveyorLink] [![coverity][coverBadge]][coverlink] |
|dev         | [![Build Status][travisDevBadge]][travisLink]    [![Build status][AppveyorDevBadge]][AppveyorLink]                                         |

[travisMasterBadge]: https://travis-ci.org/inikep/lz5.svg?branch=master "Continuous Integration test suite"
[travisDevBadge]: https://travis-ci.org/inikep/lz5.svg?branch=dev "Continuous Integration test suite"
[travisLink]: https://ci.appveyor.com/project/YannCollet/lz5
[AppveyorMasterBadge]: https://ci.appveyor.com/api/projects/status/v6kxv9si529477cq/branch/master?svg=true "Visual test suite"
[AppveyorDevBadge]: https://ci.appveyor.com/api/projects/status/v6kxv9si529477cq/branch/dev?svg=true "Visual test suite"
[AppveyorLink]: https://ci.appveyor.com/project/YannCollet/lz5
[coverBadge]: https://scan.coverity.com/projects/4735/badge.svg "Static code analysis of Master branch"
[coverlink]: https://scan.coverity.com/projects/4735

> **Branch Policy:**

> - The "master" branch is considered stable, at all times.
> - The "dev" branch is the one where all contributions must be merged
    before being promoted to master.
>   + If you plan to propose a patch, please commit into the "dev" branch,
      or its own feature branch.
      Direct commit to "master" are not permitted.

Benchmarks
-------------------------

The benchmark uses the [Open-Source Benchmark program by m^2 (v0.14.3)]
compiled with GCC v4.8.2 on Linux Mint 64-bits v17.
The reference system uses a Core i5-4300U @1.9GHz.
Benchmark evaluates the compression of reference [Silesia Corpus]
in single-thread mode.

|  Compressor          | Ratio   | Compression | Decompression |
|  ----------          | -----   | ----------- | ------------- |
|  memcpy              |  1.000  | 4200 MB/s   |   4200 MB/s   |
|**LZ5 fast 17 (r129)**|  1.607  |**690 MB/s** | **2220 MB/s** |
|**LZ5 default (r129)**|**2.101**|**385 MB/s** | **1850 MB/s** |
|  LZO 2.06            |  2.108  |  350 MB/s   |    510 MB/s   |
|  QuickLZ 1.5.1.b6    |  2.238  |  320 MB/s   |    380 MB/s   |
|  Snappy 1.1.0        |  2.091  |  250 MB/s   |    960 MB/s   |
|  LZF v3.6            |  2.073  |  175 MB/s   |    500 MB/s   |
|  zlib 1.2.8 -1       |  2.730  |   59 MB/s   |    250 MB/s   |
|**LZ5 HC (r129)**     |**2.720**|   22 MB/s   | **1830 MB/s** |
|  zlib 1.2.8 -6       |  3.099  |   18 MB/s   |    270 MB/s   |


Documentation
-------------------------

The raw LZ5 block compression format is detailed within [lz5_Block_format].

To compress an arbitrarily long file or data stream, multiple blocks are required.
Organizing these blocks and providing a common header format to handle their content
is the purpose of the Frame format, defined into [lz5_Frame_format].
Interoperable versions of LZ5 must respect this frame format.


Other source versions
-------------------------

Beyond the C reference source, 
many contributors have created versions of lz5 in multiple languages
(Java, C#, Python, Perl, Ruby, etc.).
A list of known source ports is maintained on the [LZ5 Homepage].


[Open-Source Benchmark program by m^2 (v0.14.3)]: http://encode.ru/threads/1371-Filesystem-benchmark?p=34029&viewfull=1#post34029
[Silesia Corpus]: http://sun.aei.polsl.pl/~sdeor/index.php?page=silesia
[lz5_Block_format]: lz5_Block_format.md
[lz5_Frame_format]: lz5_Frame_format.md
[LZ5 Homepage]: http://www.lz5.org
