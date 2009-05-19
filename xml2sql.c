/*
 * xml2sql, MediaWiki XML to SQL converter.
 * Copyright (C) Tietew.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 * tietew@tietew.net
 */

#include "config.h"

#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>
#ifdef HAVE_STDLIB_H
# include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
# include <string.h>
#else
# include <strings.h>
#endif
#ifdef HAVE_MALLOC_H
# include <malloc.h>
#endif
#ifdef HAVE_FCNTL_H
# include <fcntl.h>
#endif
#ifdef HAVE_GETOPT_LONG
# include <getopt.h>
#else
# include "getopt/getopt.h"
#endif
#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#ifdef HAVE_SYS_STAT_H
# include <sys/stat.h>
#endif
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#ifdef HAVE_LIBZ
# include <zlib.h>
#endif
#include <expat.h>

#ifdef _WIN32
# define WIN32_LEAN_AND_MEAN
# include <windows.h>
#endif

#include "md5.h"
#include "st.h"
#include "mediawiki.c"
#include "missing.h"

/* random.c */
void init_genrand(unsigned long s);
double genrand_real2(void);

/* read buffer size */
#define BUFFERSIZE 65536

/* compress threashold */
#define COMPRESS_THREASHOLD 512

typedef unsigned char byte;

#ifdef HAVE_LIBZ
static const char *sopts = "imp::c::rN:to:T:vh";
#else
static const char *sopts = "imp::rN:to:vh";
#endif

static const struct option lopts[] = {
	{ "import",     0, 0, 'i' },
	{ "mysql",      0, 0, 'm' },
	{ "postgresql", 2, 0, 'p' },
#ifdef HAVE_LIBZ
	{ "compress",   2, 0, 'c' },
	{ "tmpdir",     1, 0, 'T' },
#endif
	{ "renumber",   0, 0, 'r' },
	{ "namespace",  1, 0, 'N' },
	{ "no-text",    0, 0, 't' },
	{ "output-dir", 1, 0, 'o' },
	{ "verbose",    0, 0, 'v' },
	{ "help",       0, 0, 'h' },
	{ "version",    0, 0, 'V' },
	{ NULL, 0, 0, 0 },
};

static const char *version =
PACKAGE_STRING " Copyright (C) Tietew.\n";
static const char *helpmesg = 
"This is " PACKAGE_NAME ", MediaWiki XML to SQL converter\n\n"
"usage: %s [options]... [XMLFILE]\n"
"Input MediaWiki XML from XMLFILE or stdin and output table data\n"
"to page.txt, revision.txt and text.txt (-i) or page.sql, revision.sql\n"
"and text.sql (-m or -p).\n\n"
"Options:\n"
"  -i, --import               output mysqlimport format (default)\n"
"  -m, --mysql                output MySQL INSERT command\n"
"  -p, --postgresql[=ver]     output PostgreSQL COPY command\n"
"                               (default: 8.0 and ealier)\n"
#ifdef HAVE_LIBZ
"  -c, --compress[={old,full}]\n"
"                             compress text table (default: old)\n"
#endif
"  -r, --renumber             renumber page_id and rev_id\n"
"  -N, --namespace=ns,ns,...  output only specific namespace(s)\n"
"  -t, --no-text              does not output text table\n"
"  -o, --output-dir=OUTDIR    output directory (default: .)\n"
#ifdef HAVE_LIBZ
"  -T, --tmpdir=TMPDIR        temporary directory (default: OUTDIR)\n"
#endif
"  -v, --verbose              show progress\n"
"  -h, --help                 display this help\n"
"      --version              output version information\n";

enum tabletype { import, mysql, postgres } tabletype = import;
enum compresstype { gz_none, gz_old, gz_full } gzipcompress = gz_none;
int verbose = 0;
int xsurrogate = 0;
int notext = 0;
int fdin = 0; /* stdin */
int fdtmp;
const char *prog;
const char *outdir;
const char *tmpdir;
char *namespaces;
unsigned long page_id, rev_id;

char buf[BUFFERSIZE];

struct ns {
	const char *name;
	size_t len;
	int ns;
	int out;
};
struct ns *ns = NULL;
size_t nssize = 0, nscapa = 0;
int ns_key;

enum element current;
enum element elstack[64];
int elidx = -1;

char *text;
size_t tlen, tcapa;

struct text {
	char md5[MD5_DIGEST_STRING_LENGTH];
	size_t textlen;
	unsigned long id;
};

struct page {
	unsigned long id;
	int ns;
	char *title;
	char *restrictions;
	char *lastts;
	unsigned long lastid;
	unsigned long lasttid;
	int redir;
	size_t len;
	struct text *texts;
	size_t tlen;
	int skip;
};
struct revision {
	unsigned long id;
	char *timestamp;
	char *username;
	char *ip;
	unsigned long user_id;
	char *comment;
	int minor;
	char *text;
	char md5[MD5_DIGEST_STRING_LENGTH];
};
struct page page;
struct revision revision;

struct table {
	char *name;
	FILE *fp;
	unsigned long l;
	unsigned long c;
};
struct table page_tbl, rev_tbl, text_tbl;

st_table *st;

void md5(const char *s, char *digest)
{
	MD5_CTX md;
	
	MD5_Init(&md);
	MD5_Update(&md, (const uint8_t *)s, strlen(s));
	MD5_End(&md, (uint8_t *)digest);
}

void fatal(const char *mesg)
{
	if(!mesg) mesg = strerror(errno);
	fprintf(stderr, "%s: %s\n", prog, mesg);
	exit(1);
}

void fatal2(const char *mesg1, const char *mesg2)
{
	if(!mesg2) mesg2 = strerror(errno);
	fprintf(stderr, "%s: %s: %s\n", prog, mesg1, mesg2);
	exit(1);
}

void nomem()
{
	fatal(strerror(ENOMEM));
}

void parse_options(int argc, char *argv[])
{
	int c, idx;
	
	prog = strdup(argv[0]);
	while((c = getopt_long(argc, argv, sopts, lopts, &idx)) != -1) {
		switch(c) {
		case 'i':
			tabletype = import;
			break;
		
		case 'm':
			tabletype = mysql;
			break;
		
		case 'p':
			tabletype = postgres;
			if(optarg == 0 || strcmp(optarg, "8.1") < 0)
				xsurrogate = 1;
			break;
		
#ifdef HAVE_LIBZ
		case 'c':
			if(optarg == 0 || strcmp(optarg, "old") == 0)
				gzipcompress = gz_old;
			else if(strcmp(optarg, "full") == 0)
				gzipcompress = gz_full;
			else {
				fprintf(stderr, "Invalid argument %s for -c\n", optarg);
				goto error;
			}
			break;
		
		case 'T':
			tmpdir = optarg;
			break;
#endif
		
		case 'r':
			page_id = rev_id = 1;
			break;
		
		case 'N':
			namespaces = strdup(optarg);
			break;
		
		case 't':
			notext = 1;
			break;
		
		case 'o':
			outdir = optarg;
			break;
		
		case 'v':
			verbose = 1;
			break;
		
		case 'h':
			printf(helpmesg, prog);
			exit(0);
		
		case 'V':
			printf(version);
			exit(0);
		
		case '?':
		default:
		error:
			fprintf(stderr, "Try `%s --help' for more information.\n", prog);
			exit(1);
		}
	}
	
	if(optind == argc - 1) {
		if(strcmp(argv[optind], "-") != 0) {
			fdin = open(argv[optind], O_RDONLY | O_BINARY);
			if(fdin == -1) fatal2(argv[optind], 0);
		}
	} else if(optind != argc) {
		fprintf(stderr, "Too many arguments.\n");
		goto error;
	}
#ifdef HAVE_LIBZ
	if(notext || tabletype == postgres) {
		gzipcompress = gz_none;
	}
#endif
}

char *replace(char *str, char from, char to)
{
	char *c;
	for(c = str; *c; ++c) if(*c == from) *c = to;
	return str;
}

void starttable(struct table *t, const char *tbl)
{
	char fname[256];
	
	strcpy(fname, tbl);
	strcat(fname, tabletype == import ? ".txt" : ".sql");
	
	t->fp = fopen(fname, "wb");
	if(!t->fp) fatal2(fname, 0);
	t->l = 0;
	t->c = 0;
	t->name = strdup(tbl);
	
	switch(tabletype) {
	case mysql:
		fprintf(t->fp,
			"-- xml2sql - MediaWiki XML to SQL converter\n"
			"-- Table %s for MySQL\n"
			"\n"
			"/*!40000 ALTER TABLE `%s` DISABLE KEYS */;\n"
			"LOCK TABLES `%s` WRITE;\n",
			t->name, t->name, t->name);
		break;
	
	case postgres:
		fprintf(t->fp,
			"-- xml2sql - MediaWiki XML to SQL converter\n"
			"-- Table %s for PostgreSQL\n"
			"\n"
			"COPY \"%s\" FROM STDIN;\n",
			t->name, t->name);
		break;
	}
}

void fintable(struct table *t)
{
	switch(tabletype) {
	case import:
		break;
	
	case mysql:
		fprintf(t->fp,
			";\n"
			"UNLOCK TABLES;\n"
			"/*!40000 ALTER TABLE `%s` ENABLE KEYS */;\n\n",
			t->name);
		break;
	
	case postgres:
		fputs("\\.\n\n", t->fp);
		break;
	}
	if(ferror(t->fp)) fatal2(t->name, 0);
	
	fclose(t->fp);
	free(t->name);
}

void startrecord(struct table *t)
{
	switch(tabletype) {
	case mysql:
		if((t->l & 0xFF) == 0) {
			if(t->l != 0) fputs(";\n", t->fp);
			fprintf(t->fp, "INSERT INTO `%s` VALUES ", t->name);
		} else {
			putc(',', t->fp);
		}
		putc('(', t->fp);
		break;
	}
	t->c = 0;
}

void finrecord(struct table *t)
{
	switch(tabletype) {
	case mysql:
		putc(')', t->fp);
		break;
	
	default:
		putc('\n', t->fp);
		break;
	}
	++t->l;
	
	if(ferror(t->fp)) fatal2(t->name, 0);
}

void startcolumn(struct table *t, int quote)
{
	if(tabletype == mysql) {
		if(t->c != 0) putc(',', t->fp);
		if(quote) putc('\'', t->fp);
	} else {
		if(t->c != 0) putc('\t', t->fp);
	}
}

void fincolumn(struct table *t, int quote)
{
	if(tabletype == mysql && quote) putc('\'', t->fp);
	++t->c;
}

void chwarn(char *mesg, int drop)
{
	fprintf(stderr,
		"Warning: page \"%s\" (id=%lu) contains %s; %s.\n",
		page.title, page.id, mesg,
		drop ? "dropped" : "replaced to '?'");
}

static const unsigned long utf8_limits[] = {
	0x0,		/* 1 */
	0x80,		/* 2 */
	0x800,		/* 3 */
	0x10000,	/* 4 */
	0x200000,	/* 5 */
	0x4000000,	/* 6 */
	0x80000000,	/* 7 */
};

size_t putpgutf8(const byte *s, size_t i, size_t len, FILE *fp)
{
	size_t u, ulen;
	unsigned long uv;
	
	ulen = 1;
	uv = s[i];
	if(!(s[i] & 0x40)) {
	malform:
		chwarn("malformed UTF-8 sequence", 0);
		putc('?', fp);
		return 1;
	}
	if     (!(s[i] & 0x20)) { ulen = 2; uv &= 0x1F; }
	else if(!(s[i] & 0x10)) { ulen = 3; uv &= 0x0F; }
	else if(!(s[i] & 0x08)) { ulen = 4; uv &= 0x07; }
	else if(!(s[i] & 0x04)) { ulen = 5; uv &= 0x03; }
	else if(!(s[i] & 0x02)) { ulen = 6; uv &= 0x01; }
	else                    goto malform;
	
	if(ulen > (len - i)) {
		chwarn("malformed UTF-8 sequence", 1);
		return (len - i);
	}
	
	for(u = 1; u < ulen; ++u) {
		if((s[i + u] & 0xC0) != 0x80) {
			i += u - 1;
			goto malform;
		}
		uv = (uv << 6) | (s[i + u] & 0x3F);
	}
	if(uv < utf8_limits[ulen - 1]) {
		chwarn("redunrant UTF-8 sequence", 0);
		putc('?', fp);
		return ulen;
	}
	if(xsurrogate && uv >= 0x10000 && uv <= 0x1FFFFF) {
		uv -= 0x100000;
		/* high surrogate (U+D800 - U+DBFF)
		   1101 10xx xxxx xxxx
		               to
		   11101101 1010xxxx 10xxxxxx */
		putc(0xED, fp);
		putc(0xA0 | ((uv >> 18) & 0x0F), fp);
		putc(0x80 | ((uv >> 10) & 0x3F), fp);
		/* low surrogate (U+DC00 - U+DFFF)
		   1101 11xx xxxx xxxx
		               to
		   11101101 1011xxxx 10xxxxxx */
		putc(0xED, fp);
		putc(0xB0 | ((uv >> 6) & 0x0F), fp);
		putc(0x80 | (uv & 0x3F), fp);
		return ulen;
	} else if(uv >= 0x200000) {
		chwarn("a character out of UTF-16 (over U+200000)", 0);
		putc('?', fp);
		return ulen;
	} else {
		for(u = 0; u < ulen; ++u) putc(s[i + u], fp);
	}
	return ulen;
}

void putcolumnstr(struct table *t, const byte *s, size_t len, int bin)
{
	size_t i;
	for(i = 0; i < len; ++i) {
		switch(s[i]) {
		case '\0':
			if(!bin) { chwarn("NULL character", 1); break; }
			putc('\\', t->fp); putc('0', t->fp);
			break;
		case '\r':
			putc('\\', t->fp); putc('r', t->fp);
			break;
		case '\n':
			putc('\\', t->fp); putc('n', t->fp);
			break;
		case '\t':
			putc('\\', t->fp); putc('t', t->fp);
			break;
		case '\\':
			putc('\\', t->fp); putc('\\', t->fp);
			break;
		case '\'': case '\"':
			if(tabletype == mysql) putc('\\', t->fp);
			putc(s[i], t->fp);
			break;
		default:
			if(!bin && (s[i] & 0x80)) {
				i += putpgutf8(s, i, len, t->fp) - 1;
			} else {
				putc(s[i], t->fp);
			}
		}
	}
}

void putcolumn(struct table *t, const char *s, int quote)
{
	startcolumn(t, quote);
	if(s) putcolumnstr(t, (const byte *)s, strlen(s), 0);
	fincolumn(t, quote);
}

void putcolumnf(struct table *t, const char *f, ...)
{
	va_list va;
	
	va_start(va, f);
	startcolumn(t, 0);
	vfprintf(t->fp, f, va);
	fincolumn(t, 0);
	va_end(va);
}

#ifdef HAVE_LIBZ
int putgz(struct table *t, const char *s)
{
	byte *zbuf;
	z_stream z;
	int r;
	
	if(!s) { putcolumn(t, s, 1); return 0; }
	
	z.data_type = Z_ASCII;
	z.zalloc = Z_NULL;
	z.zfree = Z_NULL;
	z.opaque = Z_NULL;
	z.next_in = (Bytef *)s;
	z.avail_in = strlen(s);
	
	if(z.avail_in < COMPRESS_THREASHOLD) { putcolumn(t, s, 1); return 0; }
	
	z.avail_out = z.avail_in + z.avail_in / 1000 + 16;
	z.next_out = zbuf = malloc(z.avail_out);
	if(!zbuf) nomem();
	
	r = deflateInit2(&z, Z_BEST_COMPRESSION,
		Z_DEFLATED, -MAX_WBITS, MAX_MEM_LEVEL,
		Z_DEFAULT_STRATEGY);
	if(r != Z_OK) {
	err:
		free(zbuf);
		fatal2("zlib", z.msg);
	}
	
	r = deflate(&z, Z_FINISH);
	if(r != Z_STREAM_END) goto err;
	
	startcolumn(t, 1);
	putcolumnstr(t, zbuf, z.total_out, 1);
	fincolumn(t, 1);
	
	free(zbuf);
	deflateEnd(&z);
	
	return 1;
}
#else /* HAVE_ZLIB */
inline int putgz(struct table *t, const char *s)
{
	putcolumn(t, s, 1);
	return 0;
}
#endif /* HAVE_ZLIB */

void puttimestamp(struct table *t, const char *s)
{
	switch(tabletype) {
	case postgres:
		putcolumn(t, s, 1);
		break;
	
	default:
		startcolumn(t, 1);
		if(s) for(; *s; ++s) if(isdigit(*s)) putc(*s, t->fp);
		fincolumn(t, 1);
		break;
	}
}

void puttext(struct text *t, const char *text, int latest)
{
	int gz = 0;
	
#ifdef HAVE_LIBZ
	switch(gzipcompress) {
	case gz_old:
		gz = text && !latest;
		break;
	case gz_full:
		gz = !!text;
		break;
	}
#endif
	
	startrecord(&text_tbl);
	/* old_id */
	putcolumnf(&text_tbl, "%lu", t->id);
	/* old_text */
	if(gz) {
		gz = putgz(&text_tbl, text);
	} else {
		putcolumn(&text_tbl, text, 1);
	}
	/* old_flags */
	putcolumn(&text_tbl, gz ? "gzip" : "", 1);
	finrecord(&text_tbl);
}

void putrevision()
{
	unsigned long textid;
	struct text *t;
	size_t n, w;
	
	if(page_id != 0 || rev_id != 0 || revision.id == 0)
		revision.id = rev_id++;
	
	textid = -1;
	if(page.texts)
		for(n = 0; n < page.tlen; ++n)
			if(strcmp(page.texts[n].md5, revision.md5) == 0) {
				textid = page.texts[n].id;
				break;
			}
	if(textid == -1) {
		page.texts = realloc(page.texts,
			(page.tlen + 1) * sizeof(struct text));
		if(!page.texts) nomem();
		t = &page.texts[page.tlen];
		
		strcpy(t->md5, revision.md5);
		t->id = revision.id;
		t->textlen = revision.text ? strlen(revision.text) : 0;
		textid = revision.id;
		
#ifdef HAVE_LIBZ
		if(gzipcompress == gz_old) {
			if(revision.text) {
				w = write(fdtmp, revision.text, t->textlen);
				if(w != t->textlen) fatal(0);
			}
		} else
#endif
		if(!notext) {
			puttext(t, revision.text, 0);
		}
		++page.tlen;
	}
	
	startrecord(&rev_tbl);
	/* rev_id */
	putcolumnf(&rev_tbl, "%lu", revision.id);
	/* rev_page */
	putcolumnf(&rev_tbl, "%lu", page.id);
	/* rev_text_id */
	putcolumnf(&rev_tbl, "%lu", textid);
	/* rev_comment */
	putcolumn(&rev_tbl, revision.comment, 1);
	/* rev_user_id */
	putcolumnf(&rev_tbl, "%lu", revision.user_id);
	/* rev_user_text */
	putcolumn(&rev_tbl, revision.ip ? revision.ip : revision.username, 1);
	/* rev_timestamp */
	puttimestamp(&rev_tbl, revision.timestamp);
	/* rev_minor_edit */
	putcolumnf(&rev_tbl, "%d", revision.minor);
	/* rev_deleted */
	putcolumn(&rev_tbl, "0", 0);
	finrecord(&rev_tbl);
	
	if(page.lastts == 0 || strcmp(page.lastts, revision.timestamp) < 0) {
		free(page.lastts);
		page.lastid = revision.id;
		page.lasttid = textid;
		page.lastts = strdup(revision.timestamp);
		if(revision.text) {
			page.redir =
				strncasecmp(revision.text, "#REDIRECT", 9) == 0 &&
				strstr(revision.text, "[[");
			page.len = strlen(revision.text);
		}
	}
}

void putpage()
{
	struct text *t;
	char *text;
	size_t n, r, capa;
	
	if(page_id != 0 || page.id == 0)
		page.id = page_id++;
	
	startrecord(&page_tbl);
	/* page_id */
	putcolumnf(&page_tbl, "%lu", page.id);
	/* page_namespace */
	putcolumnf(&page_tbl, "%d", page.ns);
	/* page_title */
	putcolumn(&page_tbl, page.title, 1);
	/* page_restrictions */
	putcolumn(&page_tbl, page.restrictions, 1);
	/* page_counter */
	putcolumn(&page_tbl, "0", 0);
	/* page_is_redirect */
	putcolumnf(&page_tbl, "%d", page.redir);
	/* page_is_new */
	putcolumnf(&page_tbl, "%d", page.tlen == 1 ? 1 : 0);
	/* page_random */
	putcolumnf(&page_tbl, "%.17g", genrand_real2());
	/* page_touched */
	puttimestamp(&page_tbl, page.lastts);
	/* page_latest */
	putcolumnf(&page_tbl, "%lu", page.lastid);
	/* page_len */
	putcolumnf(&page_tbl, "%lu", (unsigned long)page.len);
	finrecord(&page_tbl);
	
#ifdef HAVE_LIBZ
	if(gzipcompress == gz_old) {
		lseek(fdtmp, 0, SEEK_SET);
		text = 0;
		capa = 0;
		
		for(n = 0; n < page.tlen; ++n) {
			t = &page.texts[n];
			if(capa < t->textlen + 1) {
				text = realloc(text, t->textlen + 1);
				if(!text) nomem();
				capa = t->textlen + 1;
			}
			
			r = read(fdtmp, text, t->textlen);
			if(r != t->textlen) fatal(0);
			text[t->textlen] = 0;
			
			puttext(t, text, t->id == page.lasttid);
		}
		
		free(text);
	}
#endif
}

void XMLCALL xstart(void *data, const char *el, const char **attr)
{
	const struct eltmap *elm;
	
	elm = lu_elt(el, strlen(el));
	if(!elm) {
	une:
		fprintf(stderr, "unexpected element <%s>\n", el);
	err:
		XML_StopParser((XML_Parser)data, XML_FALSE);
		return;
	}
	
	if(elidx == 63) {
		fprintf(stderr, "element nest too deep\n");
		goto err;
	}
	if(elidx == -1 && elm->t != el_mediawiki) goto une;
	elstack[++elidx] = current = elm->t;
	
	switch(current) {
	case el_mediawiki:
		/* open tables */
		starttable(&page_tbl, "page");
		starttable(&rev_tbl, "revision");
		if(!notext) starttable(&text_tbl, "text");
		break;
	
	case el_namespace:
		if(strcmp(attr[0], "key") != 0 || !attr[1]) {
			fprintf(stderr, "namespace does not contains attribute `key'\n");
			goto err;
		}
		ns_key = atoi(attr[1]);
		break;
	
	case el_minor:
		revision.minor = 1;
		break;
	
#ifdef HAVE_LIBZ
	case el_page:
		if(gzipcompress == gz_old) lseek(fdtmp, 0, SEEK_SET);
		break;
#endif
	}
	
	tlen = 0;
}

void XMLCALL xend(void *data, const char *el)
{
	size_t n, nslen;
	st_data_t stdata;
	char *nst;
	int nsi;
	
	switch(current) {
	case el_namespace:
		if(nssize + 1 > nscapa) {
			if(nscapa == 0) nscapa = 20;
			else nscapa *= 2;
			ns = realloc(ns, nscapa * sizeof(struct ns));
			if(!ns) nomem();
		}
		if(tlen != 0) {
			nst = malloc(tlen + 2);
			if(!nst) nomem();
			memcpy(nst, text, tlen);
			nst[tlen] = ':';
			nst[tlen + 1] = 0;
			ns[nssize].name = replace(nst, ' ', '_');
			ns[nssize].len = tlen + 1;
		} else {
			ns[nssize].name = 0;
			ns[nssize].len = 0;
		}
		ns[nssize].ns = ns_key;
		ns[nssize].out = 0;
		++nssize;
		break;
	
	case el_namespaces:
		if(namespaces) {
			for(nst = strtok(namespaces, ","); nst; nst = strtok(NULL, ",")) {
				if(strspn(nst, "0123456789") == strlen(nst)) {
					nsi = atoi(nst);
					for(n = 0; n < nssize; ++n)
						if(ns[n].ns == nsi) {
							ns[n].out = 1;
							break;
						}
				} else {
					replace(nst, ' ', '_');
					for(n = 0; n < nssize; ++n)
						if(strcasecmp(ns[n].name, nst) == 0) {
							ns[n].out = 1;
							break;
						}
				}
				if(n == nssize) {
					fprintf(stderr, "Warning: namespace `%s' not found.\n",
						nst);
				}
			}
		} else {
			for(n = 0; n < nssize; ++n) ns[n].out = 1;
		}
		break;
	
	case el_page:
		if(!page.skip) putpage();
		
		free(page.title);
		free(page.restrictions);
		free(page.lastts);
		free(page.texts);
		memset(&page, 0, sizeof(page));
		break;
	
	case el_revision:
		if(page.skip) break;
		
		putrevision();
		
		free(revision.timestamp);
		free(revision.username);
		free(revision.ip);
		free(revision.comment);
		free(revision.text);
		memset(&revision, 0, sizeof(revision));
		
		if(verbose && (rev_tbl.l & 0xFF) == 0) {
			printf("\rconverted %lu pages, %lu revisions, %lu texts...",
				page_tbl.l, rev_tbl.l, text_tbl.l);
			fflush(stdout);
		}
		
		break;
	
	case el_id:
		switch(elstack[elidx - 1]) {
		case el_page:
			page.id = strtoul(text, NULL, 0);
			break;
		
		case el_revision:
			revision.id = strtoul(text, NULL, 0);
			break;
		
		case el_contributor:
			revision.user_id = strtoul(text, NULL, 0);
			break;
		}
		break;
	
	case el_title:
		replace(text, ' ', '_');
		
		if(st_lookup(st, (st_data_t)text, &stdata)) {
			fprintf(stderr, "Warning: page `%s' duplicate; skip.\n", text);
			page.skip = 1;
			break;
		}
		st_add_direct(st, (st_data_t)strdup(text), 0);
		
		nslen = 0;
		for(n = 0; n < nssize; ++n) {
			if(ns[n].name && strncmp(text, ns[n].name, ns[n].len) == 0) {
				page.ns = ns[n].ns;
				nslen = ns[n].len;
				if(!ns[n].out) page.skip = 2;
				break;
			}
		}
		page.title = strdup(text + nslen);
		
		break;
	
	case el_restrictions:
		if(!page.skip && tlen) page.restrictions = strdup(text);
		break;
	
	case el_timestamp:
		if(!page.skip && tlen) revision.timestamp = strdup(text);
		break;
	
	case el_username:
		if(!page.skip && tlen) revision.username = strdup(text);
		break;
	
	case el_ip:
		if(!page.skip && tlen) revision.ip = strdup(text);
		break;
	
	case el_comment:
		if(!page.skip && tlen) revision.comment = strdup(text);
		break;
	
	case el_text:
		if(!page.skip && tlen) {
			revision.text = strdup(text);
			md5(revision.text, revision.md5);
		}
		break;
	}
	
	current = elstack[--elidx];
	tlen = 0;
}

void XMLCALL xdata(void *data, const XML_Char *s, int len)
{
	if(tlen + len > tcapa) {
		tcapa = tlen + len > tcapa * 2 ? tlen + len : tcapa * 2;
		text = realloc(text, tcapa + 1);
		if(!text) nomem();
	}
	memcpy(text + tlen, s, len);
	tlen += len;
	text[tlen] = 0;
}

int main(int argc, char *argv[])
{
	char *tmp;
	XML_Parser p;
	size_t r;
	int perr = 0;
	
	parse_options(argc, argv);
	init_genrand(time(NULL) ^ getpid());
	
	/* set up */
	if(!outdir) outdir = ".";
#ifdef HAVE_LIBZ
	if(gzipcompress == gz_old) {
		if(!tmpdir) tmpdir = outdir;
		
		r = strlen(outdir);
		tmp = malloc(r + 32);
		if(!tmp) nomem();
		
		memcpy(tmp, tmpdir, r);
		sprintf(tmp + r, "/.xml2sql$XXXXXX", getpid());
		
		fdtmp = mkstemp(tmp);
		if(fdtmp == -1) fatal2(tmp, 0);
		unlinktmp(tmp);
		
		free(tmp);
	}
#endif
	if(chdir(outdir) != 0) {
		fprintf(stderr, "%s: Could not chdir to %s: %s\n",
			prog, outdir, strerror(errno));
		exit(1);
	}
	
	/* initialize expat */
	p = XML_ParserCreate("UTF-8");
	if(!p) fatal("expat internal failure");
	XML_UseParserAsHandlerArg(p);
	XML_SetElementHandler(p, xstart, xend);
	XML_SetCharacterDataHandler(p, xdata);
	
	/* revision hash */
	st = st_init_strtable();
	if(!st) nomem();
	
	/* text cache */
	text = malloc(1048577);
	if(!text) nomem();
	tcapa = 1048576;
	tlen = 0;
	
	/* read and parse XML */
	do {
		r = read(fdin, buf, BUFFERSIZE);
		if(r == -1) {
			if(errno == EINTR) continue;
			fprintf(stderr, "%s: %s\n", prog, strerror(errno));
			perr = 1;
			break;
		}
		if(XML_Parse(p, buf, r, r == 0) == XML_STATUS_ERROR) {
			fprintf(stderr, "%s: %s at line %d pos %d.\n",
				prog, XML_ErrorString(XML_GetErrorCode(p)),
				XML_GetCurrentLineNumber(p),
				XML_GetCurrentColumnNumber(p));
			perr = 1;
			break;
		}
	} while(r != 0);
	
	/* close tables */
	fintable(&page_tbl);
	fintable(&rev_tbl);
	if(!notext) fintable(&text_tbl);
	
	/* cleanup */
	XML_ParserFree(p);
	if(fdin > 0) close(fdin);
	if(fdtmp > 0) close(fdtmp);
	
	if(verbose && !perr) {
		printf("\rconverted %lu pages, %lu revisions, %lu texts.   \n",
			page_tbl.l, rev_tbl.l, text_tbl.l);
	}
	
	return 0;
}
