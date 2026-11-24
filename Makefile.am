SUBDIRS = dsh dshbak contrib
		
EXTRA_DIST = autogen.sh dsh.spec git-version-gen

DISTCHECK_CONFIGURE_FLAGS = \
	CFLAGS="-Wall -pedantic -Wshadow -Wpointer-arith -Wcast-align -Wmissing-prototypes -Wmissing-declarations -Wnested-externs -Winline -W -Werror"

# Bake the resolved version into the dist tarball so a re-bootstrapped tarball
# (no .git) still reports its release version via git-version-gen.
dist-hook:
	echo '$(VERSION)' > '$(distdir)/.tarball-version'
