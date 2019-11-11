# Makefile  -  How to build NATO STANAG 5066 Annex A, B, and C daemon
# Copyright (c) 2006 Sampo Kellomaki (sampo@iki.fi), All Rights Reserved.
# This is confidential unpublished proprietary source code of the author.
# NO WARRANTY, not even implied warranties. Contains trade secrets.
# Distribution prohibited unless authorized in writing. See file COPYING.
# $Id: Makefile,v 1.18 2006/06/18 21:06:30 sampo Exp $
# Build so far only tested on Linux. This makefile needs gmake-3.78 or newer.
#
# Usage:   make              # Linux
#          make TARGET=sol8  # Sparc Solaris 8 native
#          make TARGET=xsol8 # Sparc Solaris 8 cross compile (on Linux?)

vpath %.c ../s5066d
vpath %.h ../s5066d

REL=0.5
VERSION=0x000005

CC=gcc
LD=gcc
CDIR=-Iserial_sync
CDEF=-D_REENTRANT -DDEBUG -DS5066D=$(VERSION) -DTEST_PING -DREL="\"$(REL)\""
#CDEF=-D_REENTRANT -DDEBUG -DS5066D=$(VERSION) -DCOMPILED_DATE=`date +%s` -Iserial_sync
LIBS=-lpthread

# Test boxes Nito
REMOTEHOST=quebec.cellmail.com
REMOTEDIR=w/src/s5066/

ifeq ($(TARGET),xsol8)

# Cross compilation for Solaris 8 target (on Linux host) Invoke as `make PLATFORM=xsol8'
# You must have the cross compiler installed in /apps/gcc/sol8 and in path. Similarily
# the cross binutils must be in path.
#    export PATH=/apps/gcc/sol8/bin:/apps/binutils/sol8/bin:$PATH
#    make TARGET=xsol8
SYSROOT=/apps/gcc/sol8/sysroot
CROSS_COMPILE=1
CC=sparc-sun-solaris2.8-gcc
LD=sparc-sun-solaris2.8-gcc
CDEF+=-DSUNOS -DBYTE_ORDER=4321 -DBIG_ENDIAN=4321
LIBS+=-lxnet -lsocket

else
ifeq ($(TARGET),sol8)

# Flags for Solaris 8 native compile (BIG_ENDIAN BYTE_ORDER)
#    make TARGET=sol8
CDEF+=-DSUNOS -DBYTE_ORDER=4321 -DBIG_ENDIAN=4321
LIBS+=-lxnet -lsocket

else

# Flags for Linux 2.6 native compile
#    make
CDEF+=-DLINUX

endif
endif

CFLAGS=-c -g -O -fmessage-length=0 -Wno-unused-label -Wno-unknown-pragmas -fno-strict-aliasing $(CDEF) $(CDIR)

S5066D_OBJ=s5066d.o hiios.o hiwrite.o hiread.o util.o license.o sis.o dts.o smtp.o http.o testping.o serial_sync.o globalcounter.o

s5066d: $(S5066D_OBJ)
	$(LD) $(LDFLAGS) -o s5066d $(S5066D_OBJ) $(LIBS)

synccat: serial_sync.o globalcounter.o synccat.o
	$(LD) $(LDFLAGS) -o synccat $^ $(LIBS)

iocat: serial_sync.o globalcounter.o iocat.o serial/dialout.o license.o
	$(LD) $(LDFLAGS) -o iocat $^ $(LIBS)

sizeof:
	$(CC) -o sizeof sizeof.c

license.c: COPYING_sis5066_h
	printf 'char* license = "' >license.c
	printf 'Copyright (c) 2006 Sampo Kellomaki (sampo@iki.fi), All Rights Reserved.\\n\\' >>license.c
	sed -e 's/$$/\\n\\/' COPYING >>license.c
	sed -e 's/$$/\\n\\/' COPYING_sis5066_h >>license.c
	echo '";' >>license.c

cleaner: clean
	rm -rf dep

clean:
	rm -rf *.o s5066d sizeof *~ .*~ .\#* license.c

dist: cleaner
	rm -rf open5066-$(REL)
	mkdir open5066-$(REL)
	cp *.[hc] Makefile COPYING* setup1.sh open5066-$(REL)
	cp open5066.pd open5066-$(REL)/README.open5066
	tar czf open5066-$(REL).tgz open5066-$(REL)

help:
	@echo "To compile for Linux 2.6: make"
	@echo "To compile for Solaris 8: make TARGET=sol8"
	@echo "To compile for Sparc Solaris 8 with cross compiler:"
	@echo '  PATH=/apps/gcc/sol8/bin:/apps/binutils/sol8/bin:$$PATH make TARGET=xsol8'
	@echo "Following make targets are available:"
	@grep '^[a-z-]:' Makefile

dep: license.c
	$(CC) $(CDEF) $(CDIR) -MM $(S5066D_OBJ:.o=.c) > deps

-include deps

test:
	cd t; $(MAKE) test

#EOF
