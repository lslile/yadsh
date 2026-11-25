/*
 * Copyright (c) 2004-2024, Larry Lile <lile@users.sourceforge.net>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include "includes.h"
#include "parse_pssp.h"

/*
 * Runtime signal state, shared between sig_handler and execute_command.
 * These are not command-line options, so they stay here rather than in the
 * parsed invocation.
 */
volatile sig_atomic_t signaled = 0, sigcount = 0;

void
sig_handler(int signum)
{
	signaled = signum;
	if (++sigcount > 5)
	{
		static const char msg[] = "Something must be hung, exiting...\n";
		write(STDERR_FILENO, msg, sizeof(msg) - 1);
		_exit(1);
	}
}

static void
set_sig(int sig, void (*handler)(int))
{
	struct sigaction sa;

	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	sa.sa_handler = handler;
	if (sigaction(sig, &sa, NULL) < 0)
		perror("sigaction");
}

char *
clean_string(char *string)
{
	char	*s = string,
		*t;

	if (!s)
		return s;

	while (isspace(*s))
		s++;

	if (*s == '#')
		return (char *)NULL;

	t = s + strlen(s) - 1;
	while (t > s && isspace(*t))
		t--;
	*++t = '\0';

	return s;
}

#if defined(HAVE_READLINE)
/*
 * Append one history line ("<line>\n") to fd in a single write() call, so a
 * concurrent session that also opened the file O_APPEND cannot interleave
 * within the line (which would merge two commands or leave a blank entry).
 * A short write is treated as an error rather than retried, since re-issuing
 * write() under O_APPEND would re-seek to a now-moved end of file.  Returns 1
 * on success, 0 on error (errno set).
 */
static int
append_line(int fd, const char *line)
{
	size_t	 llen = strlen(line);
	size_t	 total = llen + 1;
	char	*buf = malloc(total);
	ssize_t	 n;

	if (buf == NULL)
	{
		errno = ENOMEM;
		return 0;
	}
	memcpy(buf, line, llen);
	buf[llen] = '\n';
	do {
		n = write(fd, buf, total);
	} while (n < 0 && errno == EINTR);
	free(buf);
	if (n == (ssize_t)total)
		return 1;
	if (n >= 0)
		errno = EIO;	/* partial write: cannot safely continue */
	return 0;
}

/*
 * Load prior history from an already-opened, verified stream: one command per
 * line, matching what append_line() writes.  Uses only ISO C stdio so it is
 * portable across the EL and *BSD build hosts.  Blank lines are skipped.
 */
static void
load_history_fp(FILE *fp)
{
	char	*buf = NULL;
	size_t	 len = 0, cap = 0;
	int	 c;

	while ((c = fgetc(fp)) != EOF)
	{
		if (c == '\n')
		{
			if (len > 0)
			{
				buf[len] = '\0';
				add_history(buf);
			}
			len = 0;
			continue;
		}
		if (len + 1 >= cap)
		{
			size_t	 ncap = cap ? cap * 2 : 128;
			char	*nb = realloc(buf, ncap);
			if (nb == NULL)
				break;
			buf = nb;
			cap = ncap;
		}
		buf[len++] = (char)c;
	}
	if (len > 0 && len < cap)
	{
		buf[len] = '\0';
		add_history(buf);
	}
	free(buf);
}

/*
 * Prompt-time signal handling.  Ctrl-C / Ctrl-\ at the interactive prompt must
 * not terminate dsh (that disposition exists only while a remote command runs,
 * to kill a hung remote shell - see sig_handler); like a normal shell, they
 * should abandon the half-typed line and bring back a fresh prompt.
 *
 * The handler itself does only async-signal-safe work: set a flag.  readline
 * (rl_catch_signals left on) restores the terminal and re-raises the signal to
 * this handler, then keeps running; the flag is acted on in prompt_event_hook,
 * which readline calls from normal context, where touching readline state and
 * clearing the line is safe.  No siglongjmp out of the library, no readline
 * calls from signal context.
 */
#if defined(HAVE_RL_LINE_CANCEL)
static volatile sig_atomic_t	g_prompt_intr = 0;

static void
prompt_sig(int signo)
{
	(void)signo;
	g_prompt_intr = 1;
}

static int
prompt_event_hook(void)
{
	if (g_prompt_intr)
	{
		g_prompt_intr = 0;
		rl_replace_line("", 0);	/* discard the half-typed line */
		rl_done = 1;		/* make readline return now (empty) */
	}
	return 0;
}
#endif /* HAVE_RL_LINE_CANCEL */

/*
 * Run a command locally through the user's shell: the interactive "!cmd"
 * escape.  The child inherits dsh's terminal, so the command sees a real tty;
 * the parent ignores SIGINT/SIGQUIT while waiting, so Ctrl-C reaches the child
 * rather than dsh.
 */
static void
run_local(const char *cmd)
{
	const char	*shell;
	pid_t		 pid;

	while (*cmd == ' ' || *cmd == '\t')
		cmd++;
	if (*cmd == '\0')
		return;

	shell = getenv("SHELL");
	if (shell == NULL || *shell == '\0')
		shell = "/bin/sh";

	pid = fork();
	if (pid < 0)
	{
		perror("fork");
		return;
	}
	if (pid == 0)
	{
		signal(SIGINT, SIG_DFL);
		signal(SIGQUIT, SIG_DFL);
		signal(SIGABRT, SIG_DFL);
		execl(shell, shell, "-c", cmd, (char *)NULL);
		perror(shell);
		_exit(127);
	}
	else
	{
		void	(*oldint)(int)  = signal(SIGINT, SIG_IGN);
		void	(*oldquit)(int) = signal(SIGQUIT, SIG_IGN);
		int	  status;

		while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
			;
		signal(SIGINT, oldint);
		signal(SIGQUIT, oldquit);
	}
}
#endif /* HAVE_READLINE */

int
execute_command(const struct invocation *inv, int quiet, int timeout,
    int argc, char **argv)
{
	int		 rc = 0;
	struct member	*ptr;
	int		 watchdog = timeout,
			 watchdogs_set = 0,
			 queue_watchdog = 0;

	signaled = 0;
	sigcount = 0;

	if (inv->echo)
	{
		char **cmdp = inv->remote_cmdv;
		collective_display(inv);
		printf("command: ");
		while(*cmdp)
			printf("%s ", *cmdp++);
		printf("\n");
	}

	queue_max_slots(inv->fanout);
	ptr = collective_get_next(NULL);
	while (((ptr && !signaled) || queue_load()))
	{
		time_t now = time(NULL);
		if (queue_slots() == 0)
		{
			if (watchdog > now)
			{
				debug_msg("queue watchdog expired\n");
				queue_set_watchdogs(watchdog);
				watchdogs_set = 1;
			}
			if (!watchdog)
			{
				debug_msg("setting watchdog on queue\n");
				queue_watchdog = now + watchdog;
			}
		}
		if (queue_slots() && queue_watchdog)
		{
			debug_msg("clearing watchdog on queue\n");
			queue_watchdog = 0;
		}

		while (queue_slots() > 0 && (!signaled && ptr))
		{
			struct member	*next;
			next = collective_get_next(ptr);
			if (inv->all || ptr->responding)
			{
				char *name = ptr->name;
				if (!strcmp(inv->hn_method, "reliable"))
					name = ptr->reliable;
				queue_insert(ptr, name, argc, argv);
			} else {
				debug_msg("%s not responding, dropped from collective\n",
						ptr->name);
				collective_drop_member(ptr);
			}
			ptr = next;
		}
		rc += queue_run(inv, quiet, signaled);

		if (!ptr && queue_load() && !watchdogs_set)
		{
			queue_set_watchdogs(watchdog);
			watchdogs_set = 1;
		}
	}

	if (signaled > 0 && queue_load() > 0)
	{
		debug_msg("queued processes to be killed: %d\n",
		    queue_load());
	}

	return rc;
}

int
main(int argc, char **argv)
{
	struct invocation	 inv;
	char			*wcoll = (char *)NULL,
				*prompt;
	int			 rc = 0;

	memset(&inv, 0, sizeof(inv));

	if (geteuid() == 0)
		prompt = "dsh# ";
	else
		prompt = "dsh> ";

	/* Turn argv and the environment into the canonical invocation. */
	parse_pssp(argc, argv, &inv);

	/*
	 * Resolve the working collective: the -w argument, else stdin when it
	 * is "-", else the $WCOLL file.  This wires the parsed source to the
	 * engine's collective loader; neither side looks at argv again.
	 */
	if (!inv.collective_arg && ((wcoll = getenv("WCOLL")) == NULL))
	{
		print_error("working collective not set.\n");
		exit(1);
	}

	if (inv.collective_arg)
	{
		if (*inv.collective_arg != '-')
		{
			debug_msg("loading collective from -w\n");
			collective_parse(&inv, inv.collective_arg, ',');
		} else {
			debug_msg("loading collective from stdin\n");
			collective_add_stdin(&inv, stdin);
		}
	} else {
		FILE	*file = stdin;
		debug_msg("loading collective from WCOLL=%s\n", wcoll);
		if ((file = fopen(wcoll, "r")) == NULL)
		{
			perror("fopen");
			return 1;
		}
		collective_add_file(&inv, file);
	}

	if (inv.quiet)
		return collective_display(&inv);

	set_sig(SIGINT, sig_handler);
	set_sig(SIGQUIT, sig_handler);
	signal(SIGABRT, &sig_handler);

	if (inv.verify)
	{
		char	*ping[] = { "ping", "-w", "4", NULL, NULL };
		execute_command(&inv, 1, 10, 4, ping);
	}

	if (inv.cmd_argc == 0)
	{
#if defined(HAVE_READLINE)
		char *line;
		char *histfile = NULL;
		int   hist_loaded = 0;
		{
			const char *home = getenv("HOME");
			if (home != NULL && *home != '\0')
			{
				size_t hlen = strlen(home) + sizeof("/.dsh_history");
				histfile = malloc(hlen);
				if (histfile != NULL)
					snprintf(histfile, hlen, "%s/.dsh_history", home);
			}
		}
#if defined(HAVE_READ_HISTORY)
		if (histfile != NULL)
		{
			int rfd;

			using_history();
			/*
			 * Load prior history ourselves through a verified
			 * descriptor rather than read_history(), which reopens
			 * the path.  O_NONBLOCK|O_NOFOLLOW means a FIFO cannot
			 * block us waiting for a writer and a symlink cannot
			 * redirect the read; we trust only a regular file we own.
			 */
			rfd = open(histfile, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
			if (rfd >= 0)
			{
				struct stat rst;

				if (fstat(rfd, &rst) == 0
				    && S_ISREG(rst.st_mode)
				    && rst.st_uid == geteuid()
				    && rst.st_nlink == 1)
				{
					int   fl = fcntl(rfd, F_GETFL);
					FILE *fp;

					if (fl != -1)
						(void)fcntl(rfd, F_SETFL,
						    fl & ~O_NONBLOCK);
					fp = fdopen(rfd, "r");
					if (fp != NULL)
					{
						load_history_fp(fp);
						fclose(fp);	/* closes rfd */
						rfd = -1;
					}
				}
				if (rfd >= 0)
					close(rfd);
			}
			hist_loaded = history_length;
		}
#endif /* HAVE_READ_HISTORY */
#if defined(HAVE_RL_LINE_CANCEL)
		/*
		 * readline polls this hook while waiting for input, which lets
		 * a prompt signal cancel the current line (see prompt_sig).
		 */
		rl_event_hook = prompt_event_hook;
#endif /* HAVE_RL_LINE_CANCEL */
		while (1)
		{
			char *s;

			/*
			 * At the prompt, Ctrl-C / Ctrl-\ must not terminate dsh
			 * (that disposition exists only while a remote command
			 * runs, to kill a hung remote shell - see sig_handler), so
			 * history is saved on a real exit via `exit` or Ctrl-D.
			 * With line-cancel support, prompt_sig raises a flag that
			 * prompt_event_hook clears the line on; otherwise the
			 * signals are simply ignored at the prompt.  The
			 * terminating disposition is restored below while a
			 * command runs.
			 */
#if defined(HAVE_RL_LINE_CANCEL)
			g_prompt_intr = 0;
			set_sig(SIGINT, prompt_sig);
			set_sig(SIGQUIT, prompt_sig);
#else
			set_sig(SIGINT, SIG_IGN);
			set_sig(SIGQUIT, SIG_IGN);
#endif /* HAVE_RL_LINE_CANCEL */
			set_sig(SIGABRT, SIG_DFL);

			if ((line = readline(prompt)) == NULL)
				break;	/* EOF (Ctrl-D): leave the shell */

			if ((s = clean_string(line)) == NULL)
				continue;

			signal(SIGINT,  &sig_handler);
			signal(SIGQUIT, &sig_handler);
			signal(SIGABRT, &sig_handler);
			
			if (s && strlen(s))
			{
				if (!strcmp(s, "exit"))
					break;
#if defined(HAVE_ADD_HISTORY)
				add_history(s);
#endif /* HAVE_ADD_HISTORY */
				if (*s == '!')
				{
					run_local(s + 1);
					continue;
				} else {
					inv.remote_argv[inv.remote_argc - 1] = strdup(s);
					rc = execute_command(&inv, 0, inv.timeout,
							inv.remote_argc, inv.remote_argv);
				}
			}
		}
		if (line == (char *)NULL)
			printf("\n");
#if defined(HAVE_WRITE_HISTORY)
		if (histfile != NULL)
		{
			/*
			 * Persist this session's new history lines, safely.
			 * Commands may carry secrets, so the file must be a
			 * regular file we own, with a single hard link and no
			 * group/other access.
			 *
			 * The readline history API only takes a pathname and
			 * reopens the file itself, which follows a symlink and
			 * leaves a check-to-use race.  Instead open the file
			 * ourselves with O_NOFOLLOW, verify the descriptor with
			 * fstat() BEFORE touching it, and append through that
			 * same fd - so we never touch an unintended target, and
			 * the thing we checked is the thing we write to.  O_RDWR
			 * lets us inspect the final byte; O_APPEND keeps a
			 * concurrent session's entries from being clobbered;
			 * O_NONBLOCK keeps the open from blocking on a FIFO with
			 * no reader (which we reject anyway), and is cleared once
			 * the target is confirmed to be a regular file.
			 */
			int fd = open(histfile,
			    O_RDWR | O_CREAT | O_APPEND | O_NOFOLLOW | O_NONBLOCK,
			    S_IRUSR | S_IWUSR);

			if (fd < 0)
			{
				if (errno == ELOOP)
					fprintf(stderr,
					    "dsh: not saving history: %s is a symlink\n",
					    histfile);
				else
					fprintf(stderr,
					    "dsh: could not open history %s: %s\n",
					    histfile, strerror(errno));
			}
			else
			{
				struct stat st;

				/*
				 * Verify the descriptor BEFORE altering it: a
				 * regular file we own with exactly one link
				 * (reject hard links, which O_NOFOLLOW does not
				 * catch - fchmod on one would change the linked
				 * file's permissions).
				 */
				if (fstat(fd, &st) != 0
				    || !S_ISREG(st.st_mode)
				    || st.st_uid != geteuid()
				    || st.st_nlink != 1)
				{
					fprintf(stderr,
					    "dsh: not saving history: %s is not a private regular file\n",
					    histfile);
				}
				else
				{
					HIST_ENTRY **hlist;
					int	i, werr = 0, fl;
					struct stat st2, st3;
					struct flock lk;

					/*
					 * Now safe to tighten any pre-existing
					 * group/other bits; then confirm the file
					 * really is owner-only before writing.
					 */
					(void)fchmod(fd, S_IRUSR | S_IWUSR);
					if (fstat(fd, &st2) != 0
					    || (st2.st_mode & (S_IRWXG | S_IRWXO)) != 0)
					{
						fprintf(stderr,
						    "dsh: not saving history: could not secure %s\n",
						    histfile);
					}
					else
					{
						/* Regular file confirmed: writes can block now. */
						fl = fcntl(fd, F_GETFL);
						if (fl != -1)
							(void)fcntl(fd, F_SETFL,
							    fl & ~O_NONBLOCK);

						/*
						 * Serialize the newline repair and the
						 * appends against other sessions with an
						 * exclusive lock on the whole file, so two
						 * sessions cannot both repair the final
						 * line or interleave entries.  Best effort:
						 * if locking is unsupported we still save.
						 * The lock is released when fd is closed.
						 */
						lk.l_type = F_WRLCK;
						lk.l_whence = SEEK_SET;
						lk.l_start = 0;
						lk.l_len = 0;
						(void)fcntl(fd, F_SETLKW, &lk);

						/*
						 * Re-stat under the lock: another session
						 * may have appended between our open and
						 * acquiring the lock, so the size we check
						 * for a trailing newline must be current.
						 */
						if (fstat(fd, &st3) != 0)
							st3 = st2;

						/*
						 * If the existing file does not end
						 * in a newline, terminate it first so
						 * our first appended command is not
						 * merged onto a partial final line.
						 */
						if (st3.st_size > 0)
						{
							char	last;
							ssize_t pr = pread(fd, &last, 1,
							    (off_t)st3.st_size - 1);
							if (pr == 1 && last != '\n')
							{
								ssize_t wn;
								do {
									wn = write(fd, "\n", 1);
								} while (wn < 0 && errno == EINTR);
								if (wn != 1)
									werr = (wn < 0) ? errno : EIO;
							}
						}

						hlist = history_list();
						if (hist_loaded < 0)
							hist_loaded = 0;
						if (werr == 0 && hlist != NULL)
						{
							for (i = hist_loaded;
							     i < history_length && hlist[i] != NULL;
							     i++)
							{
								const char *l = hlist[i]->line;
								if (l == NULL)
									l = "";
								if (!append_line(fd, l))
								{
									werr = errno;
									break;
								}
							}
						}
						if (werr != 0)
							fprintf(stderr,
							    "dsh: could not save history to %s: %s\n",
							    histfile, strerror(werr));
					}
				}
				close(fd);
			}
		}
#endif /* HAVE_WRITE_HISTORY */
		free(histfile);
#else
		printf("readline not availble\n");
#endif /* HAVE_READLINE */
	} else {
		rc = execute_command(&inv, 0, inv.timeout,
		    inv.remote_argc, inv.remote_argv);
	}

	set_sig(SIGINT, SIG_DFL);
	set_sig(SIGQUIT, SIG_DFL);
	set_sig(SIGABRT, SIG_DFL);

	invocation_free(&inv);
	free_collective();

	return rc;
}
