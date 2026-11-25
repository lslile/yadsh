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

#ifndef _parse_h
#define _parse_h

struct invocation;

/*
 * The dialect-neutral parser entry point.  Exactly one parser is linked into
 * each binary: parse_pssp.c for dsh.pssp (IBM PSSP syntax), parse_dsm.c for
 * dsh.dsm (IBM DSM syntax).  Both define parse_invocation() with this same
 * prototype, so dsh.c calls it without naming a dialect; the build selects
 * which object is linked (see configure --enable-dialect and dsh/Makefile.am).
 *
 * It turns argv and the environment into *inv, which the caller provides
 * zero-initialised, and nothing else: the engine, queue and output code read
 * *inv and never look at argv or getenv.  On a usage or configuration error it
 * prints a diagnostic and exits the process, so it returns only on success.
 * Release the result with invocation_free() (declared in invocation.h).
 */
void parse_invocation(int argc, char **argv, struct invocation *inv);

#endif /* _parse_h */
