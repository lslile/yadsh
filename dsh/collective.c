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
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
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
collective_parse(char *buffer, char separator)
{
	char	*tmp = strdup(buffer);
	char	*ptr = tmp;

	while (ptr)
	{
		char *sep;
		if ((sep = strchr(ptr, separator)))
			*sep++ = '\0';
		if (strlen(ptr))
			collective_add_member(ptr);
		ptr = sep;
	}
	return 0;
}


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
			member->name = strdup(name);
			member->reliable = strdup(reliable);
		} else {
			if (!qflag)
				print_host_error(name, "unknown host\n");
			return 1;
		}
	}

	if (collective) {
		struct member	*ptr,
				*prev;
		ptr = collective;
		do
		{
			if (!strcmp(member->reliable, ptr->reliable)) {
				debug_msg("collective_add_member - dropping duplicate\n");
				free(member);
				return 0;
			}
			prev = ptr;
			ptr = ptr->next;
		} while (ptr);
		prev->next = member;
		member->prev = prev;
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

int
collective_add_file(FILE *file)
{
	int		 fd = fileno(file);
	char		*baddr;
	struct stat	 sb;

	if (fstat(fd, &sb) != 0)
	{
		perror("stat");
		exit(1);
	}

	if ((baddr = (char *)mmap(0, sb.st_size, PROT_READ | PROT_WRITE, MAP_PRIVATE,
	    fd, 0)) == MAP_FAILED)
	{
		perror("mmap");
		exit(1);
	}

	collective_parse(baddr, '\n');

	if (munmap(baddr, sb.st_size) == -1)
	{
		perror("munmap");
		exit(1);
	}

	return 0;
}

int
collective_add_stdin(FILE *file)
{
	char	 *wcoll = (char *)NULL;
	int	 fd = fileno(file),
		 count = 0,
		 tmp;

	do {
		char	 readbuffer[1024],
			*tmpbuffer;

		if ((tmp = read(fd, &readbuffer, sizeof(readbuffer))) > 0) {
			tmpbuffer = (char *)malloc(sizeof(char) * (count + tmp + 1));
			bzero(tmpbuffer, sizeof(char) * (count + tmp + 1));
			if (wcoll) {
				bcopy(wcoll, tmpbuffer, sizeof(char) * count);
				free(wcoll);
			}
			wcoll = tmpbuffer;
			bcopy(readbuffer, wcoll + count, sizeof(char) * tmp);
			count += tmp;
			
		}
	} while (tmp > 0);

	collective_parse(wcoll, '\n');

	free(wcoll);

	return 0;
}
