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
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "dsh.h"
#include "print.h"
#include "debug.h"
#include "resolv.h"

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

unsigned int	 debug = 0,
		 collective_count = 0;

int usage(void);

int
add_collective_member(char *name)
{
	char 		*reliable;
	struct member	*member;

	debug_msg("add_collective_member(%s)\n", (name ? name : "NULL"));

	if (!name || *name == '\0')
		return 1;

	if (*name == '@' )
	{
		char **netgroup;
		if ((netgroup = resolv_netgroup(name)) != (char **)NULL)
		{
			char **ptr = netgroup;
			while (*ptr)
			{
				add_collective_member(*ptr);
				ptr++;
			}
		}
		return 0;
	} else {
		if ((reliable = resolv_host(name)) != (char *)NULL)
		{
			member = malloc(sizeof(struct member));
			bzero(member, sizeof(struct member));
			member->name = name;
			member->reliable = strdup(reliable);
		} else {
			if (!qflag)
				print_host_error(name, "unknown host\n");
			return 1;
		}
	}

	if (collective) {
		struct member *ptr;
		ptr = collective;
		do
		{
			if (!strcmp(member->reliable, ptr->reliable)) {
				debug_msg("add_collective_member - dropping duplicate\n");
				free(member);
				return 0;
			}
			if (ptr->next)
				ptr = ptr->next;
		} while (ptr->next);
		ptr->next = member;
		member->prev = ptr;
	} else {
		collective = member;
	}
	collective_count++;

	return 0;
}

int
main(int argc, char **argv)
{
	extern int	 optind;
	extern char	*optarg;
	int		 ch;
	char		*wcoll = (char *)NULL;

	if (getenv("DSH_DEBUG") != NULL)
		debug = 1;

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
		char *ptr;
		ptr = wflag;
		do 
		{
			char *sep;
			if ((sep = strchr(ptr, ',')))
				*sep++ = '\0';
			add_collective_member(ptr);
			ptr = sep;
		} while(ptr);
	}

	if (qflag)
	{
		struct member *ptr = collective;
		printf("working collective (%d):", collective_count);
		while (ptr->next)
		{
			printf("%s%s", (ptr->next ? ", " : ""), ptr->name);
			ptr = ptr->next;
		}
		printf("\nfanout: %d\n", fflag);
		return 0;

	}

	return 0;
}























