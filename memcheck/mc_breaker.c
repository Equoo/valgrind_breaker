#include "libvex_basictypes.h"
#include "pub_tool_basics.h"
#include "pub_tool_aspacemgr.h"
#include "pub_tool_gdbserver.h"
#include "pub_tool_poolalloc.h"
#include "pub_tool_hashtable.h"     // For mc_include.h
#include "pub_tool_libcbase.h"
#include "pub_tool_libcassert.h"
#include "pub_tool_libcprint.h"
#include "pub_tool_machine.h"
#include "pub_tool_mallocfree.h"
#include "pub_tool_options.h"
#include "pub_tool_oset.h"
#include "pub_tool_rangemap.h"
#include "pub_tool_replacemalloc.h"
#include "pub_tool_tooliface.h"
#include "pub_tool_threadstate.h"
#include "pub_tool_xarray.h"
#include "pub_tool_xtree.h"
#include "pub_tool_xtmemory.h"
#include "../coregrind/pub_core_vki.h"           // VKI_EINVAL, VKI_ENOMEM

#include "mc_include.h"
#include "memcheck.h"   /* for client requests */

// @todo PJF this mechanism doesn't work for MUSL C
// not sure why
// source here https://elixir.bootlin.com/musl/latest/source/src/errno/__errno_location.c#L4

/* Tries to set ERRNO to ENOMEM/EINVAL if possible. */
#if defined(VGO_linux)
extern int *__errno_location (void) __attribute__((weak));
#define SET_ERRNO_ENOMEM if (__errno_location)        \
      (*__errno_location ()) = VKI_ENOMEM;
#define SET_ERRNO_EINVAL {}
#elif defined(VGO_freebsd)
extern int *__error (void) __attribute__((weak));
#define SET_ERRNO_ENOMEM if (__error)        \
      (*__error ()) = VKI_ENOMEM;
#define SET_ERRNO_EINVAL if (__error)        \
      (*__error ()) = VKI_EINVAL;
#elif defined(VGO_solaris)
extern int *___errno (void) __attribute__((weak));
#define SET_ERRNO_ENOMEM if (___errno)        \
      (*___errno ()) = VKI_ENOMEM;
#define SET_ERRNO_EINVAL if (___errno)        \
      (*___errno ()) = VKI_EINVAL;
#elif defined(VGO_darwin)
extern int * __error(void) __attribute__((weak));
#define SET_ERRNO_ENOMEM if (__error)        \
      (*__error ()) = VKI_ENOMEM;
#define SET_ERRNO_EINVAL if (__error)        \
      (*__error ()) = VKI_EINVAL;
#else
#define SET_ERRNO_ENOMEM {}
#define SET_ERRNO_EINVAL {}
#endif

#define Err_Break 12

UInt MC_(breaking_malloc) (void)
{
	Int			was_suppressed = 1;

	MC_(clo_malloc_fail_call_count)++;

	if (MC_(clo_malloc_fail_at) > 0 &&
		((MC_(clo_malloc_fail_all) && MC_(clo_malloc_fail_call_count) >= MC_(clo_malloc_fail_at))
		|| (!MC_(clo_malloc_fail_all) && MC_(clo_malloc_fail_call_count) == MC_(clo_malloc_fail_at)))
	) {
		VG_(maybe_record_error)( VG_(get_running_tid)(),
				Err_Break, 0, /*s*/NULL, &was_suppressed);
		if (was_suppressed)
			return (0);

		SET_ERRNO_ENOMEM;

		return 1;
	}
	return 0;
}
