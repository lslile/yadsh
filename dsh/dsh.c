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
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "print.h"
#include "debug.h"
#include "resolv.h"
#include "collective.h"

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

unsigned int	 debug = 0;

extern int 	 errno;

int usage(void);


int
main(int argc, char **argv)
{
	extern int	 optind;
	extern char	*optarg;
	int		 ch;
	char		*wcoll = (char *)NULL,
			*fanout;

	if (getenv("DSH_DEBUG") != NULL)
		debug = 1;

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
				break;
			case 'D':
				Dflag = 1;
				break;
			case 'l':
				lflag = strdup(optarg);
				break;
			case 'w':
				wflag = strdup(optarg);
				break;
			case 'f':
				fflag = atoi(optarg);
				break;
			case 'o':
				oflag = strdup(optarg);
				break;
			default:
				exit(!usage());
				break;
		}
	}

	argc -= optind;
	argv += optind;

	if (!wflag && ((wcoll = getenv("WCOLL")) == NULL))
	{
		fprintf(stderr, "working collective not set.\n");
		exit(1);
	}

	if (wflag)
	{
		if (*wflag != '-')
			collective_parse(wflag, ',');
		else 
			collective_add_stdin(stdin);
	} else {
		FILE	*file = stdin;
		if (!wflag)
		{
			if ((file = fopen(wcoll, "r")) == NULL)
			{
				perror("fopen");
				return 1;
			}
		}
		collective_add_file(file);
	}

	if (qflag)
		return collective_display();


	return 0;
}























