#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cpkt/postgres.h>

struct log_capture {
  int count;
  int level;
  char text[512];
};

static void on_diagnostic(void *context, cpkt_postgres_connection *connection,
                          int level, const char *message) {
  struct log_capture *capture;

  capture = (struct log_capture *)context;
  if (connection == NULL || message == NULL)
    return;
  capture->count++;
  capture->level = level;
  snprintf(capture->text, sizeof(capture->text), "%s", message);
}

int main(void) {
  char passfile[] = "cpkt-pgpass-XXXXXX";
  char conninfo[512];
  char password[3];
  struct log_capture process_log;
  struct log_capture connection_log;
  cpkt_postgres_connection *connection;
  FILE *stderr_capture;
  int pass_fd;
  int saved_stderr;
  int status;

  memset(&process_log, 0, sizeof(process_log));
  memset(&connection_log, 0, sizeof(connection_log));
  pass_fd = mkstemp(passfile);
  if (pass_fd < 0)
    return 1;
  close(pass_fd);
  if (chmod(passfile, 0644) != 0)
    return 2;
  status = snprintf(conninfo, sizeof(conninfo),
                    "host=127.0.0.1 port=1 user=cpkt dbname=cpkt "
                    "passfile=%s sslpassword=abcdef connect_timeout=1",
                    passfile);
  if (status < 0 || (size_t)status >= sizeof(conninfo))
    return 3;

  stderr_capture = tmpfile();
  if (stderr_capture == NULL)
    return 4;
  saved_stderr = dup(fileno(stderr));
  if (saved_stderr < 0)
    return 5;
  fflush(stderr);
  if (dup2(fileno(stderr_capture), fileno(stderr)) < 0)
    return 6;

  cpkt_postgres_set_default_diagnostic_sink(on_diagnostic, &process_log);
  connection = cpkt_postgres_connect_start(conninfo);
  if (connection == NULL || process_log.count < 1 ||
      process_log.level != CPKT_POSTGRES_DIAGNOSTIC_WARNING ||
      strstr(process_log.text, "password file") == NULL)
    return 7;

  if (!cpkt_postgres_set_diagnostic_sink(connection, on_diagnostic,
                                         &connection_log))
    return 8;
  if (cpkt_postgres_default_ssl_key_password_hook(password, sizeof(password),
                                                  connection) != 2 ||
      connection_log.count != 1 ||
      connection_log.level != CPKT_POSTGRES_DIAGNOSTIC_WARNING ||
      strstr(connection_log.text, "sslpassword truncated") == NULL)
    return 9;
  if (!cpkt_postgres_set_diagnostic_sink(connection, NULL, NULL))
    return 10;
  if (cpkt_postgres_default_ssl_key_password_hook(password, sizeof(password),
                                                  connection) != 2 ||
      process_log.count < 2 || connection_log.count != 1)
    return 11;

  cpkt_postgres_connection_free(connection);
  cpkt_postgres_set_default_diagnostic_sink(NULL, NULL);
  fflush(stderr);
  if (dup2(saved_stderr, fileno(stderr)) < 0)
    return 12;
  close(saved_stderr);
  if (ftell(stderr_capture) != 0)
    return 13;
  fclose(stderr_capture);
  if (unlink(passfile) != 0)
    return 14;
  return 0;
}
