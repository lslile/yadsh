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

#include "includes.h"
RCSID("$Id$");

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
queue_insert(char *name, int argc, char **argv)
{
	struct process *new;
	int		i;

	if (!name)
		return 1;

	debug_msg("queue_insert(%s)\n", name);

	new = (struct process *)malloc(sizeof(struct process));
	memset(new, '\0', sizeof(struct process));
	new->status = -1;
	new->pstdout = -1;
	new->pstderr = -1;
	new->argv = (char **)malloc(sizeof(char *) * (argc + 1));
	memset(new->argv, '\0', sizeof(char *) * (argc + 1));
	for (i = 0; i < argc; i++)
	{
		if (argv[i])
			new->argv[i] = argv[i];
		else
			new->argv[i] = name;
	}
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
queue_run(int signal)
{
	struct process	*ptr = queue;
	fd_set		 read_fds;
	struct timeval	 tv;
	int		 hi_fd = 0,
			 rc = 0;
	time_t		 now = time(NULL);

	static int	 load = 0,
			 slots = 0;

	if (load != queue_count || slots != queue_slots())
		debug_msg("\nqueue_run(%d) [load %d / slots %d]\n",
		    signal, queue_count, queue_slots());

	load = queue_count;
	slots = queue_slots();

	FD_ZERO(&read_fds);
	while (ptr)
	{
		struct process *next = ptr->next;
		if (signal && ptr->signal != signal)
		{
			ptr->signal = signal;
			kill(ptr->pid, signal);
		}
		if (ptr->watchdog && now > ptr->timer)
		{
			debug_msg("queue_run() watchdog killed %d\n", ptr->pid);
			ptr->signal = SIGTERM;
			ptr->watchdog = -1;
			ptr->timer = 0;
			kill(ptr->pid, SIGTERM);
		}

		if (ptr->pstdout >= 0)
		{
			FD_SET(ptr->pstdout, &read_fds);
			hi_fd = (ptr->pstdout > hi_fd ? ptr->pstdout : hi_fd);
		}
		if (ptr->pstderr >= 0)
		{
			FD_SET(ptr->pstderr, &read_fds);
			hi_fd = (ptr->pstderr > hi_fd ? ptr->pstderr : hi_fd);
		}
		if (ptr->done && ptr->pstdout == -1 && ptr->pstderr == -1)
		{
			proc_output(stdout, ptr, &ptr->outbuffer, &ptr->outcount, 1);
			proc_output(stderr, ptr, &ptr->errbuffer, &ptr->errcount, 1);
			queue_delete(ptr);
		}
		ptr = next;
	}

	hi_fd++;
	tv.tv_sec = 0;
	tv.tv_usec = 100;
	if (select(hi_fd, &read_fds, NULL, NULL, &tv) == -1)
	{
		if (errno != EINTR)
			perror("queue_run() select");
		return 0;
	}

	ptr = queue;
	while (ptr)
	{
		int count;

		if (ptr->pstdout >= 0 && FD_ISSET(ptr->pstdout, &read_fds))
		{
			debug_msg("proc %d: stdout ready\n", ptr->pid);
			count = proc_read(ptr, ptr->pstdout,
					&ptr->outbuffer, &ptr->outcount);
			if (count == 0)
			{
				debug_msg("%d closing stdout\n", ptr->pid);
				close(ptr->pstdout);
				ptr->pstdout = -1;
			} 
			/*proc_output(stdout, ptr, &ptr->outbuffer, &ptr->outcount, 0);*/
			if (ptr->watchdog)
			{
				debug_msg("queue_run() watchdog reset %d\n", ptr->pid);
				ptr->timer = now + ptr->watchdog;

			}
		}
		if (ptr->pstderr >= 0 && FD_ISSET(ptr->pstderr, &read_fds))
		{
			debug_msg("proc %d: stderr ready\n", ptr->pid);
			count = proc_read(ptr, ptr->pstderr,
					&ptr->errbuffer, &ptr->errcount);
			if (count == 0)
			{
				debug_msg("%d closing stderr\n", ptr->pid);
				close(ptr->pstderr);
				ptr->pstderr = -1;
			}
			/*proc_output(stderr, ptr, &ptr->errbuffer, &ptr->errcount, 0);*/
			if (ptr->watchdog)
			{
				debug_msg("queue_run() watchdog reset %d\n", ptr->pid);
				ptr->timer = now + ptr->watchdog;
			}

		}
		ptr = ptr->next;
	}

	rc = proc_reap(queue);

	return rc;
}

int
queue_set_watchdogs(time_t timeout)
{
	struct process	*ptr = queue;
	time_t		 now = time(NULL);

	debug_msg("queue_set_watchdogs(%d)\n", timeout);

	while (ptr)
	{
		debug_msg("queue_set_watchdogs(%d) - pid %d\n", timeout, ptr->pid);
		ptr->watchdog = timeout;
		ptr->timer = now + timeout;
		ptr = ptr->next;
	}

	return 0;
}
