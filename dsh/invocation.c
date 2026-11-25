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
 * Operations on struct invocation shared by every dialect parser.  Keeping
 * the remote-argv builder and the destructor here (rather than copied into
 * parse_pssp.c and parse_dsm.c) means the two dialects cannot drift in how
 * they grow or free the canonical invocation.  This file is compiled into
 * every dialect binary, like queue.c and print.c.
 */

#include "includes.h"

void
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
