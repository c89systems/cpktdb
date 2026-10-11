#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <iodbcext.h>
#include <iodbcinst.h>
#include <iodbcunix.h>
#include <isql.h>
#include <isqlext.h>
#include <isqltypes.h>
#include <odbcinst.h>
#include <sql.h>
#include <sqlext.h>
#include <sqltypes.h>
#include <sqlucode.h>

typedef char
    cpkt_iodbc_bigint_must_be_eight_bytes[sizeof(SQLBIGINT) == 8 ? 1 : -1];
typedef char
    cpkt_iodbc_ubigint_must_be_eight_bytes[sizeof(SQLUBIGINT) == 8 ? 1 : -1];

int main(void) {
  SQLHENV environment;
  SQLHDBC connection;
  SQLCHAR sqlstate[6];
  SQLCHAR message[512];
  SQLINTEGER native_error;
  SQLSMALLINT message_length;
  char trace_path[] = "cpkt-iodbc-trace-XXXXXX";
  struct stat trace_stat;
  int trace_fd;
  UWORD config_mode;
  SQLRETURN status;

  environment = SQL_NULL_HENV;
  connection = SQL_NULL_HDBC;
  config_mode = ODBC_BOTH_DSN;
  if (!SQLGetConfigMode(&config_mode))
    return 1;
  if (config_mode != ODBC_BOTH_DSN && config_mode != ODBC_USER_DSN &&
      config_mode != ODBC_SYSTEM_DSN)
    return 2;
  status = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &environment);
  if (!SQL_SUCCEEDED(status) || environment == SQL_NULL_HENV)
    return 3;
  status = SQLSetEnvAttr(environment, SQL_ATTR_ODBC_VERSION,
                         (SQLPOINTER)SQL_OV_ODBC3, 0);
  if (!SQL_SUCCEEDED(status))
    return 4;
  status = SQLAllocHandle(SQL_HANDLE_DBC, environment, &connection);
  if (!SQL_SUCCEEDED(status) || connection == SQL_NULL_HDBC)
    return 5;
  trace_fd = mkstemp(trace_path);
  if (trace_fd < 0)
    return 8;
  close(trace_fd);
  status = SQLSetConnectAttr(connection, SQL_ATTR_TRACEFILE,
                             (SQLPOINTER)trace_path, SQL_NTS);
  if (!SQL_SUCCEEDED(status))
    return 9;
  status = SQLSetConnectAttr(connection, SQL_ATTR_TRACE,
                             (SQLPOINTER)(unsigned long)SQL_OPT_TRACE_ON, 0);
  if (!SQL_SUCCEEDED(status))
    return 10;
  status = SQLSetConnectAttr(connection, SQL_ATTR_TRACE,
                             (SQLPOINTER)(unsigned long)123, 0);
  if (status != SQL_ERROR)
    return 11;
  memset(sqlstate, 0, sizeof(sqlstate));
  memset(message, 0, sizeof(message));
  native_error = -1;
  message_length = -1;
  status = SQLGetDiagRec(SQL_HANDLE_DBC, connection, 1, sqlstate, &native_error,
                         message, sizeof(message), &message_length);
  if (!SQL_SUCCEEDED(status) || strcmp((const char *)sqlstate, "HY024") != 0 ||
      native_error != 0 || message_length <= 0)
    return 12;
  native_error = -1;
  status = SQLGetDiagRecA(SQL_HANDLE_DBC, connection, 1, NULL, &native_error,
                          NULL, 0, NULL);
  if (!SQL_SUCCEEDED(status) || native_error != 0)
    return 16;
  native_error = -1;
  status = SQLGetDiagRecW(SQL_HANDLE_DBC, connection, 1, NULL, &native_error,
                          NULL, 0, NULL);
  if (!SQL_SUCCEEDED(status) || native_error != 0)
    return 17;
  status = SQLSetConnectAttr(connection, SQL_ATTR_TRACE,
                             (SQLPOINTER)(unsigned long)SQL_OPT_TRACE_OFF, 0);
  if (!SQL_SUCCEEDED(status))
    return 13;
  if (stat(trace_path, &trace_stat) != 0 || trace_stat.st_size <= 0)
    return 14;
  if (unlink(trace_path) != 0)
    return 15;
  if (!SQL_SUCCEEDED(SQLFreeHandle(SQL_HANDLE_DBC, connection)))
    return 6;
  if (!SQL_SUCCEEDED(SQLFreeHandle(SQL_HANDLE_ENV, environment)))
    return 7;
  return 0;
}
