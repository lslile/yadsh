bin_PROGRAMS = dshbak

dshbak_SOURCES = dshbak.c

man_MANS = dshbak.1

EXTRA_DIST = dshbak.1.in

CLEANFILES = dshbak.1

edit = sed \
        -e 's,@bindir\@,$(bindir),g' \
        -e 's,@VERSION\@,$(VERSION),g'

dshbak.1: $(srcdir)/dshbak.1.in
	rm -f dshbak.1
	$(edit) $(srcdir)/dshbak.1.in > dshbak.1
