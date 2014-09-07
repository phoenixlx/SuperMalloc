#ifndef ATOMICALLY_H
#define ATOMICALLY_H

#include <sched.h>
#include <stdbool.h>
#include <immintrin.h>

#include "rng.h"


static inline bool mylock_wait(volatile unsigned int *mylock) {
  bool too_long = false;
  while (*mylock) {
    if (0==(prandnum()&(1024-1))) {
      sched_yield();
      too_long = true;
    } else {
        _mm_pause();
    }
  }
  return too_long;
}

static inline unsigned int xchg(volatile unsigned int *addr, unsigned int newval) {
  unsigned int result;
  __asm__ volatile("lock xchgl %0, %1" :
		   "+m"(*addr), "=a" (result) :
		   "1" (newval) :
		   "cc");
  return result;
}

static inline void mylock_acquire(volatile unsigned int *mylock) {
  do {
    mylock_wait(mylock);
  } while (xchg(mylock, 1));
}

static inline void mylock_release(volatile unsigned int *mylock) {
  *mylock = 0;
}

#define XABORT_LOCK_HELD 9

#ifdef COVERAGE
#define have_rtm 0
#else
#define have_rtm 1
#endif

template<typename ReturnType, typename... Arguments>
static inline ReturnType atomically(volatile unsigned int *mylock,
			            void (*predo)(Arguments... args),
				    ReturnType (*fun)(Arguments... args),
				    Arguments... args) {

  // Be a little optimistic: try to run the function without the predo if we the lock looks good
  if (*mylock == 0) {
    unsigned int xr = _xbegin();
    if (xr == _XBEGIN_STARTED) {
      ReturnType r = fun(args...);
      if (*mylock) _xabort(XABORT_LOCK_HELD);
      _xend();
      return r;
    }
  }

  int count = 0;
  while (have_rtm && count < 20) {
    mylock_wait(mylock);
    predo(args...);
    while (mylock_wait(mylock)) {
      // If the lock was held for a long time, then do the predo code again.
      predo(args...);
    }
    unsigned int xr = _xbegin();
    if (xr == _XBEGIN_STARTED) {
      ReturnType r = fun(args...);
      if (*mylock) _xabort(XABORT_LOCK_HELD);
      _xend();
      return r;
    } else if ((xr & _XABORT_EXPLICIT) && (_XABORT_CODE(xr) == XABORT_LOCK_HELD)) {
      count = 0; // reset the counter if we had an explicit lock contention abort.
      continue;
    } else {
      count++;
      for (int i = 1; i < count; i++) {
	if (0 == (prandnum()&1023)) {
	  sched_yield();
	} else {
	  __asm__ volatile("pause");
	}
      }
    }
  }
  // We finally give up and acquire the lock.
  predo(args...);
  mylock_acquire(mylock);
  ReturnType r = fun(args...);
  mylock_release(mylock);
  return r;
}

struct lock {
  unsigned int l __attribute((aligned(64)));
};

#define atomic_load(addr) __atomic_load_n(addr, __ATOMIC_CONSUME)

#define prefetch_read(addr) __builtin_prefetch(addr, 0, 3)
#define prefetch_write(addr) __builtin_prefetch(addr, 1, 3)


#endif // ATOMICALLY_H