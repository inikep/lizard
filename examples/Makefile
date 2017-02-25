# ##########################################################################
# LZ5 examples - Makefile
# Copyright (C) Yann Collet 2011-2014
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
# This makefile compile and test
# example programs, using (mostly) LZ5 streaming library,
# kindly provided by Takayuki Matsuoka
# ##########################################################################

CFLAGS ?= -O3
CFLAGS += -std=gnu99 -Wall -Wextra -Wundef -Wshadow -Wcast-align -Wstrict-prototypes
FLAGS  := -I../lib $(CPPFLAGS) $(CFLAGS) $(LDFLAGS)

TESTFILE= Makefile
LIBDIR := ../lib
LZ5 = ../programs/lz5
ENTROPY_FILES := $(LIBDIR)/entropy/*.c


# Define *.exe as extension for Windows systems
ifneq (,$(filter Windows%,$(OS)))
EXT =.exe
VOID = nul
else
EXT =
VOID = /dev/null
endif


default: all

all: printVersion doubleBuffer ringBuffer ringBufferHC lineCompress frameCompress

printVersion: $(LIBDIR)/lz5_compress.c printVersion.c $(ENTROPY_FILES)
	$(CC)      $(FLAGS) $^ -o $@$(EXT)

doubleBuffer: $(LIBDIR)/lz5_decompress.c $(LIBDIR)/lz5_compress.c blockStreaming_doubleBuffer.c $(ENTROPY_FILES)
	$(CC)      $(FLAGS) $^ -o $@$(EXT)

ringBuffer  : $(LIBDIR)/lz5_decompress.c $(LIBDIR)/lz5_compress.c blockStreaming_ringBuffer.c $(ENTROPY_FILES)
	$(CC)      $(FLAGS) $^ -o $@$(EXT)

ringBufferHC: $(LIBDIR)/lz5_decompress.c $(LIBDIR)/lz5_compress.c HCStreaming_ringBuffer.c $(ENTROPY_FILES)
	$(CC)      $(FLAGS) $^ -o $@$(EXT)

lineCompress: $(LIBDIR)/lz5_decompress.c $(LIBDIR)/lz5_compress.c blockStreaming_lineByLine.c $(ENTROPY_FILES)
	$(CC)      $(FLAGS) $^ -o $@$(EXT)

frameCompress: frameCompress.c
	$(CC)      $(FLAGS) $^ -o $@$(EXT) -L$(LIBDIR) -llz5

compressFunctions: $(LIBDIR)/lz5_decompress.c $(LIBDIR)/lz5_compress.c compress_functions.c $(ENTROPY_FILES)
	$(CC)      $(FLAGS) $^ -o $@$(EXT) -lrt

simpleBuffer: $(LIBDIR)/lz5_decompress.c $(LIBDIR)/lz5_compress.c simple_buffer.c $(ENTROPY_FILES)
	$(CC)      $(FLAGS) $^ -o $@$(EXT)

test : all
	./printVersion$(EXT)
	./doubleBuffer$(EXT) $(TESTFILE)
	./ringBuffer$(EXT)   $(TESTFILE)
	./ringBufferHC$(EXT) $(TESTFILE)
	./lineCompress$(EXT) $(TESTFILE)
	LD_LIBRARY_PATH=$(LIBDIR) ./frameCompress$(EXT) $(TESTFILE)
	$(LZ5) -vt $(TESTFILE).lz5

clean:
	@rm -f core *.o *.dec *-0 *-9 *-8192 *.lz5s *.lz5 \
	 printVersion$(EXT) doubleBuffer$(EXT) ringBuffer$(EXT) ringBufferHC$(EXT) \
	 lineCompress$(EXT) frameCompress$(EXT) compressFunctions$(EXT) simpleBuffer$(EXT)
	@echo Cleaning completed

