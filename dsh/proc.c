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
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "queue.h"
#include "debug.h"

int	sigchld = -1;

void
proc_sig_child(int signo)
{
	sigchld++;
	debug_msg("child exited (count=%d)\n", sigchld);
}

void
proc_reap(struct process *queue)
{
	if (sigchld < 1)
		return;

	while(sigchld)
	{
		pid_t	 	 pid;
		int 	 	 status;
		struct process	*ptr = queue;
		if ((pid = wait(&status)) == -1)
		{
			perror("proc_reap() wait");
			return;
		}
		debug_msg("process %d: exited %d\n",
		    pid, WEXITSTATUS(status));
		sigchld--;
		while(ptr)
		{
			if (ptr->pid == pid)
			{
				ptr->done = 1;
				ptr->status = status;
				ptr = (struct process *)NULL;
			} else {
				ptr = ptr->next;
			}
		}
	}
}

int
proc_output(FILE *file, struct process *proc, char **buffer, int *length, int flush)
{
	char	*tmp,
		*out;
	int	count = 0;

	debug_msg("proc_output()\n");

	if (!*buffer || !*length)
		return 0;

	out = strdup(*buffer);
	tmp = out;
	do
	{
		char *sep;
		if ((sep = strchr(tmp, '\n')))
		{
			*sep++ = '\0';
			count = sep - out;
			fprintf(file, "%-16s: %s\n", proc->name, tmp);
		}
		tmp = sep;
	} while (tmp);

	free(out);

	if (count)
	{
		tmp = (char *)malloc(sizeof(char) * (*length - count + 1));
		bzero(tmp, sizeof(char) * (*length - count + 1));
		bcopy(*buffer + count, tmp, sizeof(char *) * (*length - count));
		free(*buffer);
		*buffer = tmp;
		*length -= count;
	}

	return count;
}

int
proc_read(struct process *proc, int fd, char **buffer, int *length)
{
	char	 readbuf[1024];
	int	 count;

	bzero(&readbuf, sizeof(readbuf));
	if ((count = read(fd, &readbuf, sizeof(readbuf))) > 0)
	{
		char *tmp;

		/*debug_msg("proc_read() got %d bytes: %s\n", count, readbuf);*/

		tmp = (char *)malloc(sizeof(char) * (*length + count + 1));
		bzero(tmp, sizeof(char) * (*length + count + 1));
		if (*buffer)
		{
			bcopy(*buffer, tmp, sizeof(char) * *length);
			free(*buffer);
		}
		*buffer = tmp;
		bcopy(readbuf, *buffer + *length, sizeof(char) * count);
		*length += count;
	} else if (count < 0) {
		perror("proc_read() read");
	}

	debug_msg("buffer (%d): %s\n", *length, *buffer);

	return count;
}

pid_t
proc_spawn(struct process *proc)
{
	pid_t	 pid;
	int	 stdoutpipe[2],
		 stderrpipe[2];

	if (sigchld == -1)
	{
		signal(SIGCHLD, &proc_sig_child);
		sigchld = 0;
	}

	if ((pipe(stdoutpipe) != 0) ||
	    (pipe(stderrpipe) != 0))
	{
		perror("proc_spawn() pipe");
		exit(1);
	}

	if ((pid = fork()) < 0)
	{
		fprintf(stderr, "fork error!\n");
		exit(1);
	}

	if (pid > 0)
	{
		int fdflags;
		/* Parent */
		proc->stdout = stdoutpipe[0];
		close(stdoutpipe[1]);
		proc->stderr = stderrpipe[0];
		close(stderrpipe[1]);

		/* set descriptors to non-blocking XXX */
		if (!((fdflags = fcntl(proc->stdout, F_GETFL, 0)) < 0))
		{
			if ((fcntl(proc->stdout, F_SETFL, fdflags | O_NDELAY)) < 0)
				perror("proc_spawn() fcntl");
		} else {
			perror("proc_spawn() fcntl");
		}
		if (!((fdflags = fcntl(proc->stderr, F_GETFL, 0)) < 0))
		{
			if ((fcntl(proc->stderr, F_SETFL, fdflags | O_NDELAY)) < 0)
				perror("proc_spawn() fcntl");
		} else {
			perror("proc_spawn() fcntl");
		}

		return pid;
	} else {
		/* Child */
		fclose(stdin);
		if ((dup2(stdoutpipe[1], fileno(stdout)) == -1) ||
		    (dup2(stderrpipe[1], fileno(stderr)) == -1))
		{
			perror("proc_spawn() dup2");
			exit(1);
		}
		close(stdoutpipe[0]);
		close(stdoutpipe[1]);
		close(stderrpipe[0]);
		close(stderrpipe[1]);

		/* exec sub process XXX */
		execl("/bin/netstat", "/bin/netstat", "-a", NULL);

		exit(1); /* Failed to exec ! */
	}
}
