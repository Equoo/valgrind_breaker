#define _GNU_SOURCE
#include "malloc_breaker.h"

#include <dlfcn.h>
#include <errno.h>
#include <execinfo.h>
#include <link.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <valgrind/valgrind.h>

#define MAX_FRAMES 32

enum { UNINIT, RESOLVING, INACTIVE, ACTIVE };

static int state = UNINIT;
static long fail_at;
static int fail_all;
static int print_count;
static int only_main;
static const char *ignore;
static const char *count_file;
static pid_t owner_pid;
static long breakable;
static long suppressed;
static __thread int busy;

static void *(*real_malloc)(size_t);
static void *(*real_calloc)(size_t, size_t);
static void *(*real_realloc)(void *, size_t);
static void *(*real_reallocarray)(void *, size_t, size_t);
static void (*real_free)(void *);
static void *(*real_memalign)(size_t, size_t);
static void *(*real_aligned_alloc)(size_t, size_t);
static int (*real_posix_memalign)(void **, size_t, size_t);
static void *(*real_valloc)(size_t);

/* dlsym() may allocate while we resolve the real allocators. */
static char boot_buf[4096] __attribute__((aligned(16)));
static size_t boot_used;

static void *boot_alloc(size_t n) {
	void *p;

	n = (n + 15) & ~(size_t)15;
	if (n > sizeof(boot_buf) - boot_used) return (NULL);
	p = boot_buf + boot_used;
	boot_used += n;
	return (p);
}

static int is_boot(const void *p) {
	return ((const char *)p >= boot_buf &&
			(const char *)p < boot_buf + sizeof(boot_buf));
}

static void resolve(void) {
	const char *s;

	state = RESOLVING;
	real_malloc = dlsym(RTLD_NEXT, "malloc");
	real_calloc = dlsym(RTLD_NEXT, "calloc");
	real_realloc = dlsym(RTLD_NEXT, "realloc");
	real_reallocarray = dlsym(RTLD_NEXT, "reallocarray");
	real_free = dlsym(RTLD_NEXT, "free");
	real_memalign = dlsym(RTLD_NEXT, "memalign");
	real_aligned_alloc = dlsym(RTLD_NEXT, "aligned_alloc");
	real_posix_memalign = dlsym(RTLD_NEXT, "posix_memalign");
	real_valloc = dlsym(RTLD_NEXT, "valloc");

	if ((s = getenv("MALLOC_FAIL_AT"))) fail_at = atol(s);
	fail_all = (s = getenv("MALLOC_FAIL_ALL")) && atoi(s);
	print_count = (s = getenv("MALLOC_FAIL_COUNT")) && atoi(s);
	only_main = (s = getenv("MALLOC_FAIL_ONLY_MAIN")) && atoi(s);
	ignore = getenv("MALLOC_FAIL_IGNORE");
	if (ignore && !*ignore) ignore = NULL;
	count_file = getenv("MALLOC_FAIL_COUNT_FILE");
	owner_pid = getpid();

	/* Unarmed outside valgrind, so the valgrind launcher is unaffected. */
	if (RUNNING_ON_VALGRIND || ((s = getenv("MALLOC_FAIL_NOVG")) && atoi(s)))
		state = ACTIVE;
	else
		state = INACTIVE;
}

#define ENSURE_RESOLVED()               \
	do {                                \
		if (state == UNINIT) resolve(); \
	} while (0)

static int is_runtime_lib(const char *path) {
	static const char *const libs[] = {
		"/libc.so",	 "/libstdc++.so",	   "/libgcc_s.so", "/libm.so",
		"/ld-linux", "/libmalloc_breaker", "/vgpreload_",  NULL};
	int i;

	for (i = 0; libs[i]; i++)
		if (strstr(path, libs[i])) return (1);
	return (0);
}

static int matches_ignore(const char *path) {
	const char *tok;
	const char *end;
	size_t len;

	for (tok = ignore; *tok; tok = *end ? end + 1 : end) {
		end = strchrnul(tok, ',');
		len = end - tok;
		if (len && memmem(path, strlen(path), tok, len)) return (1);
	}
	return (0);
}

/* Returns 1 when the allocation must not be counted. */
static int filtered(void) {
	void *frames[MAX_FRAMES];
	struct link_map *lm;
	Dl_info info;
	int n;
	int i;

	n = backtrace(frames, MAX_FRAMES);
	for (i = 0; i < n; i++) {
		if (!dladdr1(frames[i], &info, (void **)&lm, RTLD_DL_LINKMAP) || !lm)
			continue;
		if (!lm->l_name[0]) return (0); /* main executable */
		if (is_runtime_lib(lm->l_name)) continue;
		return (only_main || (ignore && matches_ignore(lm->l_name)));
	}
	return (0);
}

static void report(long n) {
	void *frames[MAX_FRAMES];

	if (RUNNING_ON_VALGRIND) {
		VALGRIND_PRINTF_BACKTRACE("Failing malloc call #%ld\n", n);
		return;
	}
	fprintf(stderr, "Failing malloc call #%ld\n", n);
	backtrace_symbols_fd(frames, backtrace(frames, MAX_FRAMES), 2);
}

int malloc_breaker_should_break(void) {
	long n;
	int skip;

	ENSURE_RESOLVED();
	if (state != ACTIVE || busy) return (0);
	busy = 1;
	skip = (only_main || ignore) && filtered();
	busy = 0;
	if (skip) {
		__atomic_add_fetch(&suppressed, 1, __ATOMIC_RELAXED);
		return (0);
	}
	n = __atomic_add_fetch(&breakable, 1, __ATOMIC_RELAXED);
	if (fail_at <= 0 || n < fail_at || (n > fail_at && !fail_all)) return (0);
	busy = 1;
	report(n);
	busy = 0;
	errno = ENOMEM;
	return (1);
}

void malloc_breaker_set_busy(int value) { busy = value; }

__attribute__((destructor)) static void finish(void) {
	FILE *f;

	if (state != ACTIVE || getpid() != owner_pid) return;
	busy = 1;
	if (print_count) {
		if (RUNNING_ON_VALGRIND)
			VALGRIND_PRINTF(
				"total breakable allocs: %ld breakables, "
				"%ld suppressed\n",
				breakable, suppressed);
		else
			fprintf(stderr,
					"total breakable allocs: %ld breakables, "
					"%ld suppressed\n",
					breakable, suppressed);
	}
	if (count_file && (f = fopen(count_file, "w"))) {
		fprintf(f, "%ld %ld\n", breakable, suppressed);
		fclose(f);
	}
	busy = 0;
}

void *malloc(size_t n) {
	ENSURE_RESOLVED();
	if (state == RESOLVING) return (boot_alloc(n));
	if (malloc_breaker_should_break()) return (NULL);
	return (real_malloc(n));
}

void *calloc(size_t nmemb, size_t size) {
	ENSURE_RESOLVED();
	if (state == RESOLVING)
		return (size && nmemb > (size_t)-1 / size ? NULL : boot_alloc(nmemb * size));
	if (malloc_breaker_should_break()) return (NULL);
	return (real_calloc(nmemb, size));
}

/* Like memcheck, only realloc(NULL, n) counts as a new allocation. */
void *realloc(void *p, size_t n) {
	void *q;
	size_t avail;

	ENSURE_RESOLVED();
	if (state == RESOLVING) return (p ? NULL : boot_alloc(n));
	if (p && is_boot(p)) {
		avail = boot_buf + sizeof(boot_buf) - (char *)p;
		if ((q = real_malloc(n))) memcpy(q, p, n < avail ? n : avail);
		return (q);
	}
	if (!p && malloc_breaker_should_break()) return (NULL);
	return (real_realloc(p, n));
}

void *reallocarray(void *p, size_t nmemb, size_t size) {
	ENSURE_RESOLVED();
	if (!p && malloc_breaker_should_break()) return (NULL);
	return (real_reallocarray(p, nmemb, size));
}

void free(void *p) {
	ENSURE_RESOLVED();
	if (!p || is_boot(p)) return;
	real_free(p);
}

void *memalign(size_t align, size_t n) {
	ENSURE_RESOLVED();
	if (malloc_breaker_should_break()) return (NULL);
	return (real_memalign(align, n));
}

void *aligned_alloc(size_t align, size_t n) {
	ENSURE_RESOLVED();
	if (malloc_breaker_should_break()) return (NULL);
	return (real_aligned_alloc(align, n));
}

int posix_memalign(void **res, size_t align, size_t n) {
	ENSURE_RESOLVED();
	if (malloc_breaker_should_break()) return (ENOMEM);
	return (real_posix_memalign(res, align, n));
}

void *valloc(size_t n) {
	ENSURE_RESOLVED();
	if (malloc_breaker_should_break()) return (NULL);
	return (real_valloc(n));
}
