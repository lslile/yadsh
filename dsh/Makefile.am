
AM_CPPFLAGS =	$(RL_CFLAGS)

AM_CFLAGS =	$(SANITIZE_FLAGS)
AM_LDFLAGS =	$(SANITIZE_FLAGS)

# T14: one dialect binary per build (dsh.$(DIALECT)); a convenience `dsh'
# symlink points at it (install-exec-hook below).  The engine, queue, output
# and the shared invocation module are dialect-blind and compile into every
# binary; each dialect adds its own parser (parse_DIALECT.c, defining the
# neutral parse_invocation()) and its own usage text (usage_DIALECT.c).  This
# build is pssp; the dsm grammar arrives with --enable-dialect=dsm (T14b).
COMMON_SOURCES = \
		collective.c \
		debug.c \
		dsh.c \
		invocation.c \
		print.c \
		proc.c \
		queue.c	\
		resolv.c

if DIALECT_DSM
bin_PROGRAMS =	dsh.dsm
dsh_dsm_SOURCES = $(COMMON_SOURCES) parse_dsm.c usage_dsm.c
dsh_dsm_LDADD = $(RL_LIBS)
man_MANS = dsh.dsm.1
CLEANFILES = dsh.dsm.1
else
bin_PROGRAMS =	dsh.pssp
dsh_pssp_SOURCES = $(COMMON_SOURCES) parse_pssp.c usage_pssp.c
dsh_pssp_LDADD = $(RL_LIBS)
man_MANS = dsh.pssp.1
CLEANFILES = dsh.pssp.1
endif

noinst_HEADERS = \
		collective.h \
		debug.h \
		dsh.h \
		includes.h\
		invocation.h \
		parse.h \
		print.h \
		proc.h \
		queue.h \
		resolv.h \
		usage.h

# Both dialects' man-page templates ship in every tarball.  The parser and
# usage source of the dialect NOT built this time go in EXTRA_DIST too, so
# `make dist' from either configuration yields a tree that builds both.
EXTRA_DIST = dsh.pssp.1.in dsh.dsm.1.in
if DIALECT_DSM
EXTRA_DIST += parse_pssp.c usage_pssp.c
else
EXTRA_DIST += parse_dsm.c usage_dsm.c
endif

edit = sed \
        -e 's,@bindir\@,$(bindir),g' \
        -e 's,@VERSION\@,$(VERSION),g'

dsh.pssp.1: Makefile $(srcdir)/dsh.pssp.1.in
	rm -f dsh.pssp.1
	$(edit) $(srcdir)/dsh.pssp.1.in > dsh.pssp.1

dsh.dsm.1: Makefile $(srcdir)/dsh.dsm.1.in
	rm -f dsh.dsm.1
	$(edit) $(srcdir)/dsh.dsm.1.in > dsh.dsm.1

# Convenience symlinks: `dsh' -> the dialect binary, `dsh.1' -> its man page.
# Default dialect is pssp so `dsh' and `man dsh' keep working as before.
#
# Honor --program-prefix/--program-suffix/--program-transform-name: Automake
# installs dsh.$(DIALECT) and its man page through $(transform), so the link
# name and its target pass through $(transform) too.  $(transform) defaults to
# the identity s,x,x, so the common (no-transform) case links dsh -> dsh.pssp.
#
# The transform is applied by Automake at make time with autoconf's own
# escaping, which a hook cannot always reproduce byte-for-byte (backslashes in
# a custom --program-transform-name escape differently here than in Automake's
# install rule).  So the hook does not trust its computed name blindly: it
# creates the link only when (a) the link and target names differ -- a
# collapsing transform means the dialect binary is already installed as `dsh',
# nothing to link -- and (b) the computed target actually exists on disk under
# that name.  If our computed name diverges from what Automake installed, (b)
# fails and we skip rather than create a broken or clobbering link.  Likewise
# uninstall only removes a name that is actually our symlink.
#
# (A pathological --program-transform-name that rewrites the ".pssp" suffix
# itself, e.g. 's/\.pssp$$//', is self-defeating with Automake regardless of
# this hook: autoconf escapes it differently for install vs uninstall, so
# Automake's own generated rules install dsh.pssp but fail to remove it.  Use
# a form that does not mangle, e.g. 's|dsh[.]pssp|dsh|', to rename the binary.)
install-exec-hook:
	@link=`echo dsh | sed '$(transform)'`; \
	target=`echo dsh.$(DIALECT) | sed '$(transform)'`; \
	if test "x$$link" = "x$$target"; then \
	    echo " dsh convenience link skipped (dialect binary is already installed as $$link)"; \
	elif test ! -f "$(DESTDIR)$(bindir)/$$target$(EXEEXT)"; then \
	    echo " dsh convenience link skipped (installed binary name not determinable under transform)"; \
	else \
	    echo " cd $(DESTDIR)$(bindir) && rm -f $$link$(EXEEXT) && $(LN_S) $$target$(EXEEXT) $$link$(EXEEXT)"; \
	    cd "$(DESTDIR)$(bindir)" && rm -f "$$link$(EXEEXT)" && \
	        $(LN_S) "$$target$(EXEEXT)" "$$link$(EXEEXT)"; \
	fi

install-data-hook:
	@link=`echo dsh | sed '$(transform)'`; \
	target=`echo dsh.$(DIALECT) | sed '$(transform)'`; \
	if test "x$$link" = "x$$target"; then \
	    echo " dsh.1 convenience link skipped (dialect man page is already installed as $$link.1)"; \
	elif test ! -f "$(DESTDIR)$(man1dir)/$$target.1"; then \
	    echo " dsh.1 convenience link skipped (installed man page name not determinable under transform)"; \
	else \
	    echo " cd $(DESTDIR)$(man1dir) && rm -f $$link.1 && $(LN_S) $$target.1 $$link.1"; \
	    cd "$(DESTDIR)$(man1dir)" && rm -f "$$link.1" && \
	        $(LN_S) "$$target.1" "$$link.1"; \
	fi

# Remove the convenience links only when they are still our own: a symlink
# that points at this dialect's target.  If another installation (e.g. a dsm
# build, or a manual switch) has repointed `dsh' to its own binary, leave it
# be so uninstalling this package does not break that one.
uninstall-hook:
	@link=`echo dsh | sed '$(transform)'`; \
	target=`echo dsh.$(DIALECT) | sed '$(transform)'`; \
	if test "x$$link" != "x$$target"; then \
	    lp="$(DESTDIR)$(bindir)/$$link$(EXEEXT)"; \
	    if test -L "$$lp" && test "`readlink "$$lp"`" = "$$target$(EXEEXT)"; then \
	        rm -f "$$lp"; \
	    fi; \
	    mp="$(DESTDIR)$(man1dir)/$$link.1"; \
	    if test -L "$$mp" && test "`readlink "$$mp"`" = "$$target.1"; then \
	        rm -f "$$mp"; \
	    fi; \
	fi
