CC		?= cc
CXX		?= c++
CFLAGS	?= -O2 -g -Wall -Wextra
LIBS	= libmalloc_breaker.so libmalloc_breaker_cxx.so

all: $(LIBS)

libmalloc_breaker.so: malloc_breaker.c malloc_breaker.h
	$(CC) $(CFLAGS) -shared -fPIC -o $@ $< -ldl

libmalloc_breaker_cxx.so: malloc_breaker_cxx.cc malloc_breaker.h libmalloc_breaker.so
	$(CXX) $(CFLAGS) -shared -fPIC -o $@ $< -L. -lmalloc_breaker -Wl,-rpath,'$$ORIGIN' -ldl

clean:
	rm -f $(LIBS)

.PHONY: all clean
