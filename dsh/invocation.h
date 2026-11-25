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

#ifndef _invocation_h
#define _invocation_h

/*
 * The canonical, dialect-neutral result of parsing a dsh command line.
 *
 * A front-end parser fills this struct and nothing else; the engine, queue
 * and output code read it and never touch argv or the environment.  Today
 * the only parser is parse_pssp.c (the IBM PSSP dsh syntax); a second
 * dialect (e.g. CSM/DSM) becomes a second parser that fills the same struct,
 * leaving the engine unchanged.  That separation is the point of this struct.
 *
 * Ownership: remote_argv and its elements, and collective_arg, are owned by
 * the invocation and released by invocation_free().  remote_cmdv points into
 * remote_argv and is not separately freed.  hn_method points at an
 * environment string or a string literal and is likewise not freed.
 */
struct invocation
{
	/* Behaviour flags the engine acts on. */
	int	 quiet;		/* -q: list the collective, suppress host errors */
	int	 echo;		/* -i: echo the collective and command first */
	int	 verify;	/* -v: ping members before running */
	int	 all;		/* -c: include members not known to respond */
	int	 collate;	/* DSH_COLLATE: buffer each host's output */
	int	 fanout;	/* -f / FANOUT: max concurrent remote children */
	int	 timeout;	/* DSH_TIMEOUT: per-command watchdog seconds */
	char	*hn_method;	/* HN_METHOD: "initial_hostname" or "reliable" */

	/* The collective source, as named on the command line. */
	char	*collective_arg;	/* -w value, "-" for stdin, NULL => $WCOLL */

	/* The assembled remote command vector (the dialect's output). */
	char	**remote_argv;	/* argv for the remote shell, NULL-terminated */
	int	  remote_argc;	/* element count (excludes the NULL terminator) */
	char	**remote_cmdv;	/* -> the command words within remote_argv */
	int	  cmd_argc;	/* trailing command-word count; 0 => interactive */
};

#endif /* _invocation_h */
