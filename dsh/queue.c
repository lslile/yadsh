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
#include <sys/types.h>
#include <unistd.h>

#include "debug.h"
#include "queue.h"
#include "proc.h"

struct process *queue = (struct process *)NULL;

int queue_count = 0;
int queue_slot_count = 0;

int
queue_add(char *host)
{
	return 0;
}

int
queue_load(void)
{
	return queue_count;
}

int
queue_slots(void)
{
	return (queue_slot_count - queue_count);
}

int
queue_max_slots(int set)
{
	if (set > 0)
		queue_slot_count = set;
	return queue_slot_count;
}

int
queue_insert(char *name)
{
	struct process *new;

	if (!name)
		return 1;

	debug_msg("queue_insert(%s)\n", name);

	new = (struct process *)malloc(sizeof(struct process));
	bzero(new, sizeof(struct process));
	new->status = -1;
	new->stdout = -1;
	new->stderr = -1;

	new->name = name;

	if (queue)
	{
		struct process	*ptr = queue,
				*prev = queue;
		while (ptr)
		{
			prev = ptr;
			ptr = ptr->next;
		}
		prev->next = new;
		new->prev = prev;
	} else {
		queue = new;
	}
	queue_count++;

	new->pid = proc_spawn(new);

	debug_msg("proc_spawn() = %d\n", new->pid);

	return 0;
}

int
queue_delete(struct process *ptr)
{
	if (!ptr)
		return 1;

	debug_msg("queue_delete(%s)\n", ptr->name);

	if (ptr == queue)
		queue = ptr->next;
	if (ptr->prev)
		(ptr->prev)->next = ptr->next;
	if (ptr->next)
		(ptr->next)->prev = ptr->prev;
	free(ptr);
	queue_count--;

	return 0;
}

int
queue_run(void)
{
	struct process	*ptr = queue;
	fd_set		 read_fds;
	struct timeval	 tv;
	int		 hi_fd = 0;

	debug_msg("queue_run() [load %d / slots %d]\n",
	    queue_count, queue_slot_count);

	FD_ZERO(&read_fds);
	while (ptr)
	{
		struct process *next = ptr->next;
		if (ptr->stdout >= 0)
		{
			FD_SET(ptr->stdout, &read_fds);
			hi_fd = (ptr->stdout > hi_fd ? ptr->stdout : hi_fd);
		}
		if (ptr->stderr >= 0)
		{
			FD_SET(ptr->stderr, &read_fds);
			hi_fd = (ptr->stderr > hi_fd ? ptr->stderr : hi_fd);
		}
		if (ptr->done && ptr->stdout == -1 && ptr->stderr == -1)
		{
			proc_output(stdout, ptr, &ptr->outbuffer, &ptr->outcount, 0);
			proc_output(stdout, ptr, &ptr->outbuffer, &ptr->outcount, 0);
			queue_delete(ptr);
		}
		ptr = next;
	}

	hi_fd++;
	tv.tv_sec = 0;
	tv.tv_usec = 100;
	if (select(hi_fd, &read_fds, NULL, NULL, &tv) == -1)
	{
		perror("select");
		exit(1);
	}

	ptr = queue;
	while (ptr)
	{
		int count;
		if (ptr->stdout >= 0 && FD_ISSET(ptr->stdout, &read_fds))
		{
			debug_msg("proc %d: stdout ready\n", ptr->pid);
			count = proc_read(ptr, ptr->stdout,
					&ptr->outbuffer, &ptr->outcount);
			if (count == 0)
			{
				debug_msg("%d closing stdout\n", ptr->pid);
				close(ptr->stdout);
				ptr->stdout = -1;
			} 
			proc_output(stdout, ptr, &ptr->outbuffer, &ptr->outcount, 0);
		}
		if (ptr->stderr >= 0 && FD_ISSET(ptr->stderr, &read_fds))
		{
			debug_msg("proc %d: stderr ready\n", ptr->pid);
			count = proc_read(ptr, ptr->stderr,
					&ptr->errbuffer, &ptr->errcount);
			if (count == 0)
			{
				debug_msg("%d closing stderr\n", ptr->pid);
				close(ptr->stderr);
				ptr->stderr = -1;
			}
			proc_output(stderr, ptr, &ptr->errbuffer, &ptr->errcount, 0);
		}
		ptr = ptr->next;
	}

	proc_reap(queue);

	return 0;
}
