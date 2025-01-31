MAINTAINERCLEANFILES = aclocal.m4 compile config.guess \
		config.sub config.h.in configure depcomp install-sh \
		ltmain.sh Makefile.in missing

DISTCLEANFILES = *.in

noinst_LIBRARIES = libotikiosk.a

libotikiosk_a_SOURCES = src/libotikiosk.c src/mjson.c src/kiosk_commands.c src/ot_log.cpp
 
libotikiosk_a_CFLAGS = -g -O0 -D_GNU_SOURCE -I.
libotikiosk_a_CXXFLAGS = -g -O0 -D_GNU_SOURCE -I.

AM_CFLAGS = @libotikiosk_a_CFLAGS@
AM_CXXFLAGS = @libotikiosk_a_CXXFLAGS@

CLEANFILES = *~ *.o
