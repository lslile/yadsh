/*
 *  dshbak - collate and format dsh output by host.
 *
 *  Reads "host: line" records on standard input.  Lines that dsh emits for
 *  its own bookkeeping ("fanout: ..." and "working collective (N): ...") are
 *  dropped.  Without -c each host's output is printed under a header, with
 *  hosts in sorted order.  With -c, hosts whose output is byte-for-byte
 *  identical are collapsed into a single block listing all of them.
 *
 *  Output is handled as counted byte strings, not C strings, so a remote
 *  command that emits embedded NUL bytes is reproduced and collated exactly,
 *  as the original dshbak.pl did.
 *
 *  This is a C reimplementation of the original dshbak.pl.  It differs from
 *  the perl in four deliberate ways, all of them fixes:
 *    - collation compares the actual output bytes, not an MD5 digest, so two
 *      hosts can never be grouped together by a hash collision;
 *    - the order of collated blocks is deterministic (sorted by the first
 *      host in each group) rather than perl hash order;
 *    - the progress dots for very large inputs go to stderr, so a piped
 *      stdout stays pure data;
 *    - the header rules track the terminal width when stdout is a tty (the
 *      perl hardcoded 72/78 columns); piped or redirected output keeps the
 *      fixed 78-column default, so it stays stable and reproducible.
 *
 *  Copyright (c) 2004-2026, Larry Lile <lile@users.sourceforge.net>
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *  1. Redistributions of source code must retain the above copyright
 *     notice unmodified, this list of conditions, and the following
 *     disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 *  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 *  OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 *  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 *  OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 *  SUCH DAMAGE.
 */

#include <sys/ioctl.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* A counted byte string.  p is always NUL-terminated for convenience, but len
 * is authoritative and the bytes may themselves contain NULs. */
struct blob {
	char	*p;
	size_t	 len;
};

struct host {
	struct blob	 name;
	struct blob	*lines;		/* output lines, in arrival order */
	size_t		 nlines;
	size_t		 cap;
	struct blob	 sig;		/* -c: lines joined with '\n' */
	struct host	*next;		/* hash-bucket chain */
};

#define NBUCKETS 1024
static struct host	 *buckets[NBUCKETS];
static struct host	**hosts;	/* every host, in first-seen order */
static size_t		  nhosts;
static size_t		  hostcap;

/*
 * Width of the header rules.  When stdout is a terminal we fill its width;
 * otherwise (a pipe or file) we use a fixed default so redirected output is
 * stable and reproducible.
 */
#define DEFAULT_WIDTH	78
#define MIN_WIDTH	10
static size_t		  g_width = DEFAULT_WIDTH;

/* A collation group is a run of equal-signature hosts in the sorted array. */
struct grp {
	size_t	start;
	size_t	count;
};

static void *
xmalloc(size_t n)
{
	void *p = malloc(n);
	if (p == NULL) {
		perror("dshbak");
		exit(2);
	}
	return p;
}

static void *
xrealloc(void *p, size_t n)
{
	p = realloc(p, n);
	if (p == NULL) {
		perror("dshbak");
		exit(2);
	}
	return p;
}

/* Copy len bytes into a fresh, NUL-terminated blob. */
static struct blob
blobdup(const char *src, size_t len)
{
	struct blob b;
	b.p = xmalloc(len + 1);
	memcpy(b.p, src, len);
	b.p[len] = '\0';
	b.len = len;
	return b;
}

/* Byte-wise lexicographic compare, NUL-safe, shorter sorts first on a tie.
 * This matches perl's string cmp, which the original used to sort hosts. */
static int
blobcmp(const struct blob *a, const struct blob *b)
{
	size_t m = a->len < b->len ? a->len : b->len;
	int r = m ? memcmp(a->p, b->p, m) : 0;
	if (r != 0)
		return r;
	if (a->len != b->len)
		return a->len < b->len ? -1 : 1;
	return 0;
}

static unsigned long
hashbytes(const char *s, size_t len)
{
	unsigned long h = 5381UL;
	size_t i;

	for (i = 0; i < len; i++)
		h = ((h << 5) + h) + (unsigned long)(unsigned char)s[i];
	return h;
}

static struct host *
find_or_create(const char *name, size_t namelen)
{
	unsigned long b = hashbytes(name, namelen) % NBUCKETS;
	struct host *p;

	for (p = buckets[b]; p != NULL; p = p->next)
		if (p->name.len == namelen &&
		    memcmp(p->name.p, name, namelen) == 0)
			return p;

	p = xmalloc(sizeof(*p));
	p->name = blobdup(name, namelen);
	p->lines = NULL;
	p->nlines = 0;
	p->cap = 0;
	p->sig.p = NULL;
	p->sig.len = 0;
	p->next = buckets[b];
	buckets[b] = p;

	if (nhosts == hostcap) {
		hostcap = hostcap ? hostcap * 2 : 64;
		hosts = xrealloc(hosts, hostcap * sizeof(*hosts));
	}
	hosts[nhosts++] = p;
	return p;
}

static void
add_line(struct host *p, const char *line, size_t len)
{
	if (p->nlines == p->cap) {
		p->cap = p->cap ? p->cap * 2 : 8;
		p->lines = xrealloc(p->lines, p->cap * sizeof(*p->lines));
	}
	p->lines[p->nlines++] = blobdup(line, len);
}

/*
 * dsh's own status lines are not host output: "fanout: ..." and any host
 * matching "working collective (<digits>)".  Matches the perl filter, case
 * insensitively, over the raw host bytes.
 */
static int
is_status_host(const char *host, size_t len)
{
	static const char fan[] = "fanout";
	static const char pat[] = "working collective (";
	const size_t fanlen = sizeof(fan) - 1;
	const size_t patlen = sizeof(pat) - 1;
	char *lo = xmalloc(len + 1);
	int skip = 0;
	size_t i;

	for (i = 0; i < len; i++)
		lo[i] = (char)tolower((unsigned char)host[i]);
	lo[len] = '\0';

	if (len == fanlen && memcmp(lo, fan, fanlen) == 0) {
		skip = 1;
	} else if (len > patlen) {
		/* Find "working collective (" followed by digits and ')'. */
		for (i = 0; i + patlen <= len; i++) {
			if (memcmp(lo + i, pat, patlen) != 0)
				continue;
			size_t q = i + patlen;
			if (q < len && isdigit((unsigned char)lo[q])) {
				while (q < len && isdigit((unsigned char)lo[q]))
					q++;
				if (q < len && lo[q] == ')') {
					skip = 1;
					break;
				}
			}
		}
	}

	free(lo);
	return skip;
}

static int
usage(void)
{
	fputs("dshbak [-c]\n", stdout);
	return 1;
}

static int
cmp_name(const void *a, const void *b)
{
	const struct host *x = *(const struct host *const *)a;
	const struct host *y = *(const struct host *const *)b;
	return blobcmp(&x->name, &y->name);
}

/* Order hosts by output first (to form groups), then by name within a group. */
static int
cmp_sig_name(const void *a, const void *b)
{
	const struct host *x = *(const struct host *const *)a;
	const struct host *y = *(const struct host *const *)b;
	int r = blobcmp(&x->sig, &y->sig);
	return r != 0 ? r : blobcmp(&x->name, &y->name);
}

/* Order groups by their first host's name (the smallest, since each run is
 * already name-sorted).  Host names are unique, so this is a total order. */
static int
cmp_group(const void *a, const void *b)
{
	const struct grp *x = a;
	const struct grp *y = b;
	return blobcmp(&hosts[x->start]->name, &hosts[y->start]->name);
}

static struct blob
build_sig(struct host *p)
{
	struct blob sig;
	size_t total = 0, i;
	char *w;

	for (i = 0; i < p->nlines; i++)
		total += p->lines[i].len + 1;	/* line + '\n' */

	sig.p = xmalloc(total + 1);
	w = sig.p;
	for (i = 0; i < p->nlines; i++) {
		memcpy(w, p->lines[i].p, p->lines[i].len);
		w += p->lines[i].len;
		*w++ = '\n';
	}
	*w = '\0';
	sig.len = total;
	return sig;
}

/*
 * Build the host line for a collated block: names sorted, each padded to the
 * widest name plus two trailing spaces, wrapping near the output width.  This
 * mirrors the perl layout exactly, including trailing padding.  Returns a malloc'd
 * buffer and its length (host names may contain arbitrary bytes).
 */
static char *
build_hostline(struct host *const *members, size_t n, size_t *outlen)
{
	size_t longest = 0, i;
	size_t cap = 256, used = 0;
	size_t width = 0;		/* perl $length: width of current row */
	char *s = xmalloc(cap);

	for (i = 0; i < n; i++)
		if (members[i]->name.len > longest)
			longest = members[i]->name.len;

	for (i = 0; i < n; i++) {
		size_t nl = members[i]->name.len;
		size_t pad = (longest - nl) + 2;
		size_t add = nl + pad;		/* this entry's column width: longest + 2 */
		size_t need = used + 1 + add;	/* maybe a '\n', then the entry */

		if (need > cap) {
			while (need > cap)
				cap *= 2;
			s = xrealloc(s, cap);
		}

		/*
		 * Wrap before this entry only if the row already holds one and
		 * the entry, padding included, would overflow the width.  A lone
		 * entry wider than the width still goes on its own row, with no
		 * leading blank line.
		 */
		if (width > 0 && width + add > g_width) {
			s[used++] = '\n';
			width = 0;
		}

		memcpy(s + used, members[i]->name.p, nl);
		used += nl;
		memset(s + used, ' ', pad);
		used += pad;
		width += add;
	}

	*outlen = used;
	return s;
}

static size_t
output_width(void)
{
	struct winsize ws;

	if (isatty(STDOUT_FILENO) &&
	    ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
		return (size_t)ws.ws_col < MIN_WIDTH ? MIN_WIDTH
		    : (size_t)ws.ws_col;
	return DEFAULT_WIDTH;
}

static void
print_block(const char *hostline, size_t hllen, const struct blob *lines,
    size_t n)
{
	size_t i;

	fputs("HOSTS ", stdout);
	for (i = 0; i + 6 < g_width; i++)	/* "HOSTS " is 6 columns wide */
		putchar('-');
	putchar('\n');
	fwrite(hostline, 1, hllen, stdout);
	putchar('\n');
	for (i = 0; i < g_width; i++)
		putchar('-');
	putchar('\n');
	for (i = 0; i < n; i++) {
		fwrite(lines[i].p, 1, lines[i].len, stdout);
		putchar('\n');
	}
}

int
main(int argc, char **argv)
{
	int opt_c = 0, c;
	char *line = NULL;
	size_t cap = 0;
	ssize_t len;
	unsigned long filtered = 0;
	size_t i;

	opterr = 0;
	while ((c = getopt(argc, argv, "ch")) != -1) {
		switch (c) {
		case 'c':
			opt_c = 1;
			break;
		case 'h':
			usage();
			return 0;
		default:
			usage();
			return 1;
		}
	}

	g_width = output_width();

	while ((len = getline(&line, &cap, stdin)) != -1) {
		size_t llen = (size_t)len;
		size_t sep, hlen, olen;
		const char *output;
		int found = 0;

		if (llen > 0 && line[llen - 1] == '\n')
			llen--;			/* drop the trailing newline */

		for (i = 0; i + 1 < llen; i++) {
			if (line[i] == ':' && line[i + 1] == ' ') {
				sep = i;
				found = 1;
				break;
			}
		}
		if (!found)
			continue;		/* no "host: output" split */

		hlen = sep;
		while (hlen > 0 && isspace((unsigned char)line[hlen - 1]))
			hlen--;
		if (hlen == 0)
			continue;		/* empty host name */

		output = line + sep + 2;
		olen = llen - (sep + 2);
		if (olen == 0)
			continue;		/* no output on this line */

		if (is_status_host(line, hlen))
			continue;

		add_line(find_or_create(line, hlen), output, olen);

		if (++filtered % 1000 == 0)
			fputc('.', stderr);
	}
	free(line);
	if (filtered > 1000)
		fputc('\n', stderr);

	if (opt_c) {
		struct grp *groups = NULL;
		size_t ng = 0, gcap = 0;

		for (i = 0; i < nhosts; i++)
			hosts[i]->sig = build_sig(hosts[i]);
		qsort(hosts, nhosts, sizeof(*hosts), cmp_sig_name);

		i = 0;
		while (i < nhosts) {
			size_t j = i + 1;
			while (j < nhosts &&
			    blobcmp(&hosts[j]->sig, &hosts[i]->sig) == 0)
				j++;
			if (ng == gcap) {
				gcap = gcap ? gcap * 2 : 16;
				groups = xrealloc(groups, gcap * sizeof(*groups));
			}
			groups[ng].start = i;
			groups[ng].count = j - i;
			ng++;
			i = j;
		}
		qsort(groups, ng, sizeof(*groups), cmp_group);

		for (i = 0; i < ng; i++) {
			struct host *first = hosts[groups[i].start];
			size_t hllen;
			char *hl = build_hostline(&hosts[groups[i].start],
			    groups[i].count, &hllen);
			print_block(hl, hllen, first->lines, first->nlines);
			free(hl);
		}
		free(groups);
	} else {
		qsort(hosts, nhosts, sizeof(*hosts), cmp_name);
		for (i = 0; i < nhosts; i++)
			print_block(hosts[i]->name.p, hosts[i]->name.len,
			    hosts[i]->lines, hosts[i]->nlines);
	}

	return 0;
}
