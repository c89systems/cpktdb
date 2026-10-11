#include <cpkt/postgres.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct notice_state {
  cpkt_postgres_connection *connection;
  int calls;
  int errors;
} notice_state;

#if defined(CPKT_POSTGRES_NOTICE_FAULT_INJECTION)
static int fail_calloc_after = -1;
void *__real_calloc(size_t count, size_t size);
void *__wrap_calloc(size_t count, size_t size) {
  if (fail_calloc_after == 0) {
    fail_calloc_after = -1;
    return NULL;
  }
  if (fail_calloc_after > 0)
    --fail_calloc_after;
  return __real_calloc(count, size);
}

static void fail_allocation(int after) { fail_calloc_after = after; }
static int allocation_failed(void) { return fail_calloc_after == -1; }
#else
static void fail_allocation(int after) { (void)after; }
static int allocation_failed(void) { return 0; }
#endif

static void receiver(void *context, cpkt_postgres_connection *connection,
                     const cpkt_postgres_result *result) {
  notice_state *state;
  state = (notice_state *)context;
  ++state->calls;
  if (connection != state->connection || result == NULL)
    ++state->errors;
}

static void replacement_receiver(void *context,
                                 cpkt_postgres_connection *connection,
                                 const cpkt_postgres_result *result) {
  receiver(context, connection, result);
}

static void processor(void *context, cpkt_postgres_connection *connection,
                      const char *message) {
  notice_state *state;
  state = (notice_state *)context;
  ++state->calls;
  if (connection != state->connection || message == NULL || message[0] == '\0')
    ++state->errors;
}

static void replacement_processor(void *context,
                                  cpkt_postgres_connection *connection,
                                  const char *message) {
  processor(context, connection, message);
}

/* Modes: 0 disables, 1 installs the first callback, 2 the replacement. */
static int set_callback(cpkt_postgres_connection *connection, int use_processor,
                        int mode, void *context, int old_mode,
                        void *old_context_expected) {
  cpkt_postgres_notice_receiver old_receiver;
  cpkt_postgres_notice_processor old_processor;
  cpkt_postgres_notice_receiver expected_receiver;
  cpkt_postgres_notice_processor expected_processor;
  void *old_context;
  int status;
  old_context = &old_context;
  old_receiver = receiver;
  old_processor = processor;
  expected_receiver =
      old_mode == 0 ? NULL : (old_mode == 1 ? receiver : replacement_receiver);
  expected_processor =
      old_mode == 0 ? NULL
                    : (old_mode == 1 ? processor : replacement_processor);
  if (use_processor) {
    status = cpkt_postgres_set_notice_processor(
        connection,
        mode == 0 ? NULL : (mode == 1 ? processor : replacement_processor),
        context, &old_processor, &old_context);
    if (old_processor != expected_processor)
      return -1;
  } else {
    status = cpkt_postgres_set_notice_receiver(
        connection,
        mode == 0 ? NULL : (mode == 1 ? receiver : replacement_receiver),
        context, &old_receiver, &old_context);
    if (old_receiver != expected_receiver)
      return -1;
  }
  return old_context == old_context_expected ? status : -1;
}

static int trigger(cpkt_postgres_result *result) {
  return cpkt_postgres_result_field_name(result, -1) == NULL;
}

static int fresh_notice(cpkt_postgres_connection *connection) {
  cpkt_postgres_result *result;
  int status;
  result = cpkt_postgres_result_new_empty(connection,
                                          CPKT_POSTGRES_RESULT_TUPLES_OK);
  if (result == NULL)
    return 0;
  status = trigger(result);
  cpkt_postgres_result_free(result);
  return status;
}

static int run_case(int use_processor, int fail_allocations) {
  cpkt_postgres_connection *connection;
  cpkt_postgres_result *original;
  cpkt_postgres_result *copy;
  cpkt_postgres_result *replacement;
  notice_state first;
  notice_state second;
  int status;
  int before;
  memset(&first, 0, sizeof(first));
  memset(&second, 0, sizeof(second));
  connection = NULL;
  original = NULL;
  copy = NULL;
  replacement = NULL;
  status = 1;
  if (set_callback(NULL, use_processor, 1, &first, 0, NULL) != 0)
    goto cleanup;
  connection = cpkt_postgres_connect(
      "host=/tmp/cpkt-notice-registration-no-socket connect_timeout=1");
  if (connection == NULL)
    goto cleanup;
  first.connection = connection;
  second.connection = connection;
  if (fail_allocations) {
    /* A fresh connection needs both the owner and immutable snapshot. */
    fail_allocation(0);
    if (set_callback(connection, use_processor, 1, &first, 0, NULL) != 0 ||
        !allocation_failed())
      goto cleanup;
    fail_allocation(1);
    if (set_callback(connection, use_processor, 1, &first, 0, NULL) != 0 ||
        !allocation_failed())
      goto cleanup;
  }
  if (set_callback(connection, use_processor, 1, &first, 0, NULL) != 1)
    goto cleanup;
  original = cpkt_postgres_result_new_empty(connection,
                                            CPKT_POSTGRES_RESULT_TUPLES_OK);
  if (original == NULL)
    goto cleanup;
  copy = cpkt_postgres_result_copy(original,
                                   CPKT_POSTGRES_COPY_RESULT_NOTICE_HOOKS);
  if (copy == NULL)
    goto cleanup;
  if (fail_allocations) {
    fail_allocation(0);
    if (set_callback(connection, use_processor, 2, &second, 1, &first) != 0 ||
        !allocation_failed() || !fresh_notice(connection) || first.calls != 1 ||
        second.calls != 0)
      goto cleanup;
  }
  if (set_callback(connection, use_processor, 2, &second, 1, &first) != 1)
    goto cleanup;
  replacement = cpkt_postgres_result_new_empty(connection,
                                               CPKT_POSTGRES_RESULT_TUPLES_OK);
  if (replacement == NULL || !trigger(replacement) || second.calls != 1)
    goto cleanup;
  if (fail_allocations) {
    fail_allocation(0);
    if (set_callback(connection, use_processor, 0, NULL, 2, &second) != 0 ||
        !allocation_failed() || !fresh_notice(connection) || second.calls != 2)
      goto cleanup;
  }
  if (set_callback(connection, use_processor, 0, NULL, 2, &second) != 1)
    goto cleanup;
  before = second.calls;
  if (!fresh_notice(connection) || second.calls != before)
    goto cleanup;
  if (!trigger(original) || !trigger(copy) ||
      first.calls != (fail_allocations ? 3 : 2) || second.calls != before)
    goto cleanup;
  /* Optional output slots are not required for successful registration. */
  if (use_processor) {
    if (cpkt_postgres_set_notice_processor(connection, processor, &second, NULL,
                                           NULL) != 1)
      goto cleanup;
  } else if (cpkt_postgres_set_notice_receiver(connection, receiver, &second,
                                               NULL, NULL) != 1) {
    goto cleanup;
  }
  if (!fresh_notice(connection) || second.calls != before + 1)
    goto cleanup;
  cpkt_postgres_connection_free(connection);
  connection = NULL;
  first.connection = NULL;
  second.connection = NULL;
  if (!trigger(original) || !trigger(copy) || !trigger(replacement) ||
      first.calls != (fail_allocations ? 5 : 4) || second.calls != before + 2 ||
      first.errors != 0 || second.errors != 0)
    goto cleanup;
  status = 0;
cleanup:
  /* Disarm faults before cleanup so a failed assertion stays diagnosable. */
  fail_allocation(-1);
  cpkt_postgres_result_free(original);
  cpkt_postgres_result_free(copy);
  cpkt_postgres_result_free(replacement);
  cpkt_postgres_connection_free(connection);
  if (status != 0)
    fprintf(stderr, "notice registration processor=%d fault=%d failed\n",
            use_processor, fail_allocations);
  return status;
}

/* Portable static/shared cases also run in the native Darwin CI consumers. */
int cpkt_postgres_notice_registration_test(int fail_allocations) {
  int status;
  status = run_case(0, fail_allocations);
  if (run_case(1, fail_allocations) != 0)
    status = 1;
  return status;
}
