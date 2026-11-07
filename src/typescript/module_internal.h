#ifndef SKIM_TS_MODULE_INTERNAL_H
#define SKIM_TS_MODULE_INTERNAL_H

#include "chars.h"

typedef struct {
  char *local;
  size_t local_len;
  size_t spec_start;
  size_t spec_end;
  bool type_use;
  bool value_use;
  bool later_decl;
} import_binding_t;

static inline size_t trim_left(const char *src, size_t i, size_t end) {
  while (i < end && (src[i] == ' ' || src[i] == '\t' || src[i] == '\n' || src[i] == '\r'))
    i++;
  return i;
}

static inline size_t trim_right(const char *src, size_t start, size_t end) {
  while (end > start && (src[end - 1] == ' ' || src[end - 1] == '\t' || src[end - 1] == '\n' || src[end - 1] == '\r'))
    end--;
  return end;
}

static inline bool word_before_is(const char *src, size_t pos, const char *word) {
  while (pos > 0 && (src[pos - 1] == ' ' || src[pos - 1] == '\t' || src[pos - 1] == '\n' || src[pos - 1] == '\r'))
    pos--;
  size_t end = pos;
  while (pos > 0 && skim_is_id_part(src[pos - 1]))
    pos--;
  return end > pos && strlen(word) == end - pos && memcmp(src + pos, word, end - pos) == 0;
}

static inline char prev_non_ws(const char *src, size_t pos) {
  while (pos > 0 && (src[pos - 1] == ' ' || src[pos - 1] == '\t' || src[pos - 1] == '\n' || src[pos - 1] == '\r'))
    pos--;
  return pos > 0 ? src[pos - 1] : '\0';
}

static inline size_t prev_non_ws_pos(const char *src, size_t pos) {
  while (pos > 0 && (src[pos - 1] == ' ' || src[pos - 1] == '\t' || src[pos - 1] == '\n' || src[pos - 1] == '\r'))
    pos--;
  return pos > 0 ? pos - 1 : (size_t)-1;
}

size_t skim_module_import_statement_end(const char *src, size_t len, size_t i);
size_t skim_module_export_type_statement_end(const char *src, size_t len, size_t i);
bool skim_module_statement_keyword_can_start_here(const char *src, size_t pos);
bool skim_module_type_statement_keyword_at(const char *src, size_t len, size_t pos);
bool skim_module_local_name_span(const char *src, size_t start, size_t end, size_t *name_start, size_t *name_end);
char *skim_module_local_name_dup(const char *src, size_t start, size_t end);
bool skim_module_specifier_is_explicit_type(const char *src, size_t start, size_t end);

bool skim_module_typeof_before_is_type_query(const char *src, size_t pos);
bool skim_module_occurrence_is_type_position(const char *src, size_t len, size_t pos);
void skim_module_free_import_bindings(import_binding_t *bindings, size_t count);
bool skim_module_push_import_binding(
  import_binding_t **bindings,
  size_t *count,
  size_t *cap,
  char *local,
  size_t spec_start,
  size_t spec_end
);
void skim_module_analyze_import_bindings(
  const char *src,
  size_t len,
  size_t start,
  import_binding_t *bindings,
  size_t count
);
bool skim_module_import_binding_should_keep(const import_binding_t *binding, const char *src, size_t import_pos);
bool skim_module_import_local_should_keep(
  const char *src,
  size_t len,
  size_t start,
  const char *local,
  size_t import_pos
);

#endif
