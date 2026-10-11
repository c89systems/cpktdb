#include <cpkt/sqlite.h>
#include <sqlite3.h>

#include <stdio.h>
#include <string.h>

typedef struct log_record {
  int calls;
  int code;
  size_t length;
  char text[1024];
} log_record;

static log_record *active_record;

static void capture(void *context, int code, const char *message) {
  log_record *record = (log_record *)context;
  size_t length = strlen(message);
  if (record != active_record || length >= sizeof(record->text)) {
    if (record != NULL)
      record->calls = -100;
    return;
  }
  ++record->calls;
  record->code = code;
  record->length = length;
  memcpy(record->text, message, length + 1);
}

static int equal_record(const log_record *left, const log_record *right) {
  return left->calls == 1 && right->calls == 1 && left->code == right->code &&
         left->length == right->length &&
         memcmp(left->text, right->text, left->length + 1) == 0;
}

static void reset_record(log_record *record) {
  memset(record, 0, sizeof(*record));
  active_record = record;
}

int main(void) {
  log_record native_record;
  log_record facade_record;
  char long_text[2049];
  char *consumed;
  int native_count;
  int facade_count;
  int stage = 1;
  sqlite3_int64 signed_value = -((sqlite3_int64)1 << 50) + 17;
  sqlite3_uint64 unsigned_value = ((sqlite3_uint64)1 << 53) + 41;
  cpkt_sqlite_i64 signed_bits;
  cpkt_sqlite_u64 unsigned_bits;
  signed_bits.high =
      (unsigned long)(((sqlite3_uint64)signed_value >> 32) & 0xffffffffu);
  signed_bits.low = (unsigned long)((sqlite3_uint64)signed_value & 0xffffffffu);
  unsigned_bits.high = (unsigned long)((unsigned_value >> 32) & 0xffffffffu);
  unsigned_bits.low = (unsigned long)(unsigned_value & 0xffffffffu);
  memset(long_text, 'x', sizeof(long_text) - 1);
  long_text[sizeof(long_text) - 1] = '\0';
  if (sqlite3_shutdown() != SQLITE_OK ||
      sqlite3_config(SQLITE_CONFIG_LOG, NULL, NULL) != SQLITE_OK)
    goto fail;
  native_count = 19;
  facade_count = 19;
  sqlite3_log(SQLITE_NOTICE, "disabled%n", &native_count);
  cpkt_sqlite_log(CPKT_SQLITE_NOTICE, "disabled%n", &facade_count);
  if (native_count != 19 || facade_count != native_count)
    goto fail;
  stage = 2;
  if (sqlite3_config(SQLITE_CONFIG_LOG, capture, &native_record) != SQLITE_OK)
    goto fail;
  reset_record(&native_record);
  sqlite3_log(SQLITE_WARNING, "quote %q / %Q / %s", "a'b", "x'y", "end");
  if (cpkt_sqlite_global_config_log(capture, &facade_record) != CPKT_SQLITE_OK)
    goto fail;
  reset_record(&facade_record);
  cpkt_sqlite_log(CPKT_SQLITE_WARNING, "quote %q / %Q / %s", "a'b", "x'y",
                  "end");
  if (!equal_record(&native_record, &facade_record))
    goto fail;
  stage = 21;
  if (sqlite3_config(SQLITE_CONFIG_LOG, capture, &native_record) != SQLITE_OK)
    goto fail;
  consumed = sqlite3_mprintf("native-owned %d", 17);
  if (consumed == NULL)
    goto fail;
  reset_record(&native_record);
  sqlite3_log(SQLITE_NOTICE, "consume %z", consumed);
  if (cpkt_sqlite_global_config_log(capture, &facade_record) != CPKT_SQLITE_OK)
    goto fail;
  consumed = sqlite3_mprintf("native-owned %d", 17);
  if (consumed == NULL)
    goto fail;
  reset_record(&facade_record);
  cpkt_sqlite_log(CPKT_SQLITE_NOTICE, "consume %z", consumed);
  if (!equal_record(&native_record, &facade_record))
    goto fail;
  stage = 3;
  if (sqlite3_config(SQLITE_CONFIG_LOG, capture, &native_record) != SQLITE_OK)
    goto fail;
  reset_record(&native_record);
  sqlite3_log(SQLITE_NOTICE, "%s", long_text);
  if (cpkt_sqlite_global_config_log(capture, &facade_record) != CPKT_SQLITE_OK)
    goto fail;
  reset_record(&facade_record);
  cpkt_sqlite_log(CPKT_SQLITE_NOTICE, "%s", long_text);
  if (!equal_record(&native_record, &facade_record) ||
      native_record.length != 699)
    goto fail;
  stage = 4;
  if (sqlite3_config(SQLITE_CONFIG_LOG, capture, &native_record) != SQLITE_OK)
    goto fail;
  native_count = 23;
  facade_count = 23;
  reset_record(&native_record);
  sqlite3_log(SQLITE_NOTICE, "count%n tail", &native_count);
  if (cpkt_sqlite_global_config_log(capture, &facade_record) != CPKT_SQLITE_OK)
    goto fail;
  reset_record(&facade_record);
  cpkt_sqlite_log(CPKT_SQLITE_NOTICE, "count%n tail", &facade_count);
  if (native_count != facade_count ||
      !equal_record(&native_record, &facade_record))
    goto fail;
  stage = 5;
  if (sqlite3_config(SQLITE_CONFIG_LOG, capture, &native_record) != SQLITE_OK)
    goto fail;
  reset_record(&native_record);
  sqlite3_log(SQLITE_WARNING, "signed %lld", signed_value);
  if (cpkt_sqlite_global_config_log(capture, &facade_record) != CPKT_SQLITE_OK)
    goto fail;
  reset_record(&facade_record);
  cpkt_sqlite_log_i64(CPKT_SQLITE_WARNING, "signed %lld", signed_bits);
  if (!equal_record(&native_record, &facade_record))
    goto fail;
  stage = 6;
  if (sqlite3_config(SQLITE_CONFIG_LOG, capture, &native_record) != SQLITE_OK)
    goto fail;
  reset_record(&native_record);
  sqlite3_log(SQLITE_NOTICE, "unsigned %llu", unsigned_value);
  if (cpkt_sqlite_global_config_log(capture, &facade_record) != CPKT_SQLITE_OK)
    goto fail;
  reset_record(&facade_record);
  cpkt_sqlite_log_u64(CPKT_SQLITE_NOTICE, "unsigned %llu", unsigned_bits);
  if (!equal_record(&native_record, &facade_record))
    goto fail;
  stage = 7;
  if (cpkt_sqlite_initialize() != CPKT_SQLITE_OK)
    goto fail;
  stage = 71;
  /* SQLITE_CONFIG_LOG is one of SQLite's explicit anytime config options. */
  if (cpkt_sqlite_global_config_log(capture, &native_record) != CPKT_SQLITE_OK)
    goto fail;
  stage = 72;
  reset_record(&native_record);
  sqlite3_log(SQLITE_NOTICE, "initialized");
  if (native_record.calls != 1)
    goto fail;
  if (cpkt_sqlite_global_config_log(capture, &facade_record) != CPKT_SQLITE_OK)
    goto fail;
  reset_record(&facade_record);
  cpkt_sqlite_log(CPKT_SQLITE_NOTICE, "initialized");
  if (!equal_record(&native_record, &facade_record))
    goto fail;
  stage = 73;
  if (cpkt_sqlite_shutdown() != CPKT_SQLITE_OK)
    goto fail;
  stage = 74;
  if (sqlite3_config(SQLITE_CONFIG_LOG, NULL, NULL) != SQLITE_OK)
    goto fail;
  return 0;
fail:
  fprintf(stderr, "SQLite native log mismatch at stage %d\n", stage);
  return 1;
}
