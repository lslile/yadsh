/*
 * Copyright (c) 2004, Larry Lile <lile@stdio.com>
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
 * $Id$
 */

#ifndef lint
static char rcsid[] = "$Id$";
#endif /* not lint */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "print.h"
#include "debug.h"
#include "resolv.h"
#include "queue.h"
#include "collective.h"
#include "proc.h"

#define MAX_FANOUT(x)	((x - 3) / 3)
#define TIMEOUT		180

unsigned int	 qflag = 0,
		 iflag = 0,
		 vflag = 0,
		 cflag = 0,
		 tflag = 0,
		 dflag = 0,
		 Dflag = 0,
		 fflag = 64;

char		*lflag = (char *)NULL,
		*wflag = (char *)NULL,
		*oflag = (char *)NULL;

int		 signaled = 0,
		 sigcount = 0;

unsigned int	 debug = 0;

extern int 	 errno;

char		**remote_argv = (char **)NULL;
int		  remote_argc = 0;

int usage(void);

void
sig_handler(int signum)
{
	debug_msg("sig_handler(%d)\n", signum);
	signaled = signum;
	sigcount++;
	if (sigcount > 5)
	{
		print_error("Something must be hung, exiting...\n");
		exit(1);
	}
}

int
main(int argc, char **argv)
{
	extern int	 optind;
	extern char	*optarg;
	int		 ch,
			 i,
			 remote_argc_pad = 4,
			 watchdog = TIMEOUT,
			 watchdogs_set = 0,
			 queue_watchdog = 0;
	char		*wcoll = (char *)NULL,
			*fanout,
			*dsh_remote_cmd,
			*rcmd_pgm,
			*hn_method,
			*dshpath;
	struct member	*ptr;
	struct rlimit	 rl;

	int		 rc = 0;

	if (getenv("DSH_DEBUG") != NULL)
		debug = 1;
	debug_msg("DSH_DEBUG\n");

	dsh_remote_cmd = getenv("DSH_REMOTE_CMD") ?: "rsh";
	if (!strcmp(dsh_remote_cmd, "rsh"))
		rcmd_pgm = getenv("RCMD_PGM") ?: "/usr/bin/rsh";
	else if (!strcmp(dsh_remote_cmd, "secrshell"))
		rcmd_pgm = getenv("RCMD_PGM") ?: "/usr/bin/ssh";
	else
	{
		print_error("unknown DSH_REMOTE_CMD %s\n", dsh_remote_cmd);
		exit(1);
	}
	debug_msg("DSH_REMOTE_CMD = %s\n", dsh_remote_cmd);
	debug_msg("RCMD_PGM = %s\n", rcmd_pgm);

	hn_method = getenv("HN_METHOD") ?: "initial_hostname";
	if (strcmp(hn_method, "initial_hostname") &&
	    strcmp(hn_method, "reliable"))
	{
		print_error("unknown HN_METHOD %s\n", hn_method);
		exit(1);
	}
	debug_msg("HN_METHOD = %s\n", hn_method);

	dshpath = getenv("DSHPATH") ?: "/usr/ucb:/bin:/usr/bin:.";
	debug_msg("DSHPATH = %s\n", dshpath);

	if (getenv("DSHTIMEOUT"))
		watchdog = atoi(getenv("DSHTIMEOUT"));
	debug_msg("DSHTIMEOUT = %d\n", watchdog);

	if (getrlimit(RLIMIT_NOFILE, &rl) == -1)
	{
		perror("getrlimit");
	} else {
		debug_msg("RLIMIT_NOFILE = %d (%d)\n",
		    rl.rlim_cur, rl.rlim_max);
		debug_msg("maximum fanout: %d\n", MAX_FANOUT(rl.rlim_cur));
		/* XXX Reduce fflag if necessary */
	}

	if (getrlimit(RLIMIT_NPROC, &rl) == -1)
	{
		perror("getrlimit");
	} else {
		debug_msg("RLIMIT_NPROC = %d (%d)\n",
		    rl.rlim_cur, rl.rlim_max);
		/* XXX Reduce fflag if necessary */
	}

	if ((fanout = getenv("FANOUT")) != NULL && atoi(fanout) > 0)
		fflag = atoi(fanout);

	while ((ch = getopt(argc, argv, "qhivctdDl:w:f:o:")) != -1)
	{
		switch(ch)
		{
			case 'q':
				qflag = 1;
				break;
			case 'h':
				exit(usage());
				break;
			case 'i':
				iflag = 1;
				break;
			case 'v':
				vflag = 1;
				break;
			case 'c':
				cflag = 1;
				break;
			case 't':
				tflag = 1;
				break;
			case 'd':
				dflag = 1;
				if (Dflag)
				{
					print_error("-D and -d cannot be used at the same time.\n");
					exit(1);
				}
				remote_argc_pad++;
				break;
			case 'D':
				Dflag = 1;
				if (dflag)
				{
					print_error("-D and -d cannot be used at the same time.\n");
					exit(1);
				}
				remote_argc_pad++;
				break;
			case 'l':
				lflag = strdup(optarg);
				remote_argc_pad += 2;
				break;
			case 'w':
				wflag = strdup(optarg);
				break;
			case 'f':
				fflag = atoi(optarg);
				break;
			case 'o':
				oflag = strdup(optarg);
				remote_argc_pad++;
				break;
			default:
				exit(!usage());
				break;
		}
	}

	argc -= optind;
	argv += optind;

	remote_argv = (char **)malloc(sizeof(char *) * (argc + remote_argc_pad));
	memset(remote_argv, argc + remote_argc_pad, sizeof(char *));
	remote_argv[remote_argc++] = strdup(rcmd_pgm);
	if (lflag)
	{
		remote_argv[remote_argc++] = strdup("-l");
		remote_argv[remote_argc++] = strdup(lflag);
	}
	if (dflag)
		remote_argv[remote_argc++] = strdup("-f");
	if (Dflag)
		remote_argv[remote_argc++] = strdup("-F");
	if (oflag)
		remote_argv[remote_argc++] = strdup(oflag);

	remote_argv[remote_argc++] = NULL; /* hostname */

	for (i = 0; i < argc; i++)
		remote_argv[remote_argc++] = strdup(argv[i]);

	remote_argv[remote_argc] = NULL; /* Last argv entry! */

	debug_msg("remote_argv = ");
	for (i = 0; i <= remote_argc; i++)
		debug_msg("%s ", (remote_argv[i] ? : "(NULL)"));
	debug_msg("\nremote_argc = %d - %d - %d\n",
	    remote_argc, argc, argc + remote_argc_pad);
	
	if (!wflag && ((wcoll = getenv("WCOLL")) == NULL))
	{
		print_error("working collective not set.\n");
		exit(1);
	}

	if (wflag)
	{
		if (*wflag != '-')
		{
			debug_msg("loading collective from -w\n");
			collective_parse(wflag, ',');
		} else {
			debug_msg("loading collective from stdin\n");
			collective_add_stdin(stdin);
		}
	} else {
		FILE	*file = stdin;
		debug_msg("loading collective from WCOLL=%s\n", wcoll);
		if ((file = fopen(wcoll, "r")) == NULL)
		{
			perror("fopen");
			return 1;
		}
		collective_add_file(file);
	}

	if (qflag)
		return collective_display();

	if (!argc)
		exit(usage());

	signal(SIGINT,  &sig_handler);
	signal(SIGQUIT, &sig_handler);
	signal(SIGABRT, &sig_handler);

	queue_max_slots(fflag);
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
			char *name = ptr->name;
			if (!strcmp(hn_method, "reliable"))
				name = ptr->reliable;
			queue_insert(name, remote_argc, remote_argv);
			ptr = collective_get_next(ptr);
		}
		rc += queue_run(signaled);

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
