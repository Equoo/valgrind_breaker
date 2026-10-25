// operator new wrappers, preloaded next to libmalloc_breaker.so for C++ clients.
#include <dlfcn.h>
#include <new>

#include "malloc_breaker.h"

typedef void	*(*new_fn)(std::size_t);
typedef void	*(*new_nothrow_fn)(std::size_t, const std::nothrow_t &);

static void *forward(const char *name, std::size_t n)
{
	static new_fn	fns[2];
	new_fn			*fn = &fns[name[3] == 'a'];

	if (!*fn)
		*fn = (new_fn)dlsym(RTLD_NEXT, name);
	// Native libstdc++ new calls malloc: don't count that one twice.
	malloc_breaker_set_busy(1);
	void *p = (*fn)(n);
	malloc_breaker_set_busy(0);
	return p;
}

static void *forward_nothrow(const char *name, std::size_t n)
{
	static new_nothrow_fn	fns[2];
	new_nothrow_fn			*fn = &fns[name[3] == 'a'];

	if (!*fn)
		*fn = (new_nothrow_fn)dlsym(RTLD_NEXT, name);
	malloc_breaker_set_busy(1);
	void *p = (*fn)(n, std::nothrow);
	malloc_breaker_set_busy(0);
	return p;
}

void *operator new(std::size_t n)
{
	if (malloc_breaker_should_break())
		throw std::bad_alloc();
	return forward("_Znwm", n);
}

void *operator new[](std::size_t n)
{
	if (malloc_breaker_should_break())
		throw std::bad_alloc();
	return forward("_Znam", n);
}

void *operator new(std::size_t n, const std::nothrow_t &) noexcept
{
	if (malloc_breaker_should_break())
		return nullptr;
	return forward_nothrow("_ZnwmRKSt9nothrow_t", n);
}

void *operator new[](std::size_t n, const std::nothrow_t &) noexcept
{
	if (malloc_breaker_should_break())
		return nullptr;
	return forward_nothrow("_ZnamRKSt9nothrow_t", n);
}
