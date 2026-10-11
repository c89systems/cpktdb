#include <cpkt/sqlite.h>
#include <sqlite3.h>

#include <stddef.h>
#include <string.h>

static cpkt_sqlite_page_cache test_cache;
static cpkt_sqlite_page test_page;
static unsigned char test_buffer[1024];
static union {
  double alignment;
  unsigned char bytes[256];
} test_extra;
static void *tracked_allocation;
static int tracking;
static int tracked_free_count;
static int unexpected_allocation_count;
static int discard_count;
static int truncate_count;

void *__real_calloc(size_t count, size_t size);
void __real_free(void *pointer);

void *__wrap_calloc(size_t count, size_t size) {
  void *pointer;
  pointer = __real_calloc(count, size);
  if (tracking && pointer != NULL) {
    if (tracked_allocation != NULL)
      ++unexpected_allocation_count;
    else
      tracked_allocation = pointer;
  }
  return pointer;
}

void __wrap_free(void *pointer) {
  if (tracking && pointer == tracked_allocation) {
    tracked_allocation = NULL;
    ++tracked_free_count;
  }
  __real_free(pointer);
}

static cpkt_sqlite_page_cache *test_create(void *context, int page_bytes,
                                           int extra_bytes, int purgeable) {
  (void)context;
  (void)page_bytes;
  (void)extra_bytes;
  (void)purgeable;
  return &test_cache;
}

static void test_cache_size(cpkt_sqlite_page_cache *cache, int size) {
  (void)cache;
  (void)size;
}

static int test_page_count(cpkt_sqlite_page_cache *cache) {
  (void)cache;
  return 1;
}

static cpkt_sqlite_page *test_fetch(cpkt_sqlite_page_cache *cache,
                                    unsigned long key, int create) {
  (void)cache;
  (void)key;
  (void)create;
  return &test_page;
}

static void test_unpin(cpkt_sqlite_page_cache *cache, cpkt_sqlite_page *page,
                       int discard) {
  (void)cache;
  if (page == &test_page && discard)
    ++discard_count;
}

static void test_rekey(cpkt_sqlite_page_cache *cache, cpkt_sqlite_page *page,
                       unsigned long old_key, unsigned long new_key) {
  (void)cache;
  (void)page;
  (void)old_key;
  (void)new_key;
}

static void test_truncate(cpkt_sqlite_page_cache *cache, unsigned long limit) {
  (void)cache;
  (void)limit;
  ++truncate_count;
}

static void test_destroy(cpkt_sqlite_page_cache *cache) { (void)cache; }

int main(void) {
  cpkt_sqlite_page_cache_methods public_methods;
  sqlite3_pcache_methods2 native_methods;
  sqlite3_pcache *native_cache;
  sqlite3_pcache_page *native_page;
  int index;

  memset(&public_methods, 0, sizeof(public_methods));
  public_methods.create = test_create;
  public_methods.cache_size = test_cache_size;
  public_methods.page_count = test_page_count;
  public_methods.fetch = test_fetch;
  public_methods.unpin = test_unpin;
  public_methods.rekey = test_rekey;
  public_methods.truncate = test_truncate;
  public_methods.destroy = test_destroy;
  test_page.buffer = test_buffer;
  /* On 32-bit targets, exercise the documented pointer-aligned backend page
   * whose extra storage begins four bytes past an eight-byte boundary. */
  test_page.extra = test_extra.bytes;
  if (sizeof(void *) == 4U && ((size_t)test_page.extra & 7U) == 0U)
    test_page.extra = test_extra.bytes + 4;
  test_page.state = NULL;
  if (cpkt_sqlite_global_config_page_cache_methods_set(&public_methods) !=
          CPKT_SQLITE_OK ||
      sqlite3_config(SQLITE_CONFIG_GETPCACHE2, &native_methods) != SQLITE_OK)
    return 1;
  native_cache = native_methods.xCreate(1024, 16, 1);
  if (native_cache == NULL)
    return 2;
  for (index = 0; index < 64; ++index) {
    tracking = 1;
    native_page =
        native_methods.xFetch(native_cache, (unsigned int)index + 1U, 2);
    if (native_page == NULL || tracked_allocation != NULL ||
        unexpected_allocation_count != 0)
      return 3;
    if (native_page->pBuf != test_buffer ||
        native_page->pExtra == test_page.extra)
      return 4;
    if (((size_t)native_page->pExtra & 7U) != 0U)
      return 12;
    native_methods.xUnpin(native_cache, native_page, 1);
    if (tracked_allocation != NULL || tracked_free_count != 0 ||
        discard_count != index + 1)
      return 5;
    tracking = 0;
  }
  tracking = 1;
  native_page = native_methods.xFetch(native_cache, 65U, 2);
  if (native_page == NULL || tracked_allocation != NULL)
    return 6;
  native_methods.xTruncate(native_cache, 65U);
  if (tracked_allocation != NULL || tracked_free_count != 0 ||
      truncate_count != 1)
    return 7;
  /* A retained page must keep the same native wrapper and pExtra address. */
  tracking = 1;
  native_page = native_methods.xFetch(native_cache, 66U, 2);
  if (native_page == NULL || tracked_allocation != NULL)
    return 8;
  native_methods.xUnpin(native_cache, native_page, 0);
  if (tracked_allocation != NULL || tracked_free_count != 0 ||
      discard_count != 64)
    return 9;
  if (native_methods.xFetch(native_cache, 66U, 0) != native_page)
    return 10;
  native_methods.xUnpin(native_cache, native_page, 0);
  if (tracked_allocation != NULL || tracked_free_count != 0)
    return 11;
  tracking = 0;
  native_methods.xDestroy(native_cache);
  return 0;
}
