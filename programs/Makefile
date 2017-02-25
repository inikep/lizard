# ##########################################################################
# Lizard programs - Makefile
# Copyright (C) Yann Collet 2011-2016
# Copyright (C) Przemyslaw Skibinski 2016
#
# This Makefile is validated for Linux, macOS, *BSD, Hurd, Solaris, MSYS2 targets
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
#  - Lizard source repository : https://github.com/inikep/lz5
# ##########################################################################
# lz5 : Command Line Utility, supporting gzip-like arguments
# lz5c32: Same as lz5, but forced to compile in 32-bits mode
# ##########################################################################

VOID = /dev/null
LIBDIR  := ../lib
ENTROPY_FILES := $(LIBDIR)/entropy/*.c
#TEST_CODEWORDS := $(LIBDIR)/test/*.h

CFLAGS  ?= -O3   # allows custom optimization flags. For example : CFLAGS="-O2 -g" make
CFLAGS  += -Wall -Wextra -Wundef -Wcast-qual -Wcast-align -Wshadow \
           -Wswitch-enum -Wdeclaration-after-statement -Wstrict-prototypes \
		   -Wpointer-arith -Wstrict-aliasing=1
CFLAGS  += $(MOREFLAGS)
CPPFLAGS:= -I$(LIBDIR) -DXXH_NAMESPACE=Lizard_
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
$(LIBDIR)/lz5_compress.o: $(LIBDIR)/lz5_compress.c $(LIBDIR)/lz5_common.h $(LIBDIR)/lz5_compress*.h $(LIBDIR)/lz5_parser_*.h $(TEST_CODEWORDS)
	$(CC) $(FLAGS) $< -c -o $@

$(LIBDIR)/lz5_compress32.o: $(LIBDIR)/lz5_compress.c $(LIBDIR)/lz5_common.h $(LIBDIR)/lz5_compress*.h $(LIBDIR)/lz5_parser_*.h $(TEST_CODEWORDS)
	$(CC) -m32 $(FLAGS) $< -c -o $@

$(LIBDIR)/lz5_decompress.o: $(LIBDIR)/lz5_decompress.c $(LIBDIR)/lz5_compress.h $(LIBDIR)/lz5_common.h $(LIBDIR)/lz5_decompress*.h $(TEST_CODEWORDS)
	$(CC) $(FLAGS) $(ALIGN_LOOP) $< -c -o $@

$(LIBDIR)/lz5_decompress32.o: $(LIBDIR)/lz5_decompress.c $(LIBDIR)/lz5_compress.h $(LIBDIR)/lz5_common.h $(LIBDIR)/lz5_decompress*.h $(TEST_CODEWORDS)
	$(CC) -m32 $(FLAGS) $< -c -o $@



lz5: $(LIBDIR)/lz5_decompress.o $(LIBDIR)/lz5_compress.o $(LIBDIR)/lz5frame.o $(LIBDIR)/xxhash/xxhash.o bench.o lz5io.o lz5cli.o datagen.o $(ENTROPY_FILES)
	$(CC) $(FLAGS) $^ -o $@$(EXT)

lz5nohuf: $(LIBDIR)/lz5_decompress.c $(LIBDIR)/lz5_compress.c $(LIBDIR)/lz5frame.o $(LIBDIR)/xxhash/xxhash.o bench.c lz5io.o lz5cli.c datagen.o
	$(CC) -DLIZARD_NO_HUFFMAN $(FLAGS) $^ -o $@$(EXT)

lz5c32: $(LIBDIR)/lz5_decompress32.o $(LIBDIR)/lz5_compress32.o $(LIBDIR)/lz5frame.c $(LIBDIR)/xxhash/xxhash.c bench.c lz5io.c lz5cli.c datagen.c $(ENTROPY_FILES)
	$(CC) -m32 $(FLAGS) $^ -o $@$(EXT)


clean:
	@$(MAKE) -C $(LIBDIR) $@ > $(VOID)
	@rm -f core *.o *.test tmp* \
        lz5$(EXT) lz5c32$(EXT) \
	@echo Cleaning completed


#-----------------------------------------------------------------------------
# make install is validated only for Linux, OSX, BSD, Hurd, Haiku and Solaris targets
#-----------------------------------------------------------------------------
ifneq (,$(filter $(shell uname),Linux Darwin GNU/kFreeBSD GNU OpenBSD FreeBSD NetBSD DragonFly SunOS Haiku))

unlz5: lz5
	ln -s lz5 unlz5

lz5cat: lz5
	ln -s lz5 lz5cat


ifneq (,$(filter $(shell uname),SunOS))
INSTALL ?= ginstall
else
INSTALL ?= install
endif

PREFIX  ?= /usr/local
DESTDIR ?=
BINDIR  ?= $(PREFIX)/bin

ifneq (,$(filter $(shell uname),OpenBSD FreeBSD NetBSD DragonFly SunOS))
MANDIR  ?= $(PREFIX)/man/man1
else
MANDIR  ?= $(PREFIX)/share/man/man1
endif

INSTALL_PROGRAM ?= $(INSTALL) -m 755
INSTALL_SCRIPT  ?= $(INSTALL) -m 755
INSTALL_MAN     ?= $(INSTALL) -m 644


install: lz5$(EXT)
	@echo Installing binaries
	@$(INSTALL) -d -m 755 $(DESTDIR)$(BINDIR)/ $(DESTDIR)$(MANDIR)/
	@$(INSTALL_PROGRAM) lz5$(EXT) $(DESTDIR)$(BINDIR)/lz5$(EXT)
	@ln -sf lz5$(EXT) $(DESTDIR)$(BINDIR)/lz5cat$(EXT)
	@ln -sf lz5$(EXT) $(DESTDIR)$(BINDIR)/unlz5$(EXT)
	@echo Installing man pages
	@$(INSTALL_MAN) -m 644 lz5.1 $(DESTDIR)$(MANDIR)/lz5.1
	@ln -sf lz5.1 $(DESTDIR)$(MANDIR)/lz5cat.1
	@ln -sf lz5.1 $(DESTDIR)$(MANDIR)/unlz5.1
	@echo lz5 installation completed

uninstall:
	@$(RM) $(DESTDIR)$(BINDIR)/lz5cat$(EXT)
	@$(RM) $(DESTDIR)$(BINDIR)/unlz5$(EXT)
	@$(RM) $(DESTDIR)$(BINDIR)/lz5$(EXT)
	@$(RM) $(DESTDIR)$(MANDIR)/lz5.1
	@$(RM) $(DESTDIR)$(MANDIR)/lz5cat.1
	@$(RM) $(DESTDIR)$(MANDIR)/unlz5.1
	@echo lz5 programs successfully uninstalled

endif
