## $Id$

INCLUDES =	$(RL_CFLAGS)

bin_PROGRAMS =	dsh

dsh_SOURCES =	collective.c \
		debug.c \
		dsh.c \
		print.c \
		proc.c \
		queue.c	\
		resolv.c \
		usage.c

dsh_LDADD =	$(RL_LIBS)

noinst_HEADERS = acconfig.h \
		collective.h \
		config.h \
		debug.h \
		dsh.h \
		includes.h\
		print.h \
		proc.h \
		queue.h \
		resolv.h
		

EXTRA_DIST = bootstrap dsh.spec
