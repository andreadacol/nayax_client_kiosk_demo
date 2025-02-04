ACLOCAL_AMFLAGS=-I m4

MAINTAINERCLEANFILES = aclocal.m4 compile config.guess \
		config.sub config.h.in configure depcomp install-sh \
		ltmain.sh Makefile.in missing

DISTCLEANFILES = *.in

bin_PROGRAMS = otiKioskDemo
otiKioskDemo_SOURCES = otiKioskDemo.c
otiKioskDemo_CFLAGS = -g -O0 -pthread -I../libotikiosk
otiKioskDemo_LDFLAGS = -pthread -lwebsockets

AM_CFLAGS = @otiKioskDemo_CFLAGS@
AM_LDFLAGS =  @otiKioskDemo_LDFLAGS@

otiKioskDemo_LDADD = ../libotikiosk/libotikiosk.a -lstdc++ -lwebsockets

CLEANFILES = *~ *.o
