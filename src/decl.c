#include "decl.h"
#include "run.h"
#include "str.h"
#include "transform.h"

#include <stdlib.h>
#include <string.h>

static size_t decl_hash(const char *name, size_t len) {
  size_t h = 1469598103934665603ull;
  for (size_t i = 0; i < len; i++) {
    h ^= (unsigned char)name[i];
    h *= 1099511628211ull;
  }
  return h & (SKIM_BUCKET_COUNT - 1);
}

void skim_decl_reset(void) {
  skim_decl_table_t *table = &skim_run->decls;
  if (!table->decl_buckets) {
    table->decl_buckets = malloc(sizeof(*table->decl_buckets) * SKIM_BUCKET_COUNT);
    if (!table->decl_buckets) skim_die("out of memory");
    memset(table->decl_buckets, 0xff, sizeof(*table->decl_buckets) * SKIM_BUCKET_COUNT);
  }
  for (size_t i = 0; i < table->decl_count; i++)
    table->decl_buckets[table->decls[i].bucket] = (size_t)-1;
  free(table->decls);
  table->decls = NULL;
  table->decl_count = 0;
  table->decl_cap = 0;
}

void skim_decl_remember(const char *src, const char *name_start, const char *name_end, size_t pos, unsigned flags) {
  skim_decl_table_t *table = &skim_run->decls;
  size_t name_len = (size_t)(name_end - name_start);
  if (name_len == 0 || flags == 0) return;
  if (table->decl_count == table->decl_cap) {
    size_t cap = table->decl_cap ? table->decl_cap * 2 : 128;
    skim_decl_t *next = realloc(table->decls, sizeof(*table->decls) * cap);
    if (!next) skim_die("out of memory");
    table->decls = next;
    table->decl_cap = cap;
  }
  size_t bucket = decl_hash(name_start, name_len);
  size_t *buckets = table->decl_buckets;
  table->decls[table->decl_count] = (skim_decl_t){
    .src = src,
    .name = name_start,
    .name_len = name_len,
    .pos = pos,
    .flags = flags,
    .bucket = bucket,
    .next = buckets[bucket],
  };
  buckets[bucket] = table->decl_count;
  table->decl_count++;
}

bool skim_decl_seen_before(const char *src, const char *name, size_t pos, unsigned flags) {
  skim_decl_table_t *table = &skim_run->decls;
  size_t name_len = strlen(name);
  size_t *buckets = table->decl_buckets;
  for (size_t i = buckets[decl_hash(name, name_len)]; i != (size_t)-1; i = table->decls[i].next) {
    if (table->decls[i].src != src || table->decls[i].pos >= pos || !(table->decls[i].flags & flags)) continue;
    if (table->decls[i].name_len != name_len) continue;
    if (memcmp(table->decls[i].name, name, name_len) == 0) return true;
  }
  return false;
}
