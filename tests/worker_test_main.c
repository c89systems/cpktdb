/* Test-only entrypoint: exercise database calls on a default pthread stack. */
#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

extern int __real_main(int argc, char **argv);

struct test_call {
  int argc;
  char **argv;
  int status;
};

static void *run_test(void *argument) {
  struct test_call *call = (struct test_call *)argument;
  pthread_attr_t attributes;
  size_t size = 0, guard = 0;
  int status;
  status = pthread_getattr_np(pthread_self(), &attributes);
  if (status != 0)
    abort();
  status = pthread_attr_getstacksize(&attributes, &size);
  status |= pthread_attr_getguardsize(&attributes, &guard);
  status |= pthread_attr_destroy(&attributes);
#ifdef __GLIBC__
  if (status != 0 || size != 128UL * 1024 || guard == 0) {
#else
  /* musl includes a small amount of TLS/alignment overhead in this query. */
  if (status != 0 || size < 128UL * 1024 ||
      size > 128UL * 1024 + 2 * (size_t)sysconf(_SC_PAGESIZE) || guard == 0) {
#endif
    fprintf(stderr, "Unexpected database test worker: stack=%lu guard=%lu\n",
            (unsigned long)size, (unsigned long)guard);
    abort();
  }
  call->status = __real_main(call->argc, call->argv);
  return NULL;
}

int __wrap_main(int argc, char **argv) {
  struct test_call call;
  pthread_t worker;
  call.argc = argc;
  call.argv = argv;
  call.status = 1;
  if (pthread_create(&worker, NULL, run_test, &call) != 0 ||
      pthread_join(worker, NULL) != 0) {
    fputs("Cannot run database test on a default worker stack\n", stderr);
    /* Setup failure must not satisfy a test expecting a normal nonzero exit. */
    abort();
  }
  return call.status;
}
