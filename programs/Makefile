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
# lz5 : Command Line Utility, supporting gzip-like arguments
# lz5c32: Same as lz5, but forced to compile in 32-bits mode
# ##########################################################################

DESTDIR ?=
PREFIX  ?= /usr/local
VOID = /dev/null
BINDIR  := $(PREFIX)/bin
MANDIR  := $(PREFIX)/share/man/man1
LZ5DIR  := ../lib
ENTROPY_FILES := $(LZ5DIR)/entropy/*.c
#TEST_CODEWORDS := $(LZ5DIR)/test/*.h

CFLAGS  ?= -O3   # allows custom optimization flags. For example : CFLAGS="-O2 -g" make
CFLAGS  += -Wall -Wextra -Wundef -Wcast-qual -Wcast-align -Wshadow \
           -Wswitch-enum -Wdeclaration-after-statement -Wstrict-prototypes \
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



ifeq ($(shell $(CC) -v 2>&1 | grep -c "gcc version "), 1)
#ALIGN_LOOP = -falign-loops=32
endif


default: lz5

m32: lz5c32

native: lz5

all: native m32

# dependencies
$(LZ5DIR)/lz5_compress.o: $(LZ5DIR)/lz5_compress.c $(LZ5DIR)/lz5_common.h $(LZ5DIR)/lz5_compress*.h $(LZ5DIR)/lz5_parser_*.h $(TEST_CODEWORDS)
	$(CC) $(FLAGS) $< -c -o $@

$(LZ5DIR)/lz5_compress32.o: $(LZ5DIR)/lz5_compress.c $(LZ5DIR)/lz5_common.h $(LZ5DIR)/lz5_compress*.h $(LZ5DIR)/lz5_parser_*.h $(TEST_CODEWORDS)
	$(CC) -m32 $(FLAGS) $< -c -o $@

$(LZ5DIR)/lz5_compress_nohuf.o: $(LZ5DIR)/lz5_compress.c $(LZ5DIR)/lz5_common.h $(LZ5DIR)/lz5_compress*.h $(LZ5DIR)/lz5_parser_*.h $(TEST_CODEWORDS)
	$(CC) -DLZ5_NO_HUFFMAN $(FLAGS) $< -c -o $@

$(LZ5DIR)/lz5_decompress.o: $(LZ5DIR)/lz5_decompress.c $(LZ5DIR)/lz5_compress.h $(LZ5DIR)/lz5_common.h $(LZ5DIR)/lz5_decompress*.h $(TEST_CODEWORDS)
	$(CC) $(FLAGS) $(ALIGN_LOOP) $< -c -o $@

$(LZ5DIR)/lz5_decompress32.o: $(LZ5DIR)/lz5_decompress.c $(LZ5DIR)/lz5_compress.h $(LZ5DIR)/lz5_common.h $(LZ5DIR)/lz5_decompress*.h $(TEST_CODEWORDS)
	$(CC) -m32 $(FLAGS) $< -c -o $@

$(LZ5DIR)/lz5_decompress_nohuf.o: $(LZ5DIR)/lz5_decompress.c $(LZ5DIR)/lz5_compress.h $(LZ5DIR)/lz5_common.h $(LZ5DIR)/lz5_decompress*.h $(TEST_CODEWORDS)
	$(CC) -DLZ5_NO_HUFFMAN $(FLAGS) $(ALIGN_LOOP) $< -c -o $@



lz5: $(LZ5DIR)/lz5_decompress.o $(LZ5DIR)/lz5_compress.o $(LZ5DIR)/lz5frame.o $(LZ5DIR)/xxhash/xxhash.o bench.o lz5io.o lz5cli.o datagen.o $(ENTROPY_FILES)
	$(CC) $(FLAGS) $^ -o $@$(EXT)

lz5nohuf: $(LZ5DIR)/lz5_decompress_nohuf.o $(LZ5DIR)/lz5_compress_nohuf.o $(LZ5DIR)/lz5frame.o $(LZ5DIR)/xxhash/xxhash.o bench.o lz5io.o lz5cli.o datagen.o
	$(CC) $(FLAGS) $^ -o $@$(EXT)

lz5c32: $(LZ5DIR)/lz5_decompress32.o $(LZ5DIR)/lz5_compress32.o $(LZ5DIR)/lz5frame.c $(LZ5DIR)/xxhash/xxhash.c bench.c lz5io.c lz5cli.c datagen.c $(ENTROPY_FILES)
	$(CC) -m32 $(FLAGS) $^ -o $@$(EXT)


clean:
	@$(MAKE) -C $(LZ5DIR) $@ > $(VOID)
	@rm -f core *.o *.test tmp* \
        lz5$(EXT) lz5c32$(EXT) \
	@echo Cleaning completed


#------------------------------------------------------------------------
#make install is validated only for Linux, OSX, kFreeBSD, Hurd and
#FreeBSD targets
ifneq (,$(filter Linux Darwin GNU/kFreeBSD GNU FreeBSD MSYS%, $(shell uname)))

install: lz5$(EXT)
	@echo Installing binaries
	@install -d -m 755 $(DESTDIR)$(BINDIR)/ $(DESTDIR)$(MANDIR)/
	@install -m 755 lz5$(EXT) $(DESTDIR)$(BINDIR)/lz5$(EXT)
	@ln -sf lz5$(EXT) $(DESTDIR)$(BINDIR)/lz5cat$(EXT)
	@ln -sf lz5$(EXT) $(DESTDIR)$(BINDIR)/unlz5$(EXT)
	@echo Installing man pages
	@install -m 644 lz5.1 $(DESTDIR)$(MANDIR)/lz5.1
	@ln -sf lz5.1 $(DESTDIR)$(MANDIR)/lz5cat.1
	@ln -sf lz5.1 $(DESTDIR)$(MANDIR)/unlz5.1
	@echo lz5 installation completed

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/lz5cat$(EXT)
	rm -f $(DESTDIR)$(BINDIR)/unlz5$(EXT)
	[ -x $(DESTDIR)$(BINDIR)/lz5$(EXT) ] && rm -f $(DESTDIR)$(BINDIR)/lz5$(EXT)
	[ -f $(DESTDIR)$(MANDIR)/lz5.1 ] && rm -f $(DESTDIR)$(MANDIR)/lz5.1
	rm -f $(DESTDIR)$(MANDIR)/lz5cat.1
	rm -f $(DESTDIR)$(MANDIR)/unlz5.1
	@echo lz5 programs successfully uninstalled

endif
