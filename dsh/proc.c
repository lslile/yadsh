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
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "dsh.h"
#include "queue.h"
#include "debug.h"

int	sigchld = -1;

void
proc_sig_child(int signo)
{
	sigchld++;
	debug_msg("child exited (sigchld=%d)\n", sigchld);
}

void
proc_reap(struct process *queue)
{
	sigset_t	newmask,
			oldmask;
	pid_t	 	 pid = 0;
	int 	 	 status;
	struct process	*ptr;

	if (!queue || !sigchld)
		return;

	sigemptyset(&newmask);
	sigaddset(&newmask, SIGCHLD);
	if (sigprocmask(SIG_BLOCK, &newmask, &oldmask) < 0)
		perror("sigprocmask");

	debug_msg("proc_reap() sigchld = %d\n", sigchld);

	while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
	{
		ptr = queue;
		while(ptr)
		{
			if (ptr->pid == pid)
			{
				debug_msg("process %d: exited %d\n",
				    pid, WEXITSTATUS(status));
				ptr->done = 1;
				ptr->status = status;
				ptr = (struct process *)NULL;
			} else {
				ptr = ptr->next;
			}
		}
	}

	sigchld = 0;

	if (sigprocmask(SIG_SETMASK, &oldmask, NULL) < 0)
		perror("sigprocmask");
}

int
proc_output(FILE *file, struct process *proc, char **buffer, int *length, int flush)
{
	char	*tmp = (char *)NULL,
		*out = (char *)NULL;
	int	count = 0;

	debug_msg("proc_output(%s, %s, flush=%d)\n",
			(file == stdout ? "stdout" : "stderr"), proc->name, flush);

	if (*length)
	{
		out = strdup(*buffer);
		tmp = out;
		do
		{
			char *sep;
			if ((sep = strchr(tmp, '\n')))
			{
				*sep++ = '\0';
				count = sep - out;
				if (*tmp == '\r')
					fprintf(file, "tmp is CR!\n");
				fprintf(file, "%-16s%c (%d) %s}\n", proc->name,
						((file == stdout) ? ':' : '|'),
					       strlen(tmp), tmp);
			} else {
				if (flush && count < *length)
				{
					fprintf(file, "%-16s%c %s\n", proc->name,
							((file == stdout) ? ':' : '|'), tmp);
					count += strlen(tmp);
				}
				*length -= count;
				free(*buffer);
				if (*length)
					*buffer = strdup(tmp);
			}
			tmp = sep;
		} while (tmp);
	}

	if (flush && proc->status && file == stderr)
	{
		if (WIFEXITED(proc->status))
			fprintf(file, "%-16s: exit %d\n", proc->name, WIFEXITED(proc->status));
		if (WIFSIGNALED(proc->status))
			fprintf(file, "%-16s: killed (%d)\n", proc->name, WTERMSIG(proc->status));
		if (WIFSTOPPED(proc->status))
			fprintf(file, "%-16s: killed (%d)\n", proc->name, WSTOPSIG(proc->status));


	}

	free(out);

	return count;
}

int
proc_read(struct process *proc, int fd, char **buffer, int *length)
{
	char	 readbuf[TMPBUFSIZE];
	int	 count;

	bzero(&readbuf, sizeof(readbuf));

	if ((count = read(fd, &readbuf, sizeof(readbuf))) > 0)
	{
		char *tmp;

		debug_msg("proc_read() got %d bytes\n", count);

		tmp = (char *)malloc(sizeof(char) * (*length + count + 1));
		bzero(tmp, sizeof(char) * (*length + count + 1));
		if (*length && *buffer)
		{
			bcopy(*buffer, tmp, sizeof(char) * (*length));
			free(*buffer);
		}
		*buffer = tmp;
		bcopy(readbuf, *buffer + (*length), sizeof(char) * count);
		*length += count;
	} else if (count < 0) {
		perror("proc_read() read");
	}

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
		char **tmp = proc->argv;
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
		debug_msg("proc_spawn(): ");
		while (*tmp)
			debug_msg("%s ", *tmp++);
		debug_msg("\n");

		return pid;
	} else {
		/* Child XXX Don't close stdin, it causes problems for SSH */
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
		execvp(proc->argv[0], proc->argv);

		fprintf(stderr, "dsh: subprocess failed to exec: %s\n", strerror(errno));

		exit(1); /* Failed to exec ! */
	}
}
