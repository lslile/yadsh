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
#include <string.h>

#include "dsh.h"
#include "print.h"
#include "debug.h"
#include "resolv.h"
#include "collective.h"

typedef struct member
{
	char		*name,
			*reliable;
	struct member	*prev,
			*next;
} member_t;

struct member	*collective = (struct member *)NULL;

int		 collective_count = 0;

int
collective_add_member(char *name)
{
	char 		*reliable;
	struct member	*member;

	debug_msg("collective_add_member(%s)\n", (name ? name : "NULL"));

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
				collective_add_member(*ptr);
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
				debug_msg("collective_add_member - dropping duplicate\n");
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
collective_display(void)
{
	struct member	*ptr = collective;

	printf("working collective (%d):", collective_count);
	while (ptr)
	{
		printf("%s%s", (ptr == collective ? " " : ", "), ptr->name);
		ptr = ptr->next;
	}
	printf("\nfanout: %d\n", fflag);

	return 0;
}
