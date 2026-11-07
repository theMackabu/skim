#include "decl.h"
#include "str.h"
#include "transform.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
  const char *src;
  const char *name;
  size_t name_len;
  size_t pos;
  unsigned flags;
  size_t bucket;
  size_t next;
} skim_decl_t;

static skim_decl_t *decls;
static size_t decl_count;
static size_t decl_cap;

static size_t decl_hash(const char *name, size_t len) {
  size_t h = 1469598103934665603ull;
  for (size_t i = 0; i < len; i++) {
    h ^= (unsigned char)name[i];
    h *= 1099511628211ull;
  }
  return h & (SKIM_BUCKET_COUNT - 1);
}

void skim_decl_reset(void) {
  if (!skim_state->decl_buckets) {
    skim_state->decl_buckets = malloc(sizeof(*skim_state->decl_buckets) * SKIM_BUCKET_COUNT);
    if (!skim_state->decl_buckets) skim_die("out of memory");
    memset(skim_state->decl_buckets, 0xff, sizeof(*skim_state->decl_buckets) * SKIM_BUCKET_COUNT);
  }
  for (size_t i = 0; i < decl_count; i++)
    skim_state->decl_buckets[decls[i].bucket] = (size_t)-1;
  free(decls);
  decls = NULL;
  decl_count = 0;
  decl_cap = 0;
}

void skim_decl_remember(const char *src, const char *name_start, const char *name_end, size_t pos, unsigned flags) {
  size_t name_len = (size_t)(name_end - name_start);
  if (name_len == 0 || flags == 0) return;
  if (decl_count == decl_cap) {
    size_t cap = decl_cap ? decl_cap * 2 : 128;
    skim_decl_t *next = realloc(decls, sizeof(*decls) * cap);
    if (!next) skim_die("out of memory");
    decls = next;
    decl_cap = cap;
  }
  size_t bucket = decl_hash(name_start, name_len);
  size_t *buckets = skim_state->decl_buckets;
  decls[decl_count] = (skim_decl_t){
    .src = src,
    .name = name_start,
    .name_len = name_len,
    .pos = pos,
    .flags = flags,
    .bucket = bucket,
    .next = buckets[bucket],
  };
  buckets[bucket] = decl_count;
  decl_count++;
}

bool skim_decl_seen_before(const char *src, const char *name, size_t pos, unsigned flags) {
  size_t name_len = strlen(name);
  size_t *buckets = skim_state->decl_buckets;
  for (size_t i = buckets[decl_hash(name, name_len)]; i != (size_t)-1; i = decls[i].next) {
    if (decls[i].src != src || decls[i].pos >= pos || !(decls[i].flags & flags)) continue;
    if (decls[i].name_len != name_len) continue;
    if (memcmp(decls[i].name, name, name_len) == 0) return true;
  }
  return false;
}
