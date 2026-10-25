#ifndef MALLOC_BREAKER_H
# define MALLOC_BREAKER_H

# ifdef __cplusplus
extern "C" {
# endif

/* Counts one allocation; returns 1 (with errno = ENOMEM) if it must fail. */
int		malloc_breaker_should_break(void);
/* While set, allocations of the calling thread are neither counted nor failed. */
void	malloc_breaker_set_busy(int value);

# ifdef __cplusplus
}
# endif

#endif
