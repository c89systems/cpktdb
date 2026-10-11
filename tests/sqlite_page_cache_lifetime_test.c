#include <cpkt/sqlite.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct test_page test_page;
typedef struct test_cache test_cache;

struct test_page {
  cpkt_sqlite_page public_page;
  unsigned long key;
  int pinned;
  test_page *next;
};

struct test_cache {
  cpkt_sqlite_page_cache public_cache;
  int page_bytes;
  int extra_bytes;
  test_page *pages;
};

static int created_pages;
static int freed_pages;
static int retained_pages;
static int evicted_pages;
static int cache_hits;
static int unpins;
static int row_count;

static void test_free_page(test_page *page) {
  free(page->public_page.buffer);
  free(page->public_page.extra);
  free(page);
  ++freed_pages;
}

static cpkt_sqlite_page_cache *test_create(void *context, int page_bytes,
                                           int extra_bytes, int purgeable) {
  test_cache *cache;
  (void)context;
  (void)purgeable;
  cache = (test_cache *)calloc(1, sizeof(*cache));
  if (cache == NULL)
    return NULL;
  cache->page_bytes = page_bytes;
  cache->extra_bytes = extra_bytes;
  return &cache->public_cache;
}

static void test_cache_size(cpkt_sqlite_page_cache *base, int size) {
  (void)base;
  (void)size;
}

static int test_page_count(cpkt_sqlite_page_cache *base) {
  test_cache *cache;
  test_page *page;
  int count;
  cache = (test_cache *)base;
  count = 0;
  for (page = cache->pages; page != NULL; page = page->next)
    ++count;
  return count;
}

static cpkt_sqlite_page *test_fetch(cpkt_sqlite_page_cache *base,
                                    unsigned long key, int create_flag) {
  test_cache *cache;
  test_page *page;
  cache = (test_cache *)base;
  for (page = cache->pages; page != NULL; page = page->next) {
    if (page->key == key) {
      page->pinned = 1;
      ++cache_hits;
      return &page->public_page;
    }
  }
  if (create_flag == 0)
    return NULL;
  page = (test_page *)calloc(1, sizeof(*page));
  if (page == NULL)
    return NULL;
  page->public_page.buffer = calloc(1, (size_t)cache->page_bytes);
  page->public_page.extra = calloc(1, (size_t)cache->extra_bytes);
  if (page->public_page.buffer == NULL || page->public_page.extra == NULL) {
    test_free_page(page);
    return NULL;
  }
  page->key = key;
  page->pinned = 1;
  page->next = cache->pages;
  cache->pages = page;
  ++created_pages;
  return &page->public_page;
}

static void test_unlink_page(test_cache *cache, test_page *page) {
  test_page **link;
  link = &cache->pages;
  while (*link != NULL && *link != page)
    link = &(*link)->next;
  if (*link == page) {
    *link = page->next;
    test_free_page(page);
  }
}

static void test_unpin(cpkt_sqlite_page_cache *base, cpkt_sqlite_page *public,
                       int discard) {
  test_cache *cache;
  test_page *page;
  cache = (test_cache *)base;
  page = (test_page *)public;
  page->pinned = 0;
  ++unpins;
  if (discard || (unpins % 3) == 0) {
    ++evicted_pages;
    test_unlink_page(cache, page);
  } else {
    ++retained_pages;
  }
}

static void test_rekey(cpkt_sqlite_page_cache *base, cpkt_sqlite_page *public,
                       unsigned long old_key, unsigned long new_key) {
  test_cache *cache;
  test_page *page;
  test_page *next;
  (void)old_key;
  cache = (test_cache *)base;
  page = cache->pages;
  while (page != NULL) {
    next = page->next;
    if (page->key == new_key && page != (test_page *)public)
      test_unlink_page(cache, page);
    page = next;
  }
  ((test_page *)public)->key = new_key;
}

static void test_truncate(cpkt_sqlite_page_cache *base, unsigned long limit) {
  test_cache *cache;
  test_page *page;
  test_page *next;
  cache = (test_cache *)base;
  page = cache->pages;
  while (page != NULL) {
    next = page->next;
    if (page->key >= limit)
      test_unlink_page(cache, page);
    page = next;
  }
}

static void test_destroy(cpkt_sqlite_page_cache *base) {
  test_cache *cache;
  test_page *page;
  test_page *next;
  cache = (test_cache *)base;
  page = cache->pages;
  while (page != NULL) {
    next = page->next;
    test_free_page(page);
    page = next;
  }
  free(cache);
}

static void test_shrink(cpkt_sqlite_page_cache *base) {
  test_cache *cache;
  test_page *page;
  test_page *next;
  cache = (test_cache *)base;
  page = cache->pages;
  while (page != NULL) {
    next = page->next;
    if (!page->pinned)
      test_unlink_page(cache, page);
    page = next;
  }
}

static int test_row(void *context, int columns, const char *const *values,
                    const char *const *names) {
  (void)context;
  (void)names;
  if (columns != 1 || values[0] == NULL || strcmp(values[0], "1000") != 0)
    return 1;
  ++row_count;
  return 0;
}

int main(void) {
  cpkt_sqlite_page_cache_methods methods;
  cpkt_sqlite *db;
  int status;
  const char *filename;
  filename = "sqlite-page-cache-lifetime.db";
  (void)remove(filename);
  memset(&methods, 0, sizeof(methods));
  methods.create = test_create;
  methods.cache_size = test_cache_size;
  methods.page_count = test_page_count;
  methods.fetch = test_fetch;
  methods.unpin = test_unpin;
  methods.rekey = test_rekey;
  methods.truncate = test_truncate;
  methods.destroy = test_destroy;
  methods.shrink = test_shrink;
  if (cpkt_sqlite_global_config_page_cache_methods_set(&methods) !=
      CPKT_SQLITE_OK)
    return 1;
  db = cpkt_sqlite_new(filename);
  if (db == NULL)
    return 2;
  status = cpkt_sqlite_exec(db,
                            "PRAGMA journal_mode=DELETE; PRAGMA cache_size=10;"
                            "CREATE TABLE t (id INTEGER, value TEXT);",
                            NULL, NULL);
  if (status != CPKT_SQLITE_OK)
    return 3;
  status = cpkt_sqlite_exec(
      db,
      "WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n "
      "WHERE x<1000) INSERT INTO t SELECT x, hex(randomblob(200)) FROM n;",
      NULL, NULL);
  if (status != CPKT_SQLITE_OK)
    return 4;
  status = cpkt_sqlite_exec(db, "SELECT count(*) FROM t;", test_row, NULL);
  if (status != CPKT_SQLITE_OK || row_count != 1)
    return 5;
  cpkt_sqlite_close(db);
  if (created_pages != freed_pages || retained_pages == 0 ||
      evicted_pages == 0 || cache_hits == 0) {
    fprintf(stderr,
            "pages created=%d freed=%d retained=%d evicted=%d "
            "hits=%d\n",
            created_pages, freed_pages, retained_pages, evicted_pages,
            cache_hits);
    return 6;
  }
  db = cpkt_sqlite_new(filename);
  if (db == NULL)
    return 7;
  status = cpkt_sqlite_exec(db, "SELECT count(*) FROM t;", test_row, NULL);
  cpkt_sqlite_close(db);
  if (status != CPKT_SQLITE_OK || row_count != 2 ||
      created_pages != freed_pages)
    return 8;
  (void)remove(filename);
  return 0;
}
