## $Id$

bin_SCRIPTS = dshbak

EXTRA_DIST = dshbak.pl

BUILT_SOURCES = dshbak

CLEANFILES = dshbak

## This is a temporary placeholder until I either
## setup the Makefile.PL magic or autoconf the perl
## bits
dshbak: Makefile dshbak.pl
	rm -f dshbak
	cp $(srcdir)/dshbak.pl dshbak.tmp
	mv dshbak.tmp dshbak
	chmod +x dshbak
