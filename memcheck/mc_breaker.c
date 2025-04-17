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

#define KRST  "\033[0m"
#define KNRM  "\x1B[0m"
#define KRED  "\x1B[31m"
#define KGRN  "\x1B[32m"
#define KYEL  "\x1B[33m"
#define KBLU  "\x1B[34m"
#define KMAG  "\x1B[35m"
#define KCYN  "\x1B[36m"
#define KWHT  "\x1B[37m"

UInt MC_(breaking_malloc) (void)
{
	static Int malloc_call_count = 0;

	malloc_call_count++;

	if (MC_(clo_malloc_fail_at) > 0 &&
		((MC_(clo_malloc_fail_all) && malloc_call_count >= MC_(clo_malloc_fail_at))
		|| (!MC_(clo_malloc_fail_all) && malloc_call_count == MC_(clo_malloc_fail_at)))
	) {
		VG_(umsg)("%sFailing malloc call%s #%d\n", KBLU, KNRM, malloc_call_count);
		SET_ERRNO_ENOMEM;

		VG_(printf)(KMAG);

		ExeContext* ec = VG_(record_ExeContext)(VG_(get_running_tid)(), 0);
		VG_(pp_ExeContext)(ec);
		
		VG_(printf)(KNRM);

		return 1;
	}
	return 0;
}