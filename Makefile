# ################################################################
# LZ5 - Makefile
# Copyright (C) Yann Collet 2011-2015
# All rights reserved.
#
# BSD license
# Redistribution and use in source and binary forms, with or without modification,
# are permitted provided that the following conditions are met:
#
# * Redistributions of source code must retain the above copyright notice, this
#   list of conditions and the following disclaimer.
#
# * Redistributions in binary form must reproduce the above copyright notice, this
#   list of conditions and the following disclaimer in the documentation and/or
#   other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
# ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
# ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# You can contact the author at :
#  - LZ5 source repository : https://github.com/inikep/lz5
# ################################################################

DESTDIR?=
PREFIX ?= /usr/local

LIBDIR ?= $(PREFIX)/lib
INCLUDEDIR=$(PREFIX)/include
PRGDIR  = programs
LZ5DIR  = lib


# Define nul output
VOID = /dev/null


.PHONY: default all lib lz5 clean test examples

default: lz5

all: lib lz5

lib:
	@$(MAKE) -C $(LZ5DIR) all

lz5:
	@$(MAKE) -C $(PRGDIR)
	@cp $(PRGDIR)/lz5 .

clean:
	@$(MAKE) -C $(PRGDIR) $@ > $(VOID)
	@$(MAKE) -C $(LZ5DIR) $@ > $(VOID)
	@$(MAKE) -C examples $@ > $(VOID)
	@$(RM) lz5
	@echo Cleaning completed


#------------------------------------------------------------------------
#make install is validated only for Linux, OSX, kFreeBSD, Hurd and
#FreeBSD targets
ifneq (,$(filter $(shell uname),Linux Darwin GNU/kFreeBSD GNU FreeBSD MSYS_NT-10.0))

install:
	@$(MAKE) -C $(LZ5DIR) $@
	@$(MAKE) -C $(PRGDIR) $@

uninstall:
	@$(MAKE) -C $(LZ5DIR) $@
	@$(MAKE) -C $(PRGDIR) $@

travis-install:
	sudo $(MAKE) install

test:
	$(MAKE) -C $(PRGDIR) test

cmake:
	@cd cmake_unofficial; cmake CMakeLists.txt; $(MAKE)

gpptest: clean
	$(MAKE) all CC=g++ CFLAGS="-O3 -I../lib -Wall -Wextra -Wundef -Wshadow -Wcast-align -Werror"

clangtest: clean
	CFLAGS="-O3 -Werror -Wconversion -Wno-sign-conversion" $(MAKE) all CC=clang

sanitize: clean
	CFLAGS="-O3 -g -fsanitize=undefined" $(MAKE) test CC=clang FUZZER_TIME="-T1mn" NB_LOOPS=-i1

staticAnalyze: clean
	CFLAGS=-g scan-build --status-bugs -v $(MAKE) all

armtest: clean
	CFLAGS="-O3 -Werror" $(MAKE) -C $(LZ5DIR) all CC=arm-linux-gnueabi-gcc
	CFLAGS="-O3 -Werror" $(MAKE) -C $(PRGDIR) bins CC=arm-linux-gnueabi-gcc

examples:
	$(MAKE) -C $(LZ5DIR)
	$(MAKE) -C $(PRGDIR) lz5
	$(MAKE) -C examples test

endif
