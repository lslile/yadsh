# Test harness: run with `make check`. Tests are plain POSIX-sh scripts that
# exit 0 (pass) / non-zero (fail); automake's test runner aggregates them.

AM_TESTS_ENVIRONMENT = \
	DSH='$(abs_top_builddir)/dsh/dsh.pssp'; export DSH; \
	DSHBAK='$(abs_top_builddir)/dshbak/dshbak'; export DSHBAK; \
	FAKE_RCMD='$(abs_srcdir)/fake-rcmd'; export FAKE_RCMD; \
	DSHPATH=''; export DSHPATH; \
	unset DSH_COLLATE FANOUT DSH_TIMEOUT HN_METHOD DSH_DEBUG WCOLL \
	      RCMD_PGM DSH_REMOTE_CMD;

TESTS = \
	dshbak.test \
	dsh.test \
	dsh-interactive.test

EXTRA_DIST = $(TESTS) fake-rcmd
