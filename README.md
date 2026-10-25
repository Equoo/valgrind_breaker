# valgrind_breaker

Malloc failure injection on top of the system valgrind: no valgrind rebuild.

A small `LD_PRELOAD` library counts allocations and makes the Nth one fail
(`NULL` + `errno = ENOMEM`, or `std::bad_alloc` for `operator new`), while
memcheck still tracks every block. The `valgrind_breaker` script drives it and
can fail each allocation in turn to find leaks, errors and crashes on the
failure paths.

## Requirements

- valgrind (any recent version) and its `valgrind/valgrind.h` header
  (usually shipped with the valgrind package itself)
- gcc/g++, make, glibc

## Usage

```sh
make
./valgrind_breaker --malloc-fail-auto ./prog args...
./valgrind_breaker --malloc-fail-auto ./minishell < input.txt
./valgrind_breaker --malloc-fail-at=12 --leak-check=full ./prog
./valgrind_breaker --malloc-fail-count=yes ./prog
```

Valgrind options are passed through unchanged.

| Option | Meaning |
|---|---|
| `--malloc-fail-at=<n>` | fail the Nth allocation (1-based) |
| `--malloc-fail-all=yes\|no` | also fail every allocation after the Nth |
| `--malloc-fail-count=yes\|no` | print the allocation counters at exit |
| `--malloc-fail-only-main=yes\|no` | only count allocations requested by the main executable |
| `--malloc-fail-ignore=a,b` | don't count allocations requested by objects whose path contains `a` or `b` (e.g. `readline`) |
| `--malloc-fail-auto` | fail each allocation in turn and report problems |
| `--malloc-fail-start=<n>` / `--malloc-fail-max=<n>` | range for auto mode |
| `--malloc-fail-jobs=<n>` | parallel runs in auto mode (default: `nproc`) |

`VALGRIND=/path/to/valgrind` selects another valgrind binary.

## How it works

- `libmalloc_breaker.so` wraps `malloc`, `calloc`, `realloc(NULL, n)`,
  `reallocarray`, `memalign`, `aligned_alloc`, `posix_memalign` and `valloc`,
  and forwards to libc's versions, which memcheck replaces as usual.
- `libmalloc_breaker_cxx.so` wraps `operator new`/`new[]` (preloaded only for
  programs linked against libstdc++).
- valgrind keeps the user's `LD_PRELOAD` and puts its own preloads in front.
  The script passes `--soname-synonyms=somalloc=nouserintercepts`, otherwise
  memcheck would replace the shim's own `malloc` and it would do nothing.
- The shim is only armed under valgrind (`RUNNING_ON_VALGRIND`), so the
  valgrind launcher is unaffected. `MALLOC_FAIL_NOVG=1` arms it natively.
- The requester of an allocation is the first stack frame outside libc,
  libstdc++, libgcc_s, libm and ld.so: `strdup()` called from `main` counts
  as `main`'s.

## Limitations

- Dynamically linked programs only (no static binaries).
- The allocation count is written at normal exit; a program leaving through
  `_exit()` during the counting run can't be auto-tested.
- Auto mode reads stdin when it is not a terminal: redirect it
  (`< input` or `< /dev/null`).

The original patched-memcheck implementation is in the git history.
