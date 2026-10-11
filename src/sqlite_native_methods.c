#include "sqlite_native_methods.h"

#include <limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

typedef struct cpkt_sqlite_native_mutex_view cpkt_sqlite_native_mutex_view;
typedef struct cpkt_sqlite_native_mutex_item cpkt_sqlite_native_mutex_item;
typedef struct cpkt_sqlite_native_cache_view cpkt_sqlite_native_cache_view;
typedef struct cpkt_sqlite_native_cache_item cpkt_sqlite_native_cache_item;
typedef struct cpkt_sqlite_native_page_item cpkt_sqlite_native_page_item;

enum { CPKT_SQLITE_CACHE_BUCKETS = 64 };

struct cpkt_sqlite_native_mutex_item {
  sqlite3_mutex *native;
  int type;
  cpkt_sqlite_native_mutex_item *next;
};

struct cpkt_sqlite_native_mutex_view {
  sqlite3_mutex_methods native;
  pthread_mutex_t lock;
  cpkt_sqlite_native_mutex_item *static_items;
  cpkt_sqlite_native_mutex_view *next;
};

struct cpkt_sqlite_native_cache_view {
  sqlite3_pcache_methods2 native;
  cpkt_sqlite_native_cache_view *next;
};

struct cpkt_sqlite_native_cache_item {
  cpkt_sqlite_page_cache public_cache;
  sqlite3_pcache *native;
  cpkt_sqlite_native_cache_view *view;
  cpkt_sqlite_native_page_item *small_buckets[CPKT_SQLITE_CACHE_BUCKETS];
  cpkt_sqlite_native_page_item **buckets;
  size_t bucket_count;
  size_t page_count;
};

struct cpkt_sqlite_native_page_item {
  cpkt_sqlite_page public_page;
  sqlite3_pcache_page *native;
  unsigned int key;
  cpkt_sqlite_native_page_item *next;
  cpkt_sqlite_native_page_item **previous;
};

static pthread_mutex_t cpkt_sqlite_native_method_lock =
    PTHREAD_MUTEX_INITIALIZER;
static cpkt_sqlite_native_mutex_view *cpkt_sqlite_mutex_views;
static cpkt_sqlite_native_cache_view *cpkt_sqlite_cache_views;

static cpkt_sqlite_native_mutex_view *
cpkt_sqlite_mutex_view_lookup(const void *context) {
  cpkt_sqlite_native_mutex_view *view;
  for (view = cpkt_sqlite_mutex_views; view != NULL; view = view->next)
    if (view == context)
      return view;
  return NULL;
}

static cpkt_sqlite_native_cache_view *
cpkt_sqlite_cache_view_lookup(const void *context) {
  cpkt_sqlite_native_cache_view *view;
  for (view = cpkt_sqlite_cache_views; view != NULL; view = view->next)
    if (view == context)
      return view;
  return NULL;
}

static int cpkt_sqlite_mutex_view_init(void *context) {
  cpkt_sqlite_native_mutex_view *view;
  view = (cpkt_sqlite_native_mutex_view *)context;
  return view->native.xMutexInit == NULL ? SQLITE_OK
                                         : view->native.xMutexInit();
}

static int cpkt_sqlite_mutex_view_end(void *context) {
  cpkt_sqlite_native_mutex_view *view;
  cpkt_sqlite_native_mutex_item *item;
  cpkt_sqlite_native_mutex_item *next;
  int status;
  view = (cpkt_sqlite_native_mutex_view *)context;
  status =
      view->native.xMutexEnd == NULL ? SQLITE_OK : view->native.xMutexEnd();
  pthread_mutex_lock(&view->lock);
  item = view->static_items;
  view->static_items = NULL;
  pthread_mutex_unlock(&view->lock);
  while (item != NULL) {
    next = item->next;
    free(item);
    item = next;
  }
  return status;
}

static void *cpkt_sqlite_mutex_view_alloc(void *context, int type) {
  cpkt_sqlite_native_mutex_view *view;
  cpkt_sqlite_native_mutex_item *item;
  view = (cpkt_sqlite_native_mutex_view *)context;
  if (type >= SQLITE_MUTEX_STATIC_MAIN) {
    pthread_mutex_lock(&view->lock);
    for (item = view->static_items; item != NULL; item = item->next) {
      if (item->type == type) {
        pthread_mutex_unlock(&view->lock);
        return item;
      }
    }
  }
  item = (cpkt_sqlite_native_mutex_item *)calloc(1, sizeof(*item));
  if (item == NULL) {
    if (type >= SQLITE_MUTEX_STATIC_MAIN)
      pthread_mutex_unlock(&view->lock);
    return NULL;
  }
  item->native = view->native.xMutexAlloc(type);
  if (item->native == NULL) {
    free(item);
    if (type >= SQLITE_MUTEX_STATIC_MAIN)
      pthread_mutex_unlock(&view->lock);
    return NULL;
  }
  item->type = type;
  if (type >= SQLITE_MUTEX_STATIC_MAIN) {
    item->next = view->static_items;
    view->static_items = item;
    pthread_mutex_unlock(&view->lock);
  }
  return item;
}

static void cpkt_sqlite_mutex_view_free(void *context, void *mutex) {
  cpkt_sqlite_native_mutex_view *view;
  cpkt_sqlite_native_mutex_item *item;
  view = (cpkt_sqlite_native_mutex_view *)context;
  item = (cpkt_sqlite_native_mutex_item *)mutex;
  if (item == NULL || item->type >= SQLITE_MUTEX_STATIC_MAIN)
    return;
  view->native.xMutexFree(item->native);
  free(item);
}

static void cpkt_sqlite_mutex_view_enter(void *context, void *mutex) {
  cpkt_sqlite_native_mutex_view *view;
  cpkt_sqlite_native_mutex_item *item;
  view = (cpkt_sqlite_native_mutex_view *)context;
  item = (cpkt_sqlite_native_mutex_item *)mutex;
  view->native.xMutexEnter(item->native);
}

static int cpkt_sqlite_mutex_view_try(void *context, void *mutex) {
  cpkt_sqlite_native_mutex_view *view;
  cpkt_sqlite_native_mutex_item *item;
  view = (cpkt_sqlite_native_mutex_view *)context;
  item = (cpkt_sqlite_native_mutex_item *)mutex;
  return view->native.xMutexTry(item->native);
}

static void cpkt_sqlite_mutex_view_leave(void *context, void *mutex) {
  cpkt_sqlite_native_mutex_view *view;
  cpkt_sqlite_native_mutex_item *item;
  view = (cpkt_sqlite_native_mutex_view *)context;
  item = (cpkt_sqlite_native_mutex_item *)mutex;
  view->native.xMutexLeave(item->native);
}

static int cpkt_sqlite_mutex_view_held(void *context, void *mutex) {
  cpkt_sqlite_native_mutex_view *view;
  cpkt_sqlite_native_mutex_item *item;
  view = (cpkt_sqlite_native_mutex_view *)context;
  item = (cpkt_sqlite_native_mutex_item *)mutex;
  return view->native.xMutexHeld(item->native);
}

static int cpkt_sqlite_mutex_view_not_held(void *context, void *mutex) {
  cpkt_sqlite_native_mutex_view *view;
  cpkt_sqlite_native_mutex_item *item;
  view = (cpkt_sqlite_native_mutex_view *)context;
  item = (cpkt_sqlite_native_mutex_item *)mutex;
  return view->native.xMutexNotheld(item->native);
}

/** Build an owned callable C89 view of a native mutex method table. */
int cpkt_sqlite_native_mutex_methods_get(const sqlite3_mutex_methods *native,
                                         cpkt_sqlite_mutex_methods *out) {
  cpkt_sqlite_native_mutex_view *view;
  memset(out, 0, sizeof(*out));
  if (native->xMutexAlloc == NULL)
    return SQLITE_OK;
  view = (cpkt_sqlite_native_mutex_view *)calloc(1, sizeof(*view));
  if (view == NULL)
    return SQLITE_NOMEM;
  view->native = *native;
  if (pthread_mutex_init(&view->lock, NULL) != 0) {
    free(view);
    return SQLITE_NOMEM;
  }
  pthread_mutex_lock(&cpkt_sqlite_native_method_lock);
  view->next = cpkt_sqlite_mutex_views;
  cpkt_sqlite_mutex_views = view;
  pthread_mutex_unlock(&cpkt_sqlite_native_method_lock);
  out->context = view;
  out->initialize =
      native->xMutexInit == NULL ? NULL : cpkt_sqlite_mutex_view_init;
  out->shutdown = native->xMutexEnd == NULL ? NULL : cpkt_sqlite_mutex_view_end;
  out->allocate = cpkt_sqlite_mutex_view_alloc;
  out->free = native->xMutexFree == NULL ? NULL : cpkt_sqlite_mutex_view_free;
  out->enter =
      native->xMutexEnter == NULL ? NULL : cpkt_sqlite_mutex_view_enter;
  out->try_enter =
      native->xMutexTry == NULL ? NULL : cpkt_sqlite_mutex_view_try;
  out->leave =
      native->xMutexLeave == NULL ? NULL : cpkt_sqlite_mutex_view_leave;
  out->held = native->xMutexHeld == NULL ? NULL : cpkt_sqlite_mutex_view_held;
  out->not_held =
      native->xMutexNotheld == NULL ? NULL : cpkt_sqlite_mutex_view_not_held;
  return SQLITE_OK;
}

/** Recover the selected native table from an unchanged owned view. */
int cpkt_sqlite_native_mutex_methods_unwrap(
    const cpkt_sqlite_mutex_methods *methods, sqlite3_mutex_methods *out) {
  cpkt_sqlite_native_mutex_view *view;
  if (methods == NULL || methods->allocate != cpkt_sqlite_mutex_view_alloc)
    return 0;
  pthread_mutex_lock(&cpkt_sqlite_native_method_lock);
  view = cpkt_sqlite_mutex_view_lookup(methods->context);
  if (view != NULL &&
      methods->initialize == (view->native.xMutexInit == NULL
                                  ? NULL
                                  : cpkt_sqlite_mutex_view_init) &&
      methods->shutdown == (view->native.xMutexEnd == NULL
                                ? NULL
                                : cpkt_sqlite_mutex_view_end) &&
      methods->free == (view->native.xMutexFree == NULL
                            ? NULL
                            : cpkt_sqlite_mutex_view_free) &&
      methods->enter == (view->native.xMutexEnter == NULL
                             ? NULL
                             : cpkt_sqlite_mutex_view_enter) &&
      methods->try_enter == (view->native.xMutexTry == NULL
                                 ? NULL
                                 : cpkt_sqlite_mutex_view_try) &&
      methods->leave == (view->native.xMutexLeave == NULL
                             ? NULL
                             : cpkt_sqlite_mutex_view_leave) &&
      methods->held == (view->native.xMutexHeld == NULL
                            ? NULL
                            : cpkt_sqlite_mutex_view_held) &&
      methods->not_held == (view->native.xMutexNotheld == NULL
                                ? NULL
                                : cpkt_sqlite_mutex_view_not_held))
    *out = view->native;
  else
    view = NULL;
  pthread_mutex_unlock(&cpkt_sqlite_native_method_lock);
  return view != NULL;
}

/** Release a native mutex method view after its mutexes are gone. */
void cpkt_sqlite_native_mutex_methods_release(
    cpkt_sqlite_mutex_methods *methods) {
  cpkt_sqlite_native_mutex_view **slot;
  cpkt_sqlite_native_mutex_view *view;
  if (methods == NULL)
    return;
  pthread_mutex_lock(&cpkt_sqlite_native_method_lock);
  slot = &cpkt_sqlite_mutex_views;
  while (*slot != NULL && *slot != methods->context)
    slot = &(*slot)->next;
  view = *slot;
  if (view != NULL)
    *slot = view->next;
  pthread_mutex_unlock(&cpkt_sqlite_native_method_lock);
  if (view != NULL) {
    cpkt_sqlite_native_mutex_item *item;
    cpkt_sqlite_native_mutex_item *next;
    item = view->static_items;
    while (item != NULL) {
      next = item->next;
      free(item);
      item = next;
    }
    pthread_mutex_destroy(&view->lock);
    free(view);
    memset(methods, 0, sizeof(*methods));
  }
}

static int cpkt_sqlite_cache_view_init(void *context) {
  cpkt_sqlite_native_cache_view *view;
  view = (cpkt_sqlite_native_cache_view *)context;
  return view->native.xInit == NULL ? SQLITE_OK
                                    : view->native.xInit(view->native.pArg);
}

static void cpkt_sqlite_cache_view_shutdown(void *context) {
  cpkt_sqlite_native_cache_view *view;
  view = (cpkt_sqlite_native_cache_view *)context;
  if (view->native.xShutdown != NULL)
    view->native.xShutdown(view->native.pArg);
}

static cpkt_sqlite_native_cache_item *
cpkt_sqlite_cache_item(cpkt_sqlite_page_cache *cache) {
  return cache == NULL ? NULL : (cpkt_sqlite_native_cache_item *)cache->state;
}

static size_t
cpkt_sqlite_cache_bucket(const cpkt_sqlite_native_cache_item *item,
                         unsigned int key) {
  return ((size_t)key) & (item->bucket_count - 1U);
}

static cpkt_sqlite_native_page_item *
cpkt_sqlite_cache_page_find(cpkt_sqlite_native_cache_item *item,
                            unsigned int key) {
  cpkt_sqlite_native_page_item *page;
  for (page = item->buckets[cpkt_sqlite_cache_bucket(item, key)]; page != NULL;
       page = page->next)
    if (page->key == key)
      return page;
  return NULL;
}

static void cpkt_sqlite_cache_page_link(cpkt_sqlite_native_cache_item *item,
                                        cpkt_sqlite_native_page_item *page) {
  cpkt_sqlite_native_page_item **slot;
  slot = &item->buckets[cpkt_sqlite_cache_bucket(item, page->key)];
  page->next = *slot;
  page->previous = slot;
  if (*slot != NULL)
    (*slot)->previous = &page->next;
  *slot = page;
}

static void cpkt_sqlite_cache_page_unlink(cpkt_sqlite_native_page_item *page) {
  *page->previous = page->next;
  if (page->next != NULL)
    page->next->previous = page->previous;
  page->next = NULL;
  page->previous = NULL;
}

/* Rehash only when the pinned-page population grows. Repeated fetches do not
 * allocate or traverse all pages. An allocation failure leaves the old index
 * valid; a later insertion may retry growth. */
static void cpkt_sqlite_cache_pages_grow(cpkt_sqlite_native_cache_item *item) {
  cpkt_sqlite_native_page_item **buckets;
  cpkt_sqlite_native_page_item **old_buckets;
  cpkt_sqlite_native_page_item *page;
  cpkt_sqlite_native_page_item *next;
  size_t old_count;
  size_t count;
  size_t index;
  if (item->page_count <= item->bucket_count * 2U ||
      item->bucket_count > ((size_t)-1) / (2U * sizeof(*buckets)))
    return;
  old_count = item->bucket_count;
  count = old_count * 2U;
  buckets = (cpkt_sqlite_native_page_item **)calloc(count, sizeof(*buckets));
  if (buckets == NULL)
    return;
  old_buckets = item->buckets;
  item->buckets = buckets;
  item->bucket_count = count;
  for (index = 0; index < old_count; ++index) {
    page = old_buckets[index];
    while (page != NULL) {
      next = page->next;
      cpkt_sqlite_cache_page_link(item, page);
      page = next;
    }
  }
  if (old_buckets != item->small_buckets)
    free(old_buckets);
}

static cpkt_sqlite_page_cache *cpkt_sqlite_cache_view_create(void *context,
                                                             int page_bytes,
                                                             int extra_bytes,
                                                             int purgeable) {
  cpkt_sqlite_native_cache_view *view;
  cpkt_sqlite_native_cache_item *item;
  sqlite3_pcache *native;
  view = (cpkt_sqlite_native_cache_view *)context;
  native = view->native.xCreate(page_bytes, extra_bytes, purgeable);
  if (native == NULL)
    return NULL;
  item = (cpkt_sqlite_native_cache_item *)calloc(1, sizeof(*item));
  if (item == NULL) {
    view->native.xDestroy(native);
    return NULL;
  }
  item->native = native;
  item->view = view;
  item->buckets = item->small_buckets;
  item->bucket_count = CPKT_SQLITE_CACHE_BUCKETS;
  item->public_cache.state = item;
  return &item->public_cache;
}

static void cpkt_sqlite_cache_view_size(cpkt_sqlite_page_cache *cache,
                                        int count) {
  cpkt_sqlite_native_cache_item *item;
  item = cpkt_sqlite_cache_item(cache);
  if (item != NULL)
    item->view->native.xCachesize(item->native, count);
}

static int cpkt_sqlite_cache_view_count(cpkt_sqlite_page_cache *cache) {
  cpkt_sqlite_native_cache_item *item;
  item = cpkt_sqlite_cache_item(cache);
  return item == NULL ? 0 : item->view->native.xPagecount(item->native);
}

static cpkt_sqlite_page *
cpkt_sqlite_cache_view_fetch(cpkt_sqlite_page_cache *cache, unsigned long key,
                             int create_flag) {
  cpkt_sqlite_native_cache_item *item;
  cpkt_sqlite_native_page_item *page;
  sqlite3_pcache_page *native;
  item = cpkt_sqlite_cache_item(cache);
  if (item == NULL || (sizeof(unsigned long) > sizeof(unsigned int) &&
                       key > (unsigned long)UINT_MAX))
    return NULL;
  native =
      item->view->native.xFetch(item->native, (unsigned int)key, create_flag);
  if (native == NULL)
    return NULL;
  page = cpkt_sqlite_cache_page_find(item, (unsigned int)key);
  if (page != NULL)
    return &page->public_page;
  page = (cpkt_sqlite_native_page_item *)calloc(1, sizeof(*page));
  if (page == NULL) {
    item->view->native.xUnpin(item->native, native, 0);
    return NULL;
  }
  page->native = native;
  page->key = (unsigned int)key;
  page->public_page.buffer = native->pBuf;
  page->public_page.extra = native->pExtra;
  page->public_page.state = page;
  cpkt_sqlite_cache_page_link(item, page);
  ++item->page_count;
  cpkt_sqlite_cache_pages_grow(item);
  return &page->public_page;
}

static void cpkt_sqlite_cache_view_unpin(cpkt_sqlite_page_cache *cache,
                                         cpkt_sqlite_page *page, int discard) {
  cpkt_sqlite_native_cache_item *item;
  cpkt_sqlite_native_page_item *native_page;
  item = cpkt_sqlite_cache_item(cache);
  native_page =
      page == NULL ? NULL : (cpkt_sqlite_native_page_item *)page->state;
  if (item == NULL || native_page == NULL)
    return;
  item->view->native.xUnpin(item->native, native_page->native, discard);
  cpkt_sqlite_cache_page_unlink(native_page);
  --item->page_count;
  free(native_page);
}

static void cpkt_sqlite_cache_view_rekey(cpkt_sqlite_page_cache *cache,
                                         cpkt_sqlite_page *page,
                                         unsigned long old_key,
                                         unsigned long new_key) {
  cpkt_sqlite_native_cache_item *item;
  cpkt_sqlite_native_page_item *native_page;
  item = cpkt_sqlite_cache_item(cache);
  native_page =
      page == NULL ? NULL : (cpkt_sqlite_native_page_item *)page->state;
  if (item != NULL && native_page != NULL &&
      (sizeof(unsigned long) == sizeof(unsigned int) ||
       (old_key <= (unsigned long)UINT_MAX &&
        new_key <= (unsigned long)UINT_MAX))) {
    item->view->native.xRekey(item->native, native_page->native,
                              (unsigned int)old_key, (unsigned int)new_key);
    cpkt_sqlite_cache_page_unlink(native_page);
    native_page->key = (unsigned int)new_key;
    cpkt_sqlite_cache_page_link(item, native_page);
  }
}

static void cpkt_sqlite_cache_view_truncate(cpkt_sqlite_page_cache *cache,
                                            unsigned long limit) {
  cpkt_sqlite_native_cache_item *item;
  cpkt_sqlite_native_page_item *page;
  cpkt_sqlite_native_page_item *next;
  size_t index;
  item = cpkt_sqlite_cache_item(cache);
  if (item == NULL || (sizeof(unsigned long) > sizeof(unsigned int) &&
                       limit > (unsigned long)UINT_MAX))
    return;
  item->view->native.xTruncate(item->native, (unsigned int)limit);
  for (index = 0; index < item->bucket_count; ++index) {
    page = item->buckets[index];
    while (page != NULL) {
      next = page->next;
      if (page->key >= (unsigned int)limit) {
        cpkt_sqlite_cache_page_unlink(page);
        --item->page_count;
        free(page);
      }
      page = next;
    }
  }
}

static void cpkt_sqlite_cache_view_destroy(cpkt_sqlite_page_cache *cache) {
  cpkt_sqlite_native_cache_item *item;
  cpkt_sqlite_native_page_item *page;
  cpkt_sqlite_native_page_item *next;
  size_t index;
  item = cpkt_sqlite_cache_item(cache);
  if (item != NULL) {
    item->view->native.xDestroy(item->native);
    for (index = 0; index < item->bucket_count; ++index) {
      page = item->buckets[index];
      while (page != NULL) {
        next = page->next;
        free(page);
        page = next;
      }
    }
    if (item->buckets != item->small_buckets)
      free(item->buckets);
    free(item);
  }
}

static void cpkt_sqlite_cache_view_shrink(cpkt_sqlite_page_cache *cache) {
  cpkt_sqlite_native_cache_item *item;
  item = cpkt_sqlite_cache_item(cache);
  if (item != NULL && item->view->native.xShrink != NULL)
    item->view->native.xShrink(item->native);
}

/** Build an owned C89 view of native page-cache methods. */
int cpkt_sqlite_native_page_cache_methods_get(
    const sqlite3_pcache_methods2 *native,
    cpkt_sqlite_page_cache_methods *out) {
  cpkt_sqlite_native_cache_view *view;
  memset(out, 0, sizeof(*out));
  if (native->xCreate == NULL)
    return SQLITE_OK;
  view = (cpkt_sqlite_native_cache_view *)calloc(1, sizeof(*view));
  if (view == NULL)
    return SQLITE_NOMEM;
  view->native = *native;
  pthread_mutex_lock(&cpkt_sqlite_native_method_lock);
  view->next = cpkt_sqlite_cache_views;
  cpkt_sqlite_cache_views = view;
  pthread_mutex_unlock(&cpkt_sqlite_native_method_lock);
  out->context = view;
  out->initialize = native->xInit == NULL ? NULL : cpkt_sqlite_cache_view_init;
  out->shutdown =
      native->xShutdown == NULL ? NULL : cpkt_sqlite_cache_view_shutdown;
  out->create = cpkt_sqlite_cache_view_create;
  out->cache_size =
      native->xCachesize == NULL ? NULL : cpkt_sqlite_cache_view_size;
  out->page_count =
      native->xPagecount == NULL ? NULL : cpkt_sqlite_cache_view_count;
  out->fetch = native->xFetch == NULL ? NULL : cpkt_sqlite_cache_view_fetch;
  out->unpin = native->xUnpin == NULL ? NULL : cpkt_sqlite_cache_view_unpin;
  out->rekey = native->xRekey == NULL ? NULL : cpkt_sqlite_cache_view_rekey;
  out->truncate =
      native->xTruncate == NULL ? NULL : cpkt_sqlite_cache_view_truncate;
  out->destroy =
      native->xDestroy == NULL ? NULL : cpkt_sqlite_cache_view_destroy;
  out->shrink = native->xShrink == NULL ? NULL : cpkt_sqlite_cache_view_shrink;
  return SQLITE_OK;
}

/** Recover the selected native cache table from an unchanged view. */
int cpkt_sqlite_native_page_cache_methods_unwrap(
    const cpkt_sqlite_page_cache_methods *methods,
    sqlite3_pcache_methods2 *out) {
  cpkt_sqlite_native_cache_view *view;
  if (methods == NULL || methods->create != cpkt_sqlite_cache_view_create)
    return 0;
  pthread_mutex_lock(&cpkt_sqlite_native_method_lock);
  view = cpkt_sqlite_cache_view_lookup(methods->context);
  if (view != NULL &&
      methods->initialize ==
          (view->native.xInit == NULL ? NULL : cpkt_sqlite_cache_view_init) &&
      methods->shutdown == (view->native.xShutdown == NULL
                                ? NULL
                                : cpkt_sqlite_cache_view_shutdown) &&
      methods->cache_size == (view->native.xCachesize == NULL
                                  ? NULL
                                  : cpkt_sqlite_cache_view_size) &&
      methods->page_count == (view->native.xPagecount == NULL
                                  ? NULL
                                  : cpkt_sqlite_cache_view_count) &&
      methods->fetch ==
          (view->native.xFetch == NULL ? NULL : cpkt_sqlite_cache_view_fetch) &&
      methods->unpin ==
          (view->native.xUnpin == NULL ? NULL : cpkt_sqlite_cache_view_unpin) &&
      methods->rekey ==
          (view->native.xRekey == NULL ? NULL : cpkt_sqlite_cache_view_rekey) &&
      methods->truncate == (view->native.xTruncate == NULL
                                ? NULL
                                : cpkt_sqlite_cache_view_truncate) &&
      methods->destroy == (view->native.xDestroy == NULL
                               ? NULL
                               : cpkt_sqlite_cache_view_destroy) &&
      methods->shrink ==
          (view->native.xShrink == NULL ? NULL : cpkt_sqlite_cache_view_shrink))
    *out = view->native;
  else
    view = NULL;
  pthread_mutex_unlock(&cpkt_sqlite_native_method_lock);
  return view != NULL;
}

/** Release native page-cache method metadata after pages are gone. */
void cpkt_sqlite_native_page_cache_methods_release(
    cpkt_sqlite_page_cache_methods *methods) {
  cpkt_sqlite_native_cache_view **slot;
  cpkt_sqlite_native_cache_view *view;
  if (methods == NULL)
    return;
  pthread_mutex_lock(&cpkt_sqlite_native_method_lock);
  slot = &cpkt_sqlite_cache_views;
  while (*slot != NULL && *slot != methods->context)
    slot = &(*slot)->next;
  view = *slot;
  if (view != NULL)
    *slot = view->next;
  pthread_mutex_unlock(&cpkt_sqlite_native_method_lock);
  if (view != NULL) {
    free(view);
    memset(methods, 0, sizeof(*methods));
  }
}
