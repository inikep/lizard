# ##########################################################################
# LZ5 programs - Makefile
# Copyright (C) Yann Collet 2011-2016
# Copyright (C) Przemyslaw Skibinski 2016
#
# GPL v2 License
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
#
# You can contact the author at :
#  - LZ5 source repository : https://github.com/inikep/lz5
# ##########################################################################
# fuzzer  : Test tool, to check lz5 integrity on target platform
# fuzzer32: Same as fuzzer, but forced to compile in 32-bits mode
# frametest  : Test tool, to check lz5frame integrity on target platform
# frametest32: Same as frametest, but forced to compile in 32-bits mode
# fullbench  : Precisely measure speed for each LZ5 function variant
# fullbench32: Same as fullbench, but forced to compile in 32-bits mode
# datagen : generates synthetic data samples for tests & benchmarks
# ##########################################################################

DESTDIR ?=
PREFIX  ?= /usr/local
VOID    := /dev/null
BINDIR  := $(PREFIX)/bin
MANDIR  := $(PREFIX)/share/man/man1
LZ5DIR  := ../lib
PRGDIR  := ../programs
TESTDIR := versionsTest
PYTHON  ?= python3
ENTROPY_FILES := $(LZ5DIR)/entropy/*.c

CFLAGS  ?= -O3   # can select custom flags. For example : CFLAGS="-O2 -g" make
CFLAGS  += -Wall -Wextra -Wundef -Wcast-qual -Wcast-align -Wshadow -Wswitch-enum \
           -Wdeclaration-after-statement -Wstrict-prototypes \
           -Wpointer-arith -Wstrict-aliasing=1
CFLAGS  += $(MOREFLAGS)
CPPFLAGS:= -I$(LZ5DIR) -DXXH_NAMESPACE=LZ5_
FLAGS   := $(CFLAGS) $(CPPFLAGS) $(LDFLAGS)


# Define *.exe as extension for Windows systems
ifneq (,$(filter Windows%,$(OS)))
EXT =.exe
else
EXT =
endif


# Default test parameters
TEST_FILES   := COPYING
FUZZER_TIME  := -T3mn
NB_LOOPS     ?= -i1


default: native

m32: fullbench32 fuzzer32 frametest32

native: fullbench fuzzer frametest datagen

all: native m32


lz5:
	$(MAKE) -C $(PRGDIR) $@

lz5c:
	$(MAKE) -C $(PRGDIR) $@

lz5c32:
	$(MAKE) -C $(PRGDIR) $@

fullbench  : $(LZ5DIR)/lz5_compress.o $(LZ5DIR)/lz5_decompress.o $(LZ5DIR)/lz5frame.o $(LZ5DIR)/xxhash/xxhash.o fullbench.c $(ENTROPY_FILES)
	$(CC) $(FLAGS) $^ -o $@$(EXT)

fullbench32: $(LZ5DIR)/lz5_compress.c $(LZ5DIR)/lz5_decompress.c $(LZ5DIR)/lz5frame.c $(LZ5DIR)/xxhash/xxhash.c fullbench.c $(ENTROPY_FILES)
	$(CC) -m32 $(FLAGS) $^ -o $@$(EXT)

fullbench-lib: fullbench.c $(LZ5DIR)/xxhash/xxhash.c
	$(MAKE) -C $(LZ5DIR) liblz5.a
	$(CC) $(FLAGS) $^ -o $@$(EXT) $(LZ5DIR)/liblz5.a

fullbench-dll: fullbench.c $(LZ5DIR)/xxhash/xxhash.c
	$(MAKE) -C $(LZ5DIR) liblz5
	$(CC) $(FLAGS) $^ -o $@$(EXT) -DLZ5_DLL_IMPORT=1 $(LZ5DIR)/dll/liblz5.dll

fuzzer  : $(LZ5DIR)/lz5_compress.c $(LZ5DIR)/lz5_decompress.o $(LZ5DIR)/xxhash/xxhash.o fuzzer.c $(ENTROPY_FILES)
	$(CC) -DLZ5_RESET_MEM $(FLAGS) $^ -o $@$(EXT)

fuzzer32: $(LZ5DIR)/lz5_compress.c $(LZ5DIR)/lz5_decompress.c $(LZ5DIR)/xxhash/xxhash.c fuzzer.c $(ENTROPY_FILES)
	$(CC) -m32 -DLZ5_RESET_MEM $(FLAGS) $^ -o $@$(EXT)

frametest: $(LZ5DIR)/lz5frame.o $(LZ5DIR)/lz5_compress.o $(LZ5DIR)/lz5_decompress.o $(LZ5DIR)/xxhash/xxhash.o frametest.c $(ENTROPY_FILES)
	$(CC) $(FLAGS) $^ -o $@$(EXT)

frametest32: $(LZ5DIR)/lz5frame.c $(LZ5DIR)/lz5_compress.c $(LZ5DIR)/lz5_decompress.c $(LZ5DIR)/xxhash/xxhash.c frametest.c $(ENTROPY_FILES)
	$(CC) -m32 $(FLAGS) $^ -o $@$(EXT)

datagen : $(PRGDIR)/datagen.c datagencli.c
	$(CC) $(FLAGS) -I$(PRGDIR) $^ -o $@$(EXT)

clean:
	@$(MAKE) -C $(LZ5DIR) $@ > $(VOID)
	@$(RM) core *.o *.test tmp* \
        fullbench-dll$(EXT) fullbench-lib$(EXT) \
        fullbench$(EXT) fullbench32$(EXT) \
        fuzzer$(EXT) fuzzer32$(EXT) \
        frametest$(EXT) frametest32$(EXT) \
        datagen$(EXT)
	@rm -fR $(TESTDIR)
	@echo Cleaning completed

versionsTest:
	$(PYTHON) test-lz5-versions.py


#------------------------------------------------------------------------
#make install is validated only for Linux, OSX, kFreeBSD, Hurd, Haiku and
#FreeBSD targets
ifneq (,$(filter $(shell uname),Linux Darwin GNU/kFreeBSD GNU FreeBSD Haiku))

test: test-lz5 test-lz5c test-frametest test-fullbench test-fuzzer

test32: test-lz5c32 test-frametest32 test-fullbench32 test-fuzzer32

test-all: test test32

test-lz5-sparse: lz5 datagen
	@echo "\n ---- test sparse file support ----"
	./datagen -g5M  -P100 > tmpSrc
	$(PRGDIR)/lz5 -B4D tmpSrc | $(PRGDIR)/lz5 -dv --sparse > tmpB4
	diff -s tmpSrc tmpB4
	$(PRGDIR)/lz5 -B5D tmpSrc | $(PRGDIR)/lz5 -dv --sparse > tmpB5
	diff -s tmpSrc tmpB5
	$(PRGDIR)/lz5 -B6D tmpSrc | $(PRGDIR)/lz5 -dv --sparse > tmpB6
	diff -s tmpSrc tmpB6
	$(PRGDIR)/lz5 -B7D tmpSrc | $(PRGDIR)/lz5 -dv --sparse > tmpB7
	diff -s tmpSrc tmpB7
	$(PRGDIR)/lz5 tmpSrc | $(PRGDIR)/lz5 -dv --no-sparse > tmpNoSparse
	diff -s tmpSrc tmpNoSparse
	ls -ls tmp*
	./datagen -s1 -g1200007 -P100 | $(PRGDIR)/lz5 | $(PRGDIR)/lz5 -dv --sparse > tmpOdd   # Odd size file (to generate non-full last block)
	./datagen -s1 -g1200007 -P100 | diff -s - tmpOdd
	ls -ls tmpOdd
	@$(RM) tmp*
	@echo "\n Compatibility with Console :"
	echo "Hello World 1 !" | $(PRGDIR)/lz5 | $(PRGDIR)/lz5 -d -c
	echo "Hello World 2 !" | $(PRGDIR)/lz5 | $(PRGDIR)/lz5 -d | cat
	echo "Hello World 3 !" | $(PRGDIR)/lz5 --no-frame-crc | $(PRGDIR)/lz5 -d -c
	@echo "\n Compatibility with Append :"
	./datagen -P100 -g1M > tmp1M
	cat tmp1M tmp1M > tmp2M
	$(PRGDIR)/lz5 -B5 -v tmp1M tmpC
	$(PRGDIR)/lz5 -d -v tmpC tmpR
	$(PRGDIR)/lz5 -d -v tmpC >> tmpR
	ls -ls tmp*
	diff tmp2M tmpR
	@$(RM) tmp*

test-lz5-contentSize: lz5 datagen
	@echo "\n ---- test original size support ----"
	./datagen -g15M > tmp
	$(PRGDIR)/lz5 -v tmp | $(PRGDIR)/lz5 -t
	$(PRGDIR)/lz5 -v --content-size tmp | $(PRGDIR)/lz5 -d > tmp2
	diff -s tmp tmp2
	# test large size [2-4] GB
	@./datagen -g3G -P100 | $(PRGDIR)/lz5 | $(PRGDIR)/lz5 --decompress --force --sparse - tmp
	@ls -ls tmp
	$(PRGDIR)/lz5 --quiet --content-size tmp | $(PRGDIR)/lz5 --verbose --decompress --force --sparse - tmp2
	@ls -ls tmp2
	@$(RM) tmp*

test-lz5-frame-concatenation: lz5 datagen
	@echo "\n ---- test frame concatenation ----"
	@echo -n > empty.test
	@echo hi > nonempty.test
	cat nonempty.test empty.test nonempty.test > orig.test
	@$(PRGDIR)/lz5 -zq empty.test > empty.lz5.test
	@$(PRGDIR)/lz5 -zq nonempty.test > nonempty.lz5.test
	cat nonempty.lz5.test empty.lz5.test nonempty.lz5.test > concat.lz5.test
	$(PRGDIR)/lz5 -d concat.lz5.test > result.test
	sdiff orig.test result.test
	@$(RM) *.test
	@echo frame concatenation test completed

test-lz5-multiple: lz5 datagen
	@echo "\n ---- test multiple files ----"
	@./datagen -s1        > tmp1 2> $(VOID)
	@./datagen -s2 -g100K > tmp2 2> $(VOID)
	@./datagen -s3 -g1M   > tmp3 2> $(VOID)
	$(PRGDIR)/lz5 -f -m tmp*
	ls -ls tmp*
	@$(RM) tmp1 tmp2 tmp3
	$(PRGDIR)/lz5 -df -m *.lz5
	ls -ls tmp*
	$(PRGDIR)/lz5 -f -m tmp1 notHere tmp2; echo $$?
	@$(RM) tmp*

test-lz5-basic: lz5 datagen
	@echo "\n ---- test lz5 basic compression/decompression ----"
	./datagen -g0     | $(PRGDIR)/lz5 -v     | $(PRGDIR)/lz5 -t
	./datagen -g16KB  | $(PRGDIR)/lz5 -19    | $(PRGDIR)/lz5 -t
	./datagen -g20KB > tmpSrc
	$(PRGDIR)/lz5 < tmpSrc | $(PRGDIR)/lz5 -d > tmpRes
	diff -q tmpSrc tmpRes
	$(PRGDIR)/lz5 --no-frame-crc < tmpSrc | $(PRGDIR)/lz5 -d > tmpRes
	diff -q tmpSrc tmpRes
	./datagen         | $(PRGDIR)/lz5        | $(PRGDIR)/lz5 -t
	./datagen -g6M -P99 | $(PRGDIR)/lz5 -19BD | $(PRGDIR)/lz5 -t
	./datagen -g17M   | $(PRGDIR)/lz5 -19v    | $(PRGDIR)/lz5 -qt
	./datagen -g33M   | $(PRGDIR)/lz5 --no-frame-crc | $(PRGDIR)/lz5 -t
	./datagen -g256MB | $(PRGDIR)/lz5 -vqB4D | $(PRGDIR)/lz5 -t
	@$(RM) tmp*

test-lz5-hugefile: lz5 datagen
	@echo "\n ---- test huge files compression/decompression ----"
	./datagen -g6GB   | $(PRGDIR)/lz5 -vB5D  | $(PRGDIR)/lz5 -qt
	./datagen -g6GB   | $(PRGDIR)/lz5 -v15BD  | $(PRGDIR)/lz5 -qt
	@$(RM) tmp*

test-lz5-testmode: lz5 datagen
	@echo "\n ---- bench mode ----"
	$(PRGDIR)/lz5 -bi1
	@echo "\n ---- test mode ----"
	./datagen | $(PRGDIR)/lz5 -t             && false || true
	./datagen | $(PRGDIR)/lz5 -tf            && false || true
	@echo "\n ---- pass-through mode ----"
	./datagen | $(PRGDIR)/lz5 -d  > $(VOID)  && false || true
	./datagen | $(PRGDIR)/lz5 -df > $(VOID)
	@echo "Hello World !" > tmp1
	$(PRGDIR)/lz5 -dcf tmp1
	@echo "from underground..." > tmp2
	$(PRGDIR)/lz5 -dcfm tmp1 tmp2
	@echo "\n ---- test cli ----"
	$(PRGDIR)/lz5     file-does-not-exist    && false || true
	$(PRGDIR)/lz5 -f  file-does-not-exist    && false || true
	$(PRGDIR)/lz5 -fm file1-dne file2-dne    && false || true
	$(PRGDIR)/lz5 -fm file1-dne file2-dne    && false || true

test-lz5: lz5 datagen test-lz5-basic test-lz5-multiple test-lz5-sparse \
          test-lz5-frame-concatenation test-lz5-testmode test-lz5-contentSize \
          test-lz5-hugefile

test-lz5c: lz5c datagen
	@echo "\n ---- test lz5c version ----"
	./datagen -g256MB | $(PRGDIR)/lz5c -l -v    | $(PRGDIR)/lz5c   -t

test-interop-32-64: lz5 lz5c32 datagen
	@echo "\n ---- test interoperability 32-bits -vs- 64 bits ----"
	./datagen -g16KB  | $(PRGDIR)/lz5c32 -19    | $(PRGDIR)/lz5    -t
	./datagen -P10    | $(PRGDIR)/lz5    -19B4  | $(PRGDIR)/lz5c32 -t
	./datagen         | $(PRGDIR)/lz5c32        | $(PRGDIR)/lz5    -t
	./datagen -g1M    | $(PRGDIR)/lz5    -13B5  | $(PRGDIR)/lz5c32 -t
	./datagen -g256MB | $(PRGDIR)/lz5c32 -vqB4D | $(PRGDIR)/lz5    -qt
	./datagen -g1G -P90 | $(PRGDIR)/lz5         | $(PRGDIR)/lz5c32 -t
	./datagen -g6GB   | $(PRGDIR)/lz5c32 -vq15BD | $(PRGDIR)/lz5    -qt

test-lz5c32-basic: lz5c32 datagen
	@echo "\n ---- test lz5c32 32-bits version ----"
	./datagen -g16KB  | $(PRGDIR)/lz5c32 -19    | $(PRGDIR)/lz5c32 -t
	./datagen         | $(PRGDIR)/lz5c32        | $(PRGDIR)/lz5c32 -t
	./datagen -g256MB | $(PRGDIR)/lz5c32 -vqB4D | $(PRGDIR)/lz5c32 -qt
	./datagen -g6GB   | $(PRGDIR)/lz5c32 -vqB5D | $(PRGDIR)/lz5c32 -qt

test-platform:
	@echo "\n ---- test lz5 $(QEMU_SYS) platform ----"
	$(QEMU_SYS) ./datagen -g16KB  | $(QEMU_SYS) $(PRGDIR)/lz5 -19    | $(QEMU_SYS) $(PRGDIR)/lz5 -t
	$(QEMU_SYS) ./datagen         | $(QEMU_SYS) $(PRGDIR)/lz5        | $(QEMU_SYS) $(PRGDIR)/lz5 -t
	$(QEMU_SYS) ./datagen -g256MB | $(QEMU_SYS) $(PRGDIR)/lz5 -vqB4D | $(QEMU_SYS) $(PRGDIR)/lz5 -qt
ifneq ($(QEMU_SYS),qemu-arm-static)
ifneq ($(QEMU_SYS),qemu-ppc-static)
	$(QEMU_SYS) ./datagen -g3GB   | $(QEMU_SYS) $(PRGDIR)/lz5 -vqB5D | $(QEMU_SYS) $(PRGDIR)/lz5 -qt
endif
endif

test-lz5c32: test-lz5c32-basic test-interop-32-64

test-fullbench: fullbench
	./fullbench --no-prompt $(NB_LOOPS) $(TEST_FILES)

test-fullbench32: fullbench32
	./fullbench32 --no-prompt $(NB_LOOPS) $(TEST_FILES)

test-fuzzer: fuzzer
	./fuzzer $(FUZZER_TIME)

test-fuzzer32: fuzzer32
	./fuzzer32 $(FUZZER_TIME)

test-frametest: frametest
	./frametest $(FUZZER_TIME)

test-frametest32: frametest32
	./frametest32 $(FUZZER_TIME)

test-mem: lz5 datagen fuzzer frametest fullbench
	@echo "\n ---- valgrind tests : memory analyzer ----"
	valgrind --leak-check=yes --error-exitcode=1 ./datagen -g50M > $(VOID)
	./datagen -g16KB > tmp
	valgrind --leak-check=yes --error-exitcode=1 $(PRGDIR)/lz5 -19 -BD -f tmp $(VOID)
	./datagen -g16KB -s2 > tmp2
	./datagen -g16KB -s3 > tmp3
	valgrind --leak-check=yes --error-exitcode=1 $(PRGDIR)/lz5 --force --multiple tmp tmp2 tmp3
	./datagen -g16MB > tmp
	valgrind --leak-check=yes --error-exitcode=1 $(PRGDIR)/lz5 -19 -B5D -f tmp tmp2
	valgrind --leak-check=yes --error-exitcode=1 $(PRGDIR)/lz5 -t tmp2
	valgrind --leak-check=yes --error-exitcode=1 $(PRGDIR)/lz5 -bi1 tmp
	valgrind --leak-check=yes --error-exitcode=1 ./fullbench -i1 tmp tmp2
	./datagen -g256MB > tmp
	valgrind --leak-check=yes --error-exitcode=1 $(PRGDIR)/lz5 -B4D -f -vq tmp $(VOID)
	$(RM) tmp*
	valgrind --leak-check=yes --error-exitcode=1 ./fuzzer -i64 -t1
	valgrind --leak-check=yes --error-exitcode=1 ./frametest -i256

test-mem32: lz5c32 datagen
# unfortunately, valgrind doesn't seem to work with non-native binary...

endif
