
dnl Process this file with autoconf to produce a configure script.
AC_INIT([dsh], [m4_esyscmd_s([sh ./git-version-gen .tarball-version])])
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE([foreign])

dnl Checks for programs.
AC_PROG_AWK
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_LN_S

dnl Optional sanitizer builds (developer / CI use).  --enable-asan and
dnl --enable-ubsan add the sanitizer flags to this project's own targets
dnl only, leaving the user's CFLAGS intact.  ASan and ThreadSanitizer are
dnl mutually exclusive; TSan arrives with the threaded engine (T9).
AC_ARG_ENABLE([asan],
  [AS_HELP_STRING([--enable-asan], [build with AddressSanitizer (-fsanitize=address)])],
  [enable_asan=$enableval], [enable_asan=no])
AC_ARG_ENABLE([ubsan],
  [AS_HELP_STRING([--enable-ubsan], [build with UndefinedBehaviorSanitizer (-fsanitize=undefined)])],
  [enable_ubsan=$enableval], [enable_ubsan=no])
SANITIZE_FLAGS=
AS_IF([test "x$enable_asan" = xyes],
  [SANITIZE_FLAGS="$SANITIZE_FLAGS -fsanitize=address"])
AS_IF([test "x$enable_ubsan" = xyes],
  [SANITIZE_FLAGS="$SANITIZE_FLAGS -fsanitize=undefined"])
AS_IF([test -n "$SANITIZE_FLAGS"],
  [SANITIZE_FLAGS="$SANITIZE_FLAGS -fno-omit-frame-pointer -g"])
AC_SUBST([SANITIZE_FLAGS])

dnl Checks for libraries.
AC_SEARCH_LIBS(inet_aton, resolv)
AC_SEARCH_LIBS(gethostbyname, nsl)
AC_SEARCH_LIBS(basename, gen)

dnl ******************************
dnl readline checking
dnl ******************************
msg_readline=no
AC_ARG_WITH(readline,      [  --with-readline         use readline [default=yes]])
if test "x$with_readline" != "xno"; then
LIBS="$LIBS -lreadline"

	dnl Let's check all the possible places for readline
	for termlib in ncurses curses termcap terminfo termlib ; do
		AC_CHECK_LIB(${termlib}, tputs, [LIBS="-l${termlib} $LIBS"; break])
		LIBS="$ac_save_LIBS"
	done

	AC_MSG_CHECKING(for readline)
	AC_TRY_LINK(,[(void)readline(0);], RL_LIBS="-lreadline",
		AC_TRY_LINK(,[(void)readline(0);], 
		RL_LIBS="-lreadline -l${termlib}"))

	if test "x$RL_LIBS" = "x"; then
		AC_MSG_RESULT(not found)
	else
		AC_MSG_RESULT(found)
	fi
fi

if test "x$RL_LIBS" != "x"; then
	AC_MSG_CHECKING(for basic readline functions)

	ac_save_LIBS="$LIBS"

	LIBS="$LIBS $RL_LIBS"
	AC_TRY_LINK([
#include <stdio.h>
#include <readline/readline.h>], [
char *line; 
line = readline("prompt");], 
	[AC_TRY_LINK([
#include <stdio.h>
#include <readline/history.h>], [
add_history("test");],
	[msg_readline=partial])])

	LIBS="$ac_save_LIBS"

	if test "xmsg_readline" = "xno"; then
		AC_MSG_RESULT(no)
	else
		AC_MSG_RESULT(yes)
	fi
fi
if test "x$msg_readline" != "xno"; then
	AC_MSG_CHECKING(for extra readline functions)

	ac_save_LIBS="$LIBS"

	LIBS="$LIBS $RL_LIBS"
	AC_TRY_LINK([
#include <stdio.h>
#include <readline/readline.h>], [
rl_callback_read_char();], 
	[AC_TRY_LINK([
#include <stdio.h>
#include <readline/readline.h>], [
static void execline(char *line)
{}
rl_callback_handler_install("prompt", execline);],
	[AC_TRY_LINK([
#include <stdio.h>
#include <readline/readline.h>], [
rl_callback_handler_remove();],
	[AC_TRY_LINK([
#include <stdio.h>
#include <readline/readline.h>], [
rl_forced_update_display();],
	[msg_readline=full])])])])

	LIBS="$ac_save_LIBS"

	if test "x$msg_readline" != "xfull"; then
		AC_MSG_RESULT(no)
	else
		AC_MSG_RESULT(yes)
	fi
fi

if test "x$msg_readline" = "xpartial"; then
	AC_DEFINE(HAVE_READLINE, 1, [Define if we have readline support])
fi
if test "x$msg_readline" = "xfull"; then
	AC_DEFINE(HAVE_READLINE, 1, [Define if we have readline support])
	AC_DEFINE(HAVE_READLINE_EXTRA, 1, [Define if we have extra readline support])
fi

dnl The interactive prompt cancels the current line on Ctrl-C using
dnl rl_event_hook / rl_done / rl_replace_line.  A thin readline (or an
dnl editline shim) may lack these, so probe for them; without them the
dnl prompt simply ignores Ctrl-C instead (see dsh.c).
if test "x$msg_readline" != "xno"; then
	AC_MSG_CHECKING(for readline line-cancel support)
	ac_save_LIBS="$LIBS"
	LIBS="$LIBS $RL_LIBS"
	AC_TRY_LINK([
#include <stdio.h>
#include <readline/readline.h>], [
	rl_event_hook = 0;
	rl_done = 0;
	rl_replace_line("", 0);],
	[AC_DEFINE(HAVE_RL_LINE_CANCEL, 1,
		[Define if readline provides rl_event_hook, rl_done and rl_replace_line])
	 AC_MSG_RESULT(yes)],
	[AC_MSG_RESULT(no)])
	LIBS="$ac_save_LIBS"
fi
AC_SUBST(RL_CFLAGS)
AC_SUBST(RL_LIBS)

dnl
dnl begin --with-rsh
dnl

# What remote shell transport should dsh default to using?
AC_ARG_WITH(
  [rsh],
  [  --with-rsh		  Specify the remote shell program (default rsh)], ,
  [with_rsh="rsh ssh"])

if test no = "$with_rsh"; then
  AC_MSG_WARN([Failed to find usable remote shell. Using 'rsh'.])
  with_rsh=rsh
elif test yes = "$with_rsh"; then
  # Make --with-rsh mean the same thing as --with-rsh=rsh
  with_rsh=rsh
fi

if echo $with_rsh |grep ^/ >/dev/null; then
  # If $with_rsh is an absolute path, issue a warning if the executable
  # doesn't exist or isn't usable, but then trust the user and use it
  # regardless
  with_default_rsh=$with_rsh
  AC_MSG_CHECKING([for a remote shell])
  if ! test -f $with_rsh \
      || ! test -x $with_rsh; then
    # warn the user that they may encounter problems
    AC_MSG_WARN([$with_rsh is not a path to an executable file])
  fi
else
  # Search for a remote shell
  dnl AC_CHECK_PROGS([with_default_rsh], [$with_rsh], "rsh")
  AC_PATH_PROGS([with_default_rsh], [$with_rsh], , [$PATH])
fi

AC_DEFINE_UNQUOTED(
  [RSH_DFLT], ["$with_default_rsh"],
  [The default remote shell to use.])
RSH_DFLT=$with_default_rsh
AC_SUBST(RSH_DFLT)
dnl
dnl end --with-rsh
dnl

dnl
dnl begin --with-ssh
dnl

# What remote secure shell transport should dsh default to using?
AC_ARG_WITH(
  [ssh],
  [  --with-ssh		  Specify the remote secure shell program (default ssh)], ,
  [with_ssh="ssh"])

if test no = "$with_ssh"; then
  AC_MSG_WARN([Failed to find usable remote shell. Using 'ssh'.])
  with_ssh=ssh
elif test yes = "$with_ssh"; then
  # Make --with-ssh mean the same thing as --with-ssh=ssh
  with_ssh=ssh
fi

if echo $with_ssh |grep ^/ >/dev/null; then
  # If $with_ssh is an absolute path, issue a warning if the executable
  # doesn't exist or isn't usable, but then trust the user and use it
  # regardless
  with_default_ssh=$with_ssh
  AC_MSG_CHECKING([for a remote shell])
  if ! test -f $with_ssh \
      || ! test -x $with_ssh; then
    # warn the user that they may encounter problems
    AC_MSG_WARN([$with_ssh is not a path to an executable file])
  fi
else
  # Search for a remote shell
  dnl AC_CHECK_PROGS([with_default_ssh], [$with_ssh], "ssh")
  AC_PATH_PROGS([with_default_ssh], [$with_ssh], , [$PATH])
fi

AC_DEFINE_UNQUOTED(
  [SSH_DFLT], ["$with_default_ssh"],
  [The default secure shell to use.])
SSH_DFLT=$with_default_ssh
AC_SUBST(SSH_DFLT)
dnl
dnl end --with-ssh
dnl

# What is the default for DSHPATH?
AC_ARG_WITH(dshpath,
	[  --with-dshpath=PATH	  The default value for DSHPATH],
	[with_dshpath=$withval],
	[with_dshpath=''])
if test "$with_dshpath" != "yes" && test -z "$with_dshpath"; then
	with_dshpath=''
else
	AC_MSG_RESULT([Setting default DSHPATH to $with_dshpath])
	AC_DEFINE_UNQUOTED(DSHPATH_DFLT,"$with_dshpath", Define to specify the default DSHPATH.)
fi

dnl Checks for header files.
AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS(fcntl.h sys/time.h string.h unistd.h netdb.h readline/readline.h readline/history.h)

dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_PID_T
AC_HEADER_TIME

dnl Checks for library functions.
AC_FUNC_MMAP
AC_TYPE_SIGNAL
AC_FUNC_VPRINTF
AC_CHECK_FUNCS(unsetenv putenv select strdup strerror readline add_history read_history write_history append_history)

AC_EXEEXT
AC_CONFIG_FILES([Makefile dsh/Makefile dshbak/Makefile contrib/Makefile tests/Makefile dsh.spec])
AC_OUTPUT
