#include <curl/curl.h>
#include <libpq-fe.h>

#include <dlfcn.h>
#include <stdio.h>

int main(int argc, char **argv) {
  const char *error;
  void *module;
  if (argc != 2)
    return 1;
  if (curl_version() == NULL)
    return 5;
  if (PQlibVersion() <= 0)
    return 6;
  module = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  if (module == NULL) {
    error = dlerror();
    fprintf(stderr, "cannot load PostgreSQL OAuth module: %s\n",
            error == NULL ? "unknown error" : error);
    return 2;
  }
  if (dlsym(module, "libpq_oauth_init") == NULL ||
      dlsym(module, "pg_fe_run_oauth_flow") == NULL ||
      dlsym(module, "pg_fe_cleanup_oauth_flow") == NULL) {
    fprintf(stderr,
            "PostgreSQL OAuth module is missing its flow entry points\n");
    dlclose(module);
    return 3;
  }
  return dlclose(module) == 0 ? 0 : 4;
}
