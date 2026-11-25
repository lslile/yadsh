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

/*
 * The IBM PSSP dsh command-line and environment parser.  It turns argv and
 * the environment into a struct invocation and nothing else: the engine,
 * queue and output code read that struct and never look at argv or getenv.
 * Keeping the dialect here is what lets a second syntax be added later as a
 * second parser filling the same struct.
 */

#include "includes.h"
#include "parse_pssp.h"

#if !defined(RSH_DFLT)
#define RSH_DFLT "/usr/bin/rsh"
#endif /* !defined RSH_DFLT */

#if !defined(SSH_DFLT)
#define SSH_DFLT "/usr/bin/ssh"
#endif /* !defined SSH_DFLT */

#if !defined(DSHPATH_DFLT)
#define DSHPATH_DFLT "/usr/ucb:/bin:/usr/bin:."
#endif /* !defined DSHPATH_DFLT */

#define MAX_FANOUT(x)	((x - 3) / 3)
#define TIMEOUT		180

/* Set from -Y, consumed by the (NIS) resolver path; preserved verbatim. */
static char	*__ypdomain = NULL;

/*
 * Append one argument to inv->remote_argv, keeping it NULL-terminated.  A
 * NULL argument advances the count without storing anything, reserving the
 * slot (used for the per-member hostname, which queue_insert fills in).
 */
static void
push_remote_argv(struct invocation *inv, char *argument)
{
	char **tmp;

	/* +2 because we keep remote_argv NULL terminated. */
	if ((tmp = (char **)malloc(sizeof(char *) * (inv->remote_argc + 2))) == NULL)
	{
		perror("Unable to allocate remote arguments - malloc");
		exit(1);
	}
	memset(tmp, '\0', sizeof(char *) * (inv->remote_argc + 2));

	if (inv->remote_argv)
	{
		memcpy(tmp, inv->remote_argv, inv->remote_argc * sizeof(char *));
		free(inv->remote_argv);
	}
	inv->remote_argv = tmp;

	if (argument)
		inv->remote_argv[inv->remote_argc] = strdup(argument);

	inv->remote_argc++;
}

void
parse_pssp(int argc, char **argv, struct invocation *inv)
{
	int		 ch,
			 i,
			 max_fanout = 0;
	char		*fanout,
			*dsh_remote_cmd = RSH_DFLT,
			*rcmd_pgm = "rsh",
			*dshpath = (char *)NULL,
			*envp;

	/* Flags that stay local to the parser: they only shape remote_argv. */
	unsigned int	 tflag = 1,	/* -t: 1 = csh syntax, 0 = sh syntax */
			 dflag = 0,	/* -d: remote -f */
			 Dflag = 0;	/* -D: remote -F */
	char		*lflag = (char *)NULL,	/* -l login name */
			*oflag = (char *)NULL,	/* -o remote flags */
			*wflag = (char *)NULL,	/* -w collective spec */
			*Yflag = (char *)NULL;	/* -Y NIS domain */

#if defined(RLIMIT_NOFILE) || defined(RLIMIT_NPROC)
	struct rlimit	 rl;
#endif /* defined(RLIMIT_NOFILE) || defined(RLIMIT_NPROC) */

	if (getenv("DSH_DEBUG") != NULL)
		set_debug(1);
	debug_msg("DSH_DEBUG\n");

	/* Defaults the environment and options then override. */
	inv->fanout = 64;
	inv->timeout = TIMEOUT;

	rcmd_pgm = (envp = getenv("RCMD_PGM")) ? envp : "rsh";
	if (!strcmp(rcmd_pgm, "rsh"))
		dsh_remote_cmd = (envp = getenv("DSH_REMOTE_CMD")) ? envp : RSH_DFLT;
	else if (!strcmp(rcmd_pgm, "secrshell"))
		dsh_remote_cmd = (envp = getenv("DSH_REMOTE_CMD")) ? envp : SSH_DFLT;
	else
	{
		print_error("unknown RCMD_PGM %s\n", rcmd_pgm);
		exit(1);
	}
	debug_msg("DSH_REMOTE_CMD = %s\n", dsh_remote_cmd);
	debug_msg("RCMD_PGM = %s\n", rcmd_pgm);

	push_remote_argv(inv, dsh_remote_cmd);

	inv->hn_method = (envp = getenv("HN_METHOD")) ? envp : "initial_hostname";
	if (strcmp(inv->hn_method, "initial_hostname") &&
	    strcmp(inv->hn_method, "reliable"))
	{
		print_error("unknown HN_METHOD %s\n", inv->hn_method);
		exit(1);
	}
	debug_msg("HN_METHOD = %s\n", inv->hn_method);

	dshpath = (envp = getenv("DSHPATH")) ? envp : DSHPATH_DFLT;
	debug_msg("DSHPATH = %s\n", dshpath);

	if (getenv("DSH_TIMEOUT"))
		inv->timeout = atoi(getenv("DSH_TIMEOUT"));
	debug_msg("DSH_TIMEOUT = %d\n", inv->timeout);

	if (getenv("DSH_COLLATE") != NULL)
	{
		inv->collate = 1;
		debug_msg("DSH_COLLATE\n");
	}

#if defined(RLIMIT_NOFILE)
	if (getrlimit(RLIMIT_NOFILE, &rl) == -1)
	{
		perror("getrlimit");
	} else {
		debug_msg("RLIMIT_NOFILE = %d (%d)\n",
		    rl.rlim_cur, rl.rlim_max);
		debug_msg("maximum fanout: %d\n", MAX_FANOUT(rl.rlim_cur));
		if (rl.rlim_cur != RLIM_INFINITY && rl.rlim_cur > 3)
			max_fanout = MAX_FANOUT(rl.rlim_cur);
	}
#endif /* defined(RLIMIT_NOFILE) */

#if defined(RLIMIT_NPROC)
	if (getrlimit(RLIMIT_NPROC, &rl) == -1)
	{
		perror("getrlimit");
	} else {
		debug_msg("RLIMIT_NPROC = %d (%d)\n",
		    rl.rlim_cur, rl.rlim_max);
		/* XXX Reduce fanout if necessary */
	}
#endif /* defined(RLIMIT_NPROC) */

	if ((fanout = getenv("FANOUT")) != NULL && atoi(fanout) > 0)
		inv->fanout = atoi(fanout);

	while ((ch = getopt(argc, argv, "qhivct:dDl:w:f:o:")) != -1)
	{
		switch(ch)
		{
			case 'q':
				inv->quiet = 1;
				break;
			case 'h':
				exit(usage(argv[0]));
				break;
			case 'i':
				inv->echo = 1;
				break;
			case 'v':
				inv->verify = 1;
				break;
			case 'c':
				inv->all = 1;
				break;
			case 't':
				if (!strcmp(optarg, "sh") || !strcmp(optarg, "ksh"))
					tflag = 0;
				else if (!strcmp(optarg, "csh") || !strcmp(optarg, "tcsh"))
					tflag = 1;
				else
				{
					print_error("%s unknown shell syntax.\n", optarg);
					exit(1);
				}
				break;
			case 'd':
				dflag = 1;
				if (Dflag)
				{
					print_error("-D and -d cannot be used at the same time.\n");
					exit(1);
				}
				break;
			case 'D':
				Dflag = 1;
				if (dflag)
				{
					print_error("-D and -d cannot be used at the same time.\n");
					exit(1);
				}
				break;
			case 'l':
				lflag = strdup(optarg);
				break;
			case 'w':
				wflag = strdup(optarg);
				break;
			case 'f':
				inv->fanout = atoi(optarg);
				break;
			case 'o':
				oflag = strdup(optarg);
				push_remote_argv(inv, oflag);
				break;
			case 'Y':
				Yflag = strdup(optarg);
				__ypdomain = Yflag;
				break;
			default:
				exit(!usage(argv[0]));
				break;
		}
	}

	/* Cap fanout to what RLIMIT_NOFILE can support (3 pipe fds per child). */
	if (max_fanout > 0 && inv->fanout > max_fanout)
	{
		print_error("fanout %d exceeds RLIMIT_NOFILE capacity; reducing to %d\n",
		    inv->fanout, max_fanout);
		inv->fanout = max_fanout;
	}

	argc -= optind;
	argv += optind;

	if (lflag)
	{
		push_remote_argv(inv, "-l");
		push_remote_argv(inv, lflag);
	}
	if (dflag)
		push_remote_argv(inv, "-f");
	if (Dflag)
		push_remote_argv(inv, "-F");

	/* XXX First NULL argument becomes hostname */
	push_remote_argv(inv, NULL);

	if (strlen(dshpath))
	{
		char *tmp;
		if (tflag)
		{
			tmp = (char *)malloc(sizeof(char) *
				(strlen(dshpath) + strlen("setenv PATH ;") + 1) );
			sprintf(tmp, "setenv PATH %s;", dshpath);
		} else {
			tmp = (char *)malloc(sizeof(char) *
				(strlen(dshpath) + strlen("PATH=; export PATH;") + 1) );
			sprintf(tmp, "PATH=%s; export PATH;", dshpath);
		}
		push_remote_argv(inv, tmp);
		free(tmp);
	}

	if (argc)
		for (i = 0; i < argc; i++)
			push_remote_argv(inv, argv[i]);
	inv->remote_cmdv = &inv->remote_argv[inv->remote_argc - argc];
	inv->cmd_argc = argc;

	debug_msg("remote_argv = ");
	for (i = 0; i < inv->remote_argc; i++)
		debug_msg("%s ", (inv->remote_argv[i] ? inv->remote_argv[i] : "(NULL)"));

	debug_msg("\nremote_argc = %d\n", inv->remote_argc);

	/* The collective source is resolved by the caller against $WCOLL. */
	inv->collective_arg = wflag;

	/*
	 * lflag/oflag were copied into remote_argv by push_remote_argv (which
	 * strdups), so the parser's own copies are no longer needed.  Yflag is
	 * retained via __ypdomain for the resolver, matching prior behaviour.
	 */
	if (lflag)
		free(lflag);
	if (oflag)
		free(oflag);
}

void
invocation_free(struct invocation *inv)
{
	int	i;

	if (inv->remote_argv)
	{
		for (i = 0; i < inv->remote_argc; i++)
		{
			if (inv->remote_argv[i])
			{
				free(inv->remote_argv[i]);
				inv->remote_argv[i] = (char *)NULL;
			}
		}
		free(inv->remote_argv);
		inv->remote_argv = (char **)NULL;
	}
	inv->remote_cmdv = (char **)NULL;

	if (inv->collective_arg)
	{
		free(inv->collective_arg);
		inv->collective_arg = (char *)NULL;
	}
}
