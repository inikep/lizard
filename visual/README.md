Projects for various integrated development environments (IDE)
==============================================================

#### Included projects

The following projects are included with the lz5 distribution:
- `VS2010` - Visual Studio 2010 project (which also works well with Visual Studio 2012, 2013, 2015)


#### How to compile lz5 with Visual Studio

1. Install Visual Studio e.g. VS 2015 Community Edition (it's free).
2. Download the latest version of lz5 from https://github.com/inikep/lz5/releases
3. Decompress ZIP archive.
4. Go to decompressed directory then to `visual` then `VS2010` and open `lz5.sln`
5. Visual Studio will ask about converting VS2010 project to VS2015 and you should agree.
6. Change `Debug` to `Release` and if you have 64-bit Windows change also `Win32` to `x64`.
7. Press F7 on keyboard or select `BUILD` from the menu bar and choose `Build Solution`.
8. If compilation will be fine a compiled executable will be in `visual\VS2010\bin\x64_Release\lz5.exe`


#### Projects available within lz5.sln

The Visual Studio solution file `lz5.sln` contains many projects that will be compiled to the
`visual\VS2010\bin\$(Platform)_$(Configuration)` directory. For example `lz5` set to `x64` and
`Release` will be compiled to `visual\VS2010\bin\x64_Release\lz5.exe`. The solution file contains the
following projects:

- `lz5` : Command Line Utility, supporting gzip-like arguments
- `datagen` : Synthetic and parametrable data generator, for tests
- `frametest` : Test tool that checks lz5frame integrity on target platform
- `fullbench`  : Precisely measure speed for each lz5 inner functions
- `fuzzer` : Test tool, to check lz5 integrity on target platform 
- `liblz5` : A static LZ5 library compiled to `liblz5_static.lib`
- `liblz5-dll` : A dynamic LZ5 library (DLL) compiled to `liblz5.dll` with the import library `liblz5.lib`
- `fullbench-dll` : The fullbench program compiled with the import library; the executable requires LZ5 DLL


#### Using LZ5 DLL with Microsoft Visual C++ project

The header files `lib\lz5.h`, `lib\lz5hc.h`, `lib\lz5frame.h` and the import library
`visual\VS2010\bin\$(Platform)_$(Configuration)\liblz5.lib` are required to compile a
project using Visual C++.

1. The path to header files should be added to `Additional Include Directories` that can
   be found in Project Properties of Visual Studio IDE in the `C/C++` Property Pages on the `General` page.
2. The import library has to be added to `Additional Dependencies` that can
   be found in Project Properties in the `Linker` Property Pages on the `Input` page.
   If one will provide only the name `liblz5.lib` without a full path to the library
   then the directory has to be added to `Linker\General\Additional Library Directories`.

The compiled executable will require LZ5 DLL which is available at
`visual\VS2010\bin\$(Platform)_$(Configuration)\liblz5.dll`.
