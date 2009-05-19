/* O_BINARY for Windows */
#ifndef O_BINARY
# define O_BINARY 0
#endif

/* memset portable rewrite */
#ifndef HAVE_MEMSET
void xqmemset(void *ptr, int ch, size_t size)
{
	unsigned char *pch = (unsigned char *)ptr;
	while(size--) *pch++ = (unsigned char)ch;
}
# define memset xqmemset
#endif

/* strcasecmp portable rewrite */
#if !defined(HAVE_STRCASECMP)
# if defined(HAVE_STRICMP)
#  define strcasecmp stricmp
# else
int xqstrcasecmp(const char *lhs, const char *rhs)
{
	int n;
	for(; *lhs && *rhs; lhs++, rhs++) {
		n = toupper(*rhs) - toupper(*lhs);
		if(n != 0) return n;
	}
	if(*rhs) return 1;
	if(*lhs) return -1;
	return 0;
}
#  define strcasecmp xqstrcasecmp
# endif
#endif

/* strncasecmp portable rewrite */
#if !defined(HAVE_STRNCASECMP)
# if defined(HAVE_STRNICMP)
#  define strncasecmp strnicmp
# else
int xqstrncasecmp(const char *lhs, const char *rhs, size_t len)
{
	int n;
	for(; *lhs && *rhs && len; lhs++, rhs++, len--) {
		n = toupper(*rhs) - toupper(*lhs);
		if(n != 0) return n;
	}
	if(len == 0) return 0;
	if(*rhs) return 1;
	if(*lhs) return -1;
	return 0;
}
#  define strncasecmp xqstrncasecmp
# endif
#endif

/* mkstemp non-portable rewrite */
#if !defined(HAVE_MKSTEMP)
int mkstemp(char *t)
{
	static const char ranch[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
	long genrand_int31(); /* random.c */
	size_t len;
	int i, ch, fd;
#ifdef _WIN32
	HANDLE fh;
#endif
	
	len = strlen(t) - 6;
	while(1) {
		for(i = 0; i < 6; ++i) {
			do ch = genrand_int31() % 64; while(ch >= 62);
			t[len + i] = ranch[ch];
		}
	#ifdef _WIN32
		fh = CreateFile(t, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_NEW,
						FILE_ATTRIBUTE_TEMPORARY |
						FILE_FLAG_DELETE_ON_CLOSE, NULL);
		if(fh != INVALID_HANDLE_VALUE)
			return _open_osfhandle(fh, O_RDWR | O_BINARY);
		if(GetLastError() != ERROR_FILE_EXISTS) {
			errno = EACCES;
			return -1;
		}
	#else
		fd = open(t, O_RDWR|O_CREAT|O_EXCL|O_BINARY, S_IRWXU|S_IRUSR);
		if(fd != -1) return fd;
		if(errno != EEXIST) return -1;
	#endif
	}
}
#endif /* HAVE_MKSTEMP */

/* unlink for temporary */
#ifdef _WIN32
# define unlinktmp(f) ((void)0)
#else
# define unlinktmp unlink
#endif
