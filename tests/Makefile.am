# Test harness: run with `make check`. Tests are plain POSIX-sh scripts that
# exit 0 (pass) / non-zero (fail); automake's test runner aggregates them.

# The configured dialect decides which dsh binary is built and which dsh tests
# apply: the PSSP tests drive -w / RCMD_PGM, the DSM test drives -n / -r.
# dshbak.test is dialect-neutral and always runs.
if DIALECT_DSM
dsh_binary = dsh.dsm
dialect_TESTS = dsh-dsm.test
else
dsh_binary = dsh.pssp
dialect_TESTS = dsh.test dsh-interactive.test
endif

AM_TESTS_ENVIRONMENT = \
	DSH='$(abs_top_builddir)/dsh/$(dsh_binary)'; export DSH; \
	DSHBAK='$(abs_top_builddir)/dshbak/dshbak'; export DSHBAK; \
	FAKE_RCMD='$(abs_srcdir)/fake-rcmd'; export FAKE_RCMD; \
	DSHPATH=''; export DSHPATH; \
	unset DSH_COLLATE FANOUT DSH_TIMEOUT HN_METHOD DSH_DEBUG WCOLL \
	      RCMD_PGM DSH_REMOTE_CMD \
	      DSH_NODE_LIST DSH_NODEGROUP_PATH DSH_NODE_RSH DSH_NODE_OPTS \
	      DSH_FANOUT DSH_SYNTAX DSH_PATH;

TESTS = dshbak.test $(dialect_TESTS)

# Ship every dialect's test scripts in the tarball regardless of which dialect
# was configured, so `make dist' from either build yields a tree that can
# `make check' both.
EXTRA_DIST = dshbak.test dsh.test dsh-interactive.test dsh-dsm.test fake-rcmd
