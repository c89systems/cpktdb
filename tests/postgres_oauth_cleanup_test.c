#include <cpkt/postgres.h>
#include <libpq-fe.h>

#include <stddef.h>
#include <string.h>

static int cleanup_count;
static int fail_next_allocation;
static int hook_status;

void *__real_calloc(size_t count, size_t size);

void *__wrap_calloc(size_t count, size_t size) {
  if (fail_next_allocation) {
    fail_next_allocation = 0;
    return NULL;
  }
  return __real_calloc(count, size);
}

static void cleanup(cpkt_postgres_connection *connection,
                    cpkt_postgres_oauth_bearer_request *request) {
  (void)connection;
  if (request->user == &cleanup_count)
    ++cleanup_count;
}

static int auth_hook(cpkt_postgres_auth_data_kind kind,
                     cpkt_postgres_connection *connection, void *data,
                     void *context) {
  cpkt_postgres_oauth_bearer_request *request;
  (void)connection;
  (void)context;
  if (kind != CPKT_POSTGRES_AUTH_DATA_OAUTH_BEARER_TOKEN)
    return -1;
  request = (cpkt_postgres_oauth_bearer_request *)data;
  request->cleanup = cleanup;
  request->user = &cleanup_count;
  return hook_status;
}

int main(void) {
  PGoauthBearerRequest native_request;
  int status;
  cpkt_postgres_set_auth_data_hook(auth_hook, NULL);

  memset(&native_request, 0, sizeof(native_request));
  hook_status = -1;
  status =
      PQgetAuthDataHook()(PQAUTHDATA_OAUTH_BEARER_TOKEN, NULL, &native_request);
  if (status != -1 || cleanup_count != 1 || native_request.cleanup != NULL)
    return 1;

  memset(&native_request, 0, sizeof(native_request));
  hook_status = 1;
  fail_next_allocation = 1;
  status =
      PQgetAuthDataHook()(PQAUTHDATA_OAUTH_BEARER_TOKEN, NULL, &native_request);
  if (status != -1 || cleanup_count != 2 || fail_next_allocation != 0 ||
      native_request.cleanup != NULL)
    return 2;

  memset(&native_request, 0, sizeof(native_request));
  status =
      PQgetAuthDataHook()(PQAUTHDATA_OAUTH_BEARER_TOKEN, NULL, &native_request);
  if (status != 1 || cleanup_count != 2 || native_request.cleanup == NULL)
    return 3;
  native_request.cleanup(NULL, &native_request);
  if (cleanup_count != 3 || native_request.user != NULL)
    return 4;

  cpkt_postgres_set_auth_data_hook(NULL, NULL);
  return 0;
}
