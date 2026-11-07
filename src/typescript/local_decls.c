#include "chars.h"
#include "run.h"
#include "scan_cache.h"
#include "skip.h"
#include "str.h"
#include "typescript.h"

#include <stdlib.h>
#include <string.h>

void skim_ts_namespace_decl_index_invalidate(void) {
  skim_local_decl_index_t *decl_index = &skim_run->ts.local_decls;
  free(decl_index->items);
  decl_index->items = NULL;
  decl_index->src = NULL;
  decl_index->count = 0;
  decl_index->cap = 0;
}

static size_t local_decl_keyword_len(const char *word, size_t n) {
  bool keyword = (n == 3 && (memcmp(word, "var", 3) == 0 || memcmp(word, "let", 3) == 0)) ||
                 (n == 5 && (memcmp(word, "class", 5) == 0 || memcmp(word, "const", 5) == 0)) ||
                 (n == 8 && memcmp(word, "function", 8) == 0);
  return keyword ? n : 0;
}

static void local_decls_for_source(const char *src, size_t len) {
  skim_local_decl_index_t *decl_index = &skim_run->ts.local_decls;
  skim_ts_namespace_decl_index_invalidate();
  decl_index->src = src;
  for (size_t i = 0; i < len;) {
    if (!skim_is_id_part(src[i])) {
      i++;
      continue;
    }
    size_t word = i;
    while (i < len && skim_is_id_part(src[i]))
      i++;
    size_t keyword_len = local_decl_keyword_len(src + word, i - word);
    if (!keyword_len) continue;
    size_t name_start = 0, name_end = 0;
    skim_parse_identifier(src, len, i, &name_start, &name_end);
    if (name_end == name_start) continue;
    if (decl_index->count == decl_index->cap) {
      size_t cap = decl_index->cap ? decl_index->cap * 2 : 128;
      local_decl_t *next = realloc(decl_index->items, sizeof(*next) * cap);
      if (!next) skim_die("out of memory");
      decl_index->items = next;
      decl_index->cap = cap;
    }
    decl_index->items[decl_index->count++] =
      (local_decl_t){.pos = word, .name_start = name_start, .name_end = name_end};
  }
}

bool skim_ts_local_value_decl_before(const char *src, size_t len, size_t pos, const char *name) {
  skim_local_decl_index_t *decl_index = &skim_run->ts.local_decls;
  if (!skim_scan_cache_owns(src)) local_decls_for_source(src, len);
  else if (decl_index->src != src) local_decls_for_source(src, skim_scan_cache_len());
  size_t name_len = strlen(name);
  for (size_t i = 0; i < decl_index->count && decl_index->items[i].pos < pos; i++) {
    const local_decl_t *decl = &decl_index->items[i];
    if (decl->name_end > len || decl->name_end - decl->name_start != name_len) continue;
    if (memcmp(src + decl->name_start, name, name_len) == 0) return true;
  }
  return false;
}
