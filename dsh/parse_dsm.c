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
 * The IBM DSM dsh command-line and environment parser (AIX Distributed
 * Systems Management).  Reference: the IBM dsh man page,
 * https://www.ibm.com/docs/en/aix/7.3.0?topic=d-dsh-command
 *
 * Like parse_pssp.c, it turns argv and the environment into a struct
 * invocation and nothing else; the engine, queue and output code read that
 * struct and never look at argv or getenv.  The DSM grammar differs from PSSP
 * (node list is -n not -w, timeout is -t, remote syntax is -S, transport is
 * -r/DSH_NODE_RSH), which is exactly why each dialect is its own binary.
 *
 * This first cut implements the flags the current engine can honor: target
 * selection (-n, -N, -a, noderange, DSH_NODE_LIST), transport (-r, -o, -l),
 * remote syntax (-S), fanout (-f), timeout (-t), streaming (-s), verify (-v),
 * -q (show config), -V, -h, and -X (ignore DSH_* env vars).  -C is accepted
 * but deferred to T15 (context modules).  Flags that need new engine support
 * (-z, -Q, -F, --log, -m, -T, -i, -c, -L) and remote-copy (-e/-E = T5) arrive
 * in later slices; they are not silently ignored here.
 */

#include "includes.h"
#include "parse.h"

#include <dirent.h>
#if defined(HAVE_GETOPT_H)
#include <getopt.h>
#endif

#if !defined(RSH_DFLT)
#define RSH_DFLT "/usr/bin/rsh"
#endif

#if !defined(SSH_DFLT)
#define SSH_DFLT "/usr/bin/ssh"
#endif

#if !defined(DSHPATH_DFLT)
#define DSHPATH_DFLT ""
#endif

#define MAX_FANOUT(x)	((x - 3) / 3)
#define DSM_TIMEOUT	0		/* DSM default: wait indefinitely */
#define NODERANGE_CAP	100000		/* guard against runaway [1-N] blowups */

/*
 * -X env-var filtering.  With -X, DSH_* environment variables are ignored
 * unless named in the optional comma-separated keep list.  Everything the
 * parser reads from the environment goes through env_get() so one check
 * enforces this.
 */
static int	 ignore_env = 0;
static char	*env_keep = NULL;	/* comma list of names to NOT ignore */

static int
env_kept(const char *name)
{
	size_t	 nlen;
	char	*p;

	if (!ignore_env)
		return 1;
	if (env_keep == NULL)
		return 0;		/* bare -X: ignore all */

	nlen = strlen(name);
	for (p = env_keep; *p != '\0'; )
	{
		char	*comma = strchr(p, ',');
		size_t	 seg = comma ? (size_t)(comma - p) : strlen(p);

		if (seg == nlen && strncmp(p, name, nlen) == 0)
			return 1;
		if (comma == NULL)
			break;
		p = comma + 1;
	}
	return 0;
}

static char *
env_get(const char *name)
{
	if (!env_kept(name))
		return NULL;
	return getenv(name);
}

/* Return strdup("base,add"), freeing base.  Avoids asprintf (not portable
 * without _GNU_SOURCE on glibc). */
static char *
join_comma(char *base, const char *add)
{
	size_t	 n = strlen(base) + 1 + strlen(add) + 1;
	char	*m;

	if ((m = (char *)malloc(n)) == NULL)
	{
		perror("malloc");
		exit(1);
	}
	snprintf(m, n, "%s,%s", base, add);
	free(base);
	return m;
}

/*
 * A growable comma-separated string of target names.  sb_add() appends one
 * name, inserting the separating comma as needed.
 */
struct strbuf
{
	char	*buf;
	size_t	 len;	/* strlen(buf) */
	size_t	 cap;
};

static void
sb_add(struct strbuf *sb, const char *name)
{
	size_t	 add = strlen(name);
	size_t	 need = sb->len + add + 2;	/* comma + NUL */

	if (need > sb->cap)
	{
		size_t	 ncap = sb->cap ? sb->cap : 64;
		char	*nb;

		while (ncap < need)
			ncap *= 2;
		if ((nb = (char *)realloc(sb->buf, ncap)) == NULL)
		{
			perror("Unable to allocate target list - realloc");
			exit(1);
		}
		sb->buf = nb;
		sb->cap = ncap;
	}
	if (sb->len > 0)
		sb->buf[sb->len++] = ',';
	memcpy(sb->buf + sb->len, name, add);
	sb->len += add;
	sb->buf[sb->len] = '\0';
}

/*
 * Expand one target token that may contain a single bracketed range/list,
 * e.g. "node[1-4,7]" -> node1 node2 node3 node4 node7, preserving the zero-pad
 * width of the low bound.  A token with no '[' is added verbatim.  Only one
 * bracket pair per token is handled in this first cut.
 */
static void
expand_token(struct strbuf *sb, const char *token)
{
	const char	*lb = strchr(token, '[');
	const char	*rb = lb ? strchr(lb, ']') : NULL;
	size_t		 prelen;
	const char	*suffix;
	char		*spec, *item, *save;
	char		 name[1024];

	if (lb == NULL || rb == NULL || rb < lb)
	{
		sb_add(sb, token);		/* no range: literal */
		return;
	}

	prelen = (size_t)(lb - token);
	suffix = rb + 1;
	if (prelen >= sizeof(name))
	{
		sb_add(sb, token);		/* too long to expand: literal */
		return;
	}

	/* Copy the bracket body ("1-4,7") so strtok_r can carve it up. */
	{
		size_t	 blen = (size_t)(rb - lb - 1);
		if ((spec = (char *)malloc(blen + 1)) == NULL)
		{
			perror("malloc");
			exit(1);
		}
		memcpy(spec, lb + 1, blen);
		spec[blen] = '\0';
	}

	for (item = strtok_r(spec, ",", &save); item != NULL;
	     item = strtok_r(NULL, ",", &save))
	{
		char	*dash = strchr(item, '-');
		long	 lo, hi, n;
		int	 width;
		char	*end;

		if (dash == NULL)
		{
			/* single value: keep its literal text (and width) */
			if (prelen + strlen(item) + strlen(suffix) < sizeof(name))
			{
				snprintf(name, sizeof(name), "%.*s%s%s",
				    (int)prelen, token, item, suffix);
				sb_add(sb, name);
			}
			continue;
		}

		*dash = '\0';
		lo = strtol(item, &end, 10);
		if (*item == '\0' || *end != '\0')
			continue;		/* malformed low bound: skip */
		hi = strtol(dash + 1, &end, 10);
		if (*(dash + 1) == '\0' || *end != '\0' || hi < lo)
			continue;		/* malformed/empty/reversed: skip */
		if (hi - lo + 1 > NODERANGE_CAP)
		{
			print_error("noderange %s expands past %d entries\n",
			    token, NODERANGE_CAP);
			exit(1);
		}
		width = (int)strlen(item);	/* zero-pad to low-bound width */
		for (n = lo; n <= hi; n++)
		{
			snprintf(name, sizeof(name), "%.*s%0*ld%s",
			    (int)prelen, token, width, n, suffix);
			sb_add(sb, name);
		}
	}
	free(spec);
}

/*
 * Split a string into target tokens on comma/space/newline separators, but
 * treat a bracketed [...] span as part of its surrounding token so a range
 * like node[1-4,7] survives the comma inside the brackets.  Each token is
 * handed to expand_token().  When stop_at_comment is set, a token beginning
 * with '#' ends processing of the rest of the string (group-file comments).
 */
static void
add_tokens(struct strbuf *sb, const char *str, int stop_at_comment)
{
	const char	*p = str;
	char		 tok[1024];

	while (*p != '\0')
	{
		size_t	 len = 0;
		int	 depth = 0;

		while (*p == ',' || *p == ' ' || *p == '\t' ||
		    *p == '\r' || *p == '\n')
			p++;
		if (*p == '\0')
			break;
		while (*p != '\0' && (depth > 0 ||
		    (*p != ',' && *p != ' ' && *p != '\t' &&
		     *p != '\r' && *p != '\n')))
		{
			if (*p == '[')
				depth++;
			else if (*p == ']' && depth > 0)
				depth--;
			if (len < sizeof(tok) - 1)
				tok[len++] = *p;
			p++;
		}
		tok[len] = '\0';
		if (stop_at_comment && tok[0] == '#')
			break;
		if (len > 0)
			expand_token(sb, tok);
	}
}

static void
add_list(struct strbuf *sb, const char *arg)
{
	add_tokens(sb, arg, 0);
}

/*
 * Read a node-group file, adding each whitespace-separated name.  Blank lines
 * and lines beginning with '#' are ignored.  Returns 0 on success, -1 if the
 * file could not be opened.
 */
static int
read_group_file(struct strbuf *sb, const char *path)
{
	FILE	*fp;
	char	 line[4096];

	if ((fp = fopen(path, "r")) == NULL)
		return -1;
	while (fgets(line, sizeof(line), fp) != NULL)
		add_tokens(sb, line, 1);	/* '#' ends the line */
	fclose(fp);
	return 0;
}

/* Resolve one named group by searching each directory in DSH_NODEGROUP_PATH. */
static void
resolve_group(struct strbuf *sb, const char *ngpath, const char *group)
{
	char	*copy, *dir, *save;
	char	 path[4096];
	int	 found = 0;

	if (ngpath == NULL || *ngpath == '\0')
	{
		print_error("-N %s: DSH_NODEGROUP_PATH is not set\n", group);
		exit(1);
	}
	if ((copy = strdup(ngpath)) == NULL)
	{
		perror("strdup");
		exit(1);
	}
	for (dir = strtok_r(copy, ":", &save); dir != NULL && !found;
	     dir = strtok_r(NULL, ":", &save))
	{
		snprintf(path, sizeof(path), "%s/%s", dir, group);
		if (read_group_file(sb, path) == 0)
			found = 1;
	}
	free(copy);
	if (!found)
	{
		print_error("node group '%s' not found in DSH_NODEGROUP_PATH\n",
		    group);
		exit(1);
	}
}

/* -a: union of every node in every group file under DSH_NODEGROUP_PATH. */
static void
resolve_all_groups(struct strbuf *sb, const char *ngpath)
{
	char	*copy, *dir, *save;

	if (ngpath == NULL || *ngpath == '\0')
	{
		print_error("-a: DSH_NODEGROUP_PATH is not set\n");
		exit(1);
	}
	if ((copy = strdup(ngpath)) == NULL)
	{
		perror("strdup");
		exit(1);
	}
	for (dir = strtok_r(copy, ":", &save); dir != NULL;
	     dir = strtok_r(NULL, ":", &save))
	{
		DIR		*d;
		struct dirent	*ent;
		char		 path[4096];

		if ((d = opendir(dir)) == NULL)
			continue;
		while ((ent = readdir(d)) != NULL)
		{
			struct stat	st;

			if (ent->d_name[0] == '.')
				continue;
			snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
			if (stat(path, &st) == 0 && S_ISREG(st.st_mode))
				(void)read_group_file(sb, path);
		}
		closedir(d);
	}
	free(copy);
}

/* Read DSH_NODE_LIST (a file that replaced PSSP's WCOLL) into the target set. */
static void
read_node_list_file(struct strbuf *sb, const char *path)
{
	if (read_group_file(sb, path) != 0)
	{
		print_error("cannot open DSH_NODE_LIST file %s: %s\n",
		    path, strerror(errno));
		exit(1);
	}
}

static int
show_config(const char *rsh, int fanout, int timeout, const char *syntax,
    const char *ngpath, const char *nodelist)
{
	printf("dsh.dsm configuration:\n");
	printf("  remote shell (-r/DSH_NODE_RSH):   %s\n", rsh);
	printf("  fanout (-f/DSH_FANOUT):           %d\n", fanout);
	if (timeout > 0)
		printf("  timeout (-t/DSH_TIMEOUT):         %d s\n", timeout);
	else
		printf("  timeout (-t/DSH_TIMEOUT):         (wait indefinitely)\n");
	printf("  remote syntax (-S/DSH_SYNTAX):    %s\n", syntax);
	printf("  node groups (DSH_NODEGROUP_PATH): %s\n",
	    (ngpath && *ngpath) ? ngpath : "(unset)");
	printf("  node list (DSH_NODE_LIST):        %s\n",
	    (nodelist && *nodelist) ? nodelist : "(unset)");
	printf("  contexts:                         (none; arrives in T15)\n");
	return 0;
}

void
parse_invocation(int argc, char **argv, struct invocation *inv)
{
	int		 ch, i, max_fanout = 0, explicit_sel = 0;
	struct strbuf	 targets = { NULL, 0, 0 };

	/* Flag presence / values (NULL or -1 => not given on the command line). */
	char		*nflag = NULL;	/* accumulated -n values (comma list) */
	char		*Nflag = NULL;	/* accumulated -N values */
	int		 aflag = 0;	/* -a all groups */
	int		 fflag = -1;	/* -f fanout */
	int		 tflag = -1;	/* -t timeout */
	int		 sflag = 0;	/* -s streaming */
	char		*lflag = NULL;	/* -l user */
	char		*oflag = NULL;	/* -o node options */
	char		*rflag = NULL;	/* -r remote shell */
	char		*Sflag = NULL;	/* -S csh|ksh */
	char		*Cflag = NULL;	/* -C context (T15) */
	int		 qflag = 0;	/* -q show config */

	/* Resolved settings (env defaults, then flag overrides). */
	char		*rsh, *node_opts, *dsh_path, *syntax;
	char		*ngpath, *node_list_env, *envp;
	int		 csh;		/* 1 => csh syntax, 0 => sh/ksh syntax */

#if defined(RLIMIT_NOFILE)
	struct rlimit	 rl;
#endif

#if defined(HAVE_GETOPT_LONG)
	static const struct option longopts[] = {
		{ "nodes",        required_argument, NULL, 'n' },
		{ "nodegroups",   required_argument, NULL, 'N' },
		{ "fanout",       required_argument, NULL, 'f' },
		{ "timeout",      required_argument, NULL, 't' },
		{ "syntax",       required_argument, NULL, 'S' },
		{ "stream",       no_argument,       NULL, 's' },
		{ "verify",       no_argument,       NULL, 'v' },
		{ "version",      no_argument,       NULL, 'V' },
		{ "help",         no_argument,       NULL, 'h' },
		{ "show-config",  no_argument,       NULL, 'q' },
		{ "context",      required_argument, NULL, 'C' },
		{ "node-rsh",     required_argument, NULL, 'r' },
		{ "node-options", required_argument, NULL, 'o' },
		{ "user",         required_argument, NULL, 'l' },
		{ NULL,           0,                 NULL,  0  }
	};
#endif
	/* Leading '+': stop at the first non-option so the remote command's own
	 * flags (e.g. `dsh.dsm -n h1 ls -l') are not parsed as dsh options. */
	static const char optstring[] = "+aC:f:hl:n:N:o:qr:sS:t:vVX::";

	while ((ch =
#if defined(HAVE_GETOPT_LONG)
	    getopt_long(argc, argv, optstring, longopts, NULL)
#else
	    getopt(argc, argv, optstring)	/* long options unavailable */
#endif
	    ) != -1)
	{
		switch (ch)
		{
			case 'n':
				/* Accumulate; multiple -n union together. */
				nflag = nflag ? join_comma(nflag, optarg)
				    : strdup(optarg);
				break;
			case 'N':
				Nflag = Nflag ? join_comma(Nflag, optarg)
				    : strdup(optarg);
				break;
			case 'a':
				aflag = 1;
				break;
			case 'f':
				fflag = atoi(optarg);
				break;
			case 't':
				tflag = atoi(optarg);
				break;
			case 's':
				sflag = 1;
				break;
			case 'v':
				inv->verify = 1;
				break;
			case 'l':
				free(lflag);
				lflag = strdup(optarg);
				break;
			case 'o':
				free(oflag);
				oflag = strdup(optarg);
				break;
			case 'r':
				free(rflag);
				rflag = strdup(optarg);
				break;
			case 'S':
				free(Sflag);
				Sflag = strdup(optarg);
				break;
			case 'C':
				free(Cflag);
				Cflag = strdup(optarg);
				break;
			case 'q':
				qflag = 1;
				break;
			case 'X':
				ignore_env = 1;
				/* optarg is set only as -Xlist (optional arg). */
				if (optarg != NULL)
				{
					free(env_keep);
					env_keep = strdup(optarg);
				}
				break;
			case 'V':
				printf("dsh.dsm (yadsh) %s\n", VERSION);
				exit(0);
			case 'h':
				exit(usage(argv[0]));
			default:
				exit(!usage(argv[0]));
		}
	}

	if (Sflag != NULL && strcmp(Sflag, "csh") != 0 &&
	    strcmp(Sflag, "ksh") != 0)
	{
		print_error("-S %s: expected csh or ksh\n", Sflag);
		exit(1);
	}

	if (Cflag != NULL)
	{
		print_error("-C %s: context modules are not implemented yet "
		    "(arrives in T15)\n", Cflag);
		exit(1);
	}

	/* DSH_DEBUG is itself a DSH_* variable, so honor -X: check it through
	 * env_get() after option parsing rather than reading getenv() up front. */
	if (env_get("DSH_DEBUG") != NULL)
		set_debug(1);
	debug_msg("DSH_DEBUG\n");

	/* Environment defaults (honoring -X), then command-line overrides. */
	inv->fanout = (envp = env_get("DSH_FANOUT")) ? atoi(envp) : 64;
	if (fflag >= 0)
		inv->fanout = fflag;
	if (inv->fanout <= 0)
		inv->fanout = 64;

	inv->timeout = (envp = env_get("DSH_TIMEOUT")) ? atoi(envp) : DSM_TIMEOUT;
	if (tflag >= 0)
		inv->timeout = tflag;

	syntax = (envp = env_get("DSH_SYNTAX")) ? envp : "ksh";
	if (Sflag != NULL)
		syntax = Sflag;
	csh = (strcmp(syntax, "csh") == 0);

	rsh = rflag ? rflag : (envp = env_get("DSH_NODE_RSH")) ? envp : NULL;
	if (rsh == NULL)
		rsh = (RSH_DFLT[0] != '\0') ? RSH_DFLT : SSH_DFLT;

	node_opts = oflag ? oflag :
	    ((envp = env_get("DSH_NODE_OPTS")) ? envp : NULL);
	dsh_path = (envp = env_get("DSH_PATH")) ? envp : DSHPATH_DFLT;
	ngpath = env_get("DSH_NODEGROUP_PATH");
	node_list_env = env_get("DSH_NODE_LIST");

	/* -s streaming disables per-target buffering; DSM buffers by default. */
	inv->collate = sflag ? 0 : 1;
	inv->hn_method = "initial_hostname";	/* resolver default; see resolv.c */

	if (qflag)
	{
		show_config(rsh, inv->fanout, inv->timeout, syntax, ngpath,
		    node_list_env);
		exit(0);
	}

#if defined(RLIMIT_NOFILE)
	if (getrlimit(RLIMIT_NOFILE, &rl) == 0 &&
	    rl.rlim_cur != RLIM_INFINITY && rl.rlim_cur > 3)
		max_fanout = MAX_FANOUT(rl.rlim_cur);
	if (max_fanout > 0 && inv->fanout > max_fanout)
	{
		print_error("fanout %d exceeds RLIMIT_NOFILE capacity; "
		    "reducing to %d\n", inv->fanout, max_fanout);
		inv->fanout = max_fanout;
	}
#endif

	/* Assemble the target set: -n list, -N groups, -a all.  DSH_NODE_LIST is
	 * a fallback only when the user named no explicit source, so an empty
	 * explicit selection never silently runs against the node-list file. */
	explicit_sel = (nflag != NULL || Nflag != NULL || aflag);

	if (nflag != NULL)
		add_list(&targets, nflag);
	if (Nflag != NULL)
	{
		char	*copy, *g, *save;
		if ((copy = strdup(Nflag)) == NULL)
		{
			perror("strdup");
			exit(1);
		}
		for (g = strtok_r(copy, ", \t", &save); g != NULL;
		     g = strtok_r(NULL, ", \t", &save))
			resolve_group(&targets, ngpath, g);
		free(copy);
	}
	if (aflag)
		resolve_all_groups(&targets, ngpath);
	if (!explicit_sel && node_list_env != NULL)
		read_node_list_file(&targets, node_list_env);

	if (targets.len == 0)
	{
		print_error("no targets specified "
		    "(use -n, -N, -a, or set DSH_NODE_LIST)\n");
		exit(1);
	}

	/* Build the remote command vector: <rsh> [opts] [-l user] HOST [path] cmd */
	push_remote_argv(inv, rsh);
	if (node_opts != NULL && *node_opts != '\0')
	{
		/* Split on whitespace so e.g. -o '-p 2222' reaches the remote
		 * shell as two arguments, not one.  (Options with embedded
		 * spaces in a value are not supported, matching the simple
		 * word-splitting dsh has always used.) */
		char	*copy, *t, *save;

		if ((copy = strdup(node_opts)) == NULL)
		{
			perror("strdup");
			exit(1);
		}
		for (t = strtok_r(copy, " \t", &save); t != NULL;
		     t = strtok_r(NULL, " \t", &save))
			push_remote_argv(inv, t);
		free(copy);
	}
	if (lflag != NULL)
	{
		push_remote_argv(inv, "-l");
		push_remote_argv(inv, lflag);
	}
	push_remote_argv(inv, NULL);		/* per-member hostname slot */

	if (dsh_path != NULL && *dsh_path != '\0')
	{
		char	*tmp;
		size_t	 n;

		if (csh)
		{
			n = strlen("setenv PATH ;") + strlen(dsh_path) + 1;
			if ((tmp = (char *)malloc(n)) == NULL)
			{
				perror("malloc");
				exit(1);
			}
			snprintf(tmp, n, "setenv PATH %s;", dsh_path);
		}
		else
		{
			n = strlen("PATH=; export PATH;") + strlen(dsh_path) + 1;
			if ((tmp = (char *)malloc(n)) == NULL)
			{
				perror("malloc");
				exit(1);
			}
			snprintf(tmp, n, "PATH=%s; export PATH;", dsh_path);
		}
		push_remote_argv(inv, tmp);
		free(tmp);
	}

	argc -= optind;
	argv += optind;
	for (i = 0; i < argc; i++)
		push_remote_argv(inv, argv[i]);
	inv->remote_cmdv = &inv->remote_argv[inv->remote_argc - argc];
	inv->cmd_argc = argc;

	debug_msg("remote_argv = ");
	for (i = 0; i < inv->remote_argc; i++)
		debug_msg("%s ",
		    (inv->remote_argv[i] ? inv->remote_argv[i] : "(NULL)"));
	debug_msg("\nremote_argc = %d\n", inv->remote_argc);

	/* Hand the assembled target list to the engine's collective loader. */
	inv->collective_arg = targets.buf;

	free(nflag);
	free(Nflag);
	free(lflag);
	free(oflag);
	free(rflag);
	free(Sflag);
	free(Cflag);
	free(env_keep);
	env_keep = NULL;
}
