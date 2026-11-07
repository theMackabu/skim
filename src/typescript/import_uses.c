#include "chars.h"
#include "decl.h"
#include "module_internal.h"
#include "scan_cache.h"
#include "skip.h"
#include "str.h"
#include "type_syntax.h"
#include "typescript.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static bool colon_looks_like_value_object_property(const char *src, size_t colon) {
  if (prev_non_ws(src, colon) == ')') return false;

  int paren = 0, bracket = 0, brace = 0;
  for (size_t k = colon; k > 0;) {
    char c = src[--k];
    if (!skim_char_has(c, SKIM_CC_BACK_STOP)) continue;
    if ((c == ')' || c == ']' || c == '}') && skim_back_skip_block(src, &k)) continue;
    if (c == ')') paren++;
    else if (c == '(') {
      if (paren > 0) paren--;
      else if (bracket == 0 && brace == 0) return false;
    } else if (c == ']') bracket++;
    else if (c == '[') {
      if (bracket > 0) bracket--;
    } else if (c == '}') brace++;
    else if (c == '{') {
      if (brace > 0) {
        brace--;
      } else if (paren == 0 && bracket == 0) {
        size_t before = prev_non_ws_pos(src, k);
        if (before == (size_t)-1) return false;

        char prev = src[before];
        if (prev == ':' && colon_looks_like_value_object_property(src, before)) return true;
        return prev == '=' || prev == '(' || prev == ',' || prev == '[' || prev == '?' ||
               word_before_is(src, before + 1, "return") || word_before_is(src, before + 1, "yield");
      }
    } else if (c == ';' && paren == 0 && bracket == 0 && brace == 0) {
      return false;
    }
  }
  return false;
}

static bool colon_looks_like_statement_label(const char *src, size_t len, size_t colon) {
  size_t k = colon;
  while (k > 0 && (src[k - 1] == ' ' || src[k - 1] == '\t'))
    k--;
  size_t start = k;
  while (start > 0 && skim_is_id_part(src[start - 1]))
    start--;
  if (start == k) return false;

  size_t line = start;
  while (line > 0 && (src[line - 1] == ' ' || src[line - 1] == '\t'))
    line--;
  if (
    line > 0 && src[line - 1] != '\n' && src[line - 1] != '\r' && src[line - 1] != '{' && src[line - 1] != '}' &&
    src[line - 1] != ':'
  )
    return false;

  size_t before_line = line;
  while (before_line > 0 && (src[before_line - 1] == ' ' || src[before_line - 1] == '\t' ||
                             src[before_line - 1] == '\n' || src[before_line - 1] == '\r'))
    before_line--;
  if (before_line > 0 && (src[before_line - 1] == '(' || src[before_line - 1] == ',')) return false;

  size_t next = skim_skip_ws(src, len, colon + 1);
  return next >= len || src[next] == '{' || skim_word_at(src, len, next, "while") ||
         skim_word_at(src, len, next, "for") || skim_word_at(src, len, next, "if") ||
         skim_word_at(src, len, next, "switch") || skim_word_at(src, len, next, "do") ||
         skim_word_at(src, len, next, "try") || skim_word_at(src, len, next, "var") ||
         skim_word_at(src, len, next, "let") || skim_word_at(src, len, next, "const") ||
         skim_word_at(src, len, next, "typeof") || skim_word_at(src, len, next, "break") ||
         skim_word_at(src, len, next, "continue");
}

bool skim_module_typeof_before_is_type_query(const char *src, size_t pos) {
  while (pos > 0 && (src[pos - 1] == ' ' || src[pos - 1] == '\t' || src[pos - 1] == '\n' || src[pos - 1] == '\r'))
    pos--;
  size_t end = pos;
  while (pos > 0 && skim_is_id_part(src[pos - 1]))
    pos--;
  if (end - pos != 6 || memcmp(src + pos, "typeof", 6) != 0) return false;
  char prev = prev_non_ws(src, pos);
  return prev == ':' || prev == '|' || prev == '&' || prev == '<';
}

bool skim_module_occurrence_is_type_position(const char *src, size_t len, size_t pos) {
  if (
    word_before_is(src, pos, "as") || word_before_is(src, pos, "satisfies") || word_before_is(src, pos, "type") ||
    word_before_is(src, pos, "interface") || skim_module_typeof_before_is_type_query(src, pos)
  )
    return true;
  size_t prev_pos = prev_non_ws_pos(src, pos);
  char prev = prev_pos == (size_t)-1 ? '\0' : src[prev_pos];
  if (
    prev == ':' &&
    (skim_colon_looks_like_ternary(src, prev_pos) || skim_colon_looks_like_case_label(src, prev_pos) ||
     colon_looks_like_statement_label(src, len, prev_pos) || colon_looks_like_value_object_property(src, prev_pos))
  )
    return false;
  return prev == ':' || prev == '|' || prev == '&' || prev == '<';
}

typedef struct {
  import_binding_t *items;
  size_t count;
  uint64_t lengths;
  uint32_t *slots;
  size_t mask;
} import_binding_set_t;

static inline uint32_t binding_name_hash(const char *name, size_t len) {
  uint64_t head = 0, tail = 0;
  if (len >= 8) {
    memcpy(&head, name, 8);
    memcpy(&tail, name + len - 8, 8);
  } else if (len >= 4) {
    uint32_t a, b;
    memcpy(&a, name, 4);
    memcpy(&b, name + len - 4, 4);
    head = a;
    tail = b;
  } else if (len > 0) {
    head = (uint64_t)(unsigned char)name[0] | (uint64_t)(unsigned char)name[len / 2] << 8 |
           (uint64_t)(unsigned char)name[len - 1] << 16;
  }
  uint64_t h = (head ^ 0x9e3779b97f4a7c15ull) * 0xff51afd7ed558ccdull;
  h = (h ^ (h >> 29) ^ tail ^ len) * 0xc4ceb9fe1a85ec53ull;
  return (uint32_t)(h ^ (h >> 32));
}

static import_binding_set_t binding_set_init(import_binding_t *items, size_t count) {
  enum { MIN_HASHED_BINDINGS = 8 };
  import_binding_set_t set = {.items = items, .count = count};
  for (size_t i = 0; i < count; i++)
    set.lengths |= 1ull << (items[i].local_len & 63);
  if (count < MIN_HASHED_BINDINGS) return set;
  size_t cap = 16;
  while (cap < count * 2)
    cap *= 2;
  set.slots = calloc(cap, sizeof(*set.slots));
  if (!set.slots) skim_die("out of memory");
  set.mask = cap - 1;
  for (size_t i = 0; i < count; i++) {
    size_t slot = binding_name_hash(items[i].local, items[i].local_len) & set.mask;
    while (set.slots[slot]) {
      import_binding_t *other = &items[set.slots[slot] - 1];
      if (other->local_len == items[i].local_len && memcmp(other->local, items[i].local, other->local_len) == 0) break;
      slot = (slot + 1) & set.mask;
    }
    if (!set.slots[slot]) set.slots[slot] = (uint32_t)i + 1;
  }
  return set;
}

static void binding_set_free(import_binding_set_t *set) {
  free(set->slots);
}

void skim_module_free_import_bindings(import_binding_t *bindings, size_t count) {
  for (size_t i = 0; i < count; i++)
    free(bindings[i].local);
  free(bindings);
}

bool skim_module_push_import_binding(
  import_binding_t **bindings,
  size_t *count,
  size_t *cap,
  char *local,
  size_t spec_start,
  size_t spec_end
) {
  if (!local) return false;
  if (*count == *cap) {
    size_t next_cap = *cap ? *cap * 2 : 32;
    import_binding_t *next = realloc(*bindings, sizeof(*next) * next_cap);
    if (!next) skim_die("out of memory");
    *bindings = next;
    *cap = next_cap;
  }
  (*bindings)[*count] = (import_binding_t){
    .local = local,
    .local_len = strlen(local),
    .spec_start = spec_start,
    .spec_end = spec_end,
  };
  (*count)++;
  return true;
}

static inline import_binding_t *
find_import_binding(const import_binding_set_t *set, const char *name, size_t name_len) {
  if (!(set->lengths & (1ull << (name_len & 63)))) return NULL;
  if (set->slots) {
    size_t slot = binding_name_hash(name, name_len) & set->mask;
    while (set->slots[slot]) {
      import_binding_t *binding = &set->items[set->slots[slot] - 1];
      if (binding->local_len == name_len && memcmp(binding->local, name, name_len) == 0) return binding;
      slot = (slot + 1) & set->mask;
    }
    return NULL;
  }
  for (size_t i = 0; i < set->count; i++) {
    import_binding_t *binding = &set->items[i];
    if (binding->local_len == name_len && memcmp(binding->local, name, name_len) == 0) return binding;
  }
  return NULL;
}

typedef enum {
  IMPORT_USE_DECL,
  IMPORT_USE_TYPE,
  IMPORT_USE_OCCURRENCE,
} import_use_kind_t;

typedef enum {
  IMPORT_POSITION_UNKNOWN,
  IMPORT_POSITION_TYPE,
  IMPORT_POSITION_VALUE,
} import_position_t;

typedef struct {
  uint32_t scan_pos;
  uint32_t prev_plus_one;
  uint8_t kind;
  uint8_t position;
} import_use_t;

typedef struct {
  uint32_t name;
  uint32_t name_len;
  uint32_t last_use_plus_one;
} import_ident_t;

static struct {
  const char *src;
  size_t len;
  size_t path_start;
  uint64_t *on_path;
  import_ident_t *idents;
  size_t ident_count;
  size_t ident_mask;
  import_use_t *uses;
  size_t use_count;
  size_t use_cap;
} import_index;

void skim_ts_module_index_invalidate(void) {
  free(import_index.on_path);
  free(import_index.idents);
  free(import_index.uses);
  memset(&import_index, 0, sizeof(import_index));
}

static import_ident_t *import_index_slot(const char *src, const char *name, size_t name_len) {
  size_t slot = binding_name_hash(name, name_len) & import_index.ident_mask;
  for (;;) {
    import_ident_t *ident = &import_index.idents[slot];
    if (ident->name_len == 0) return ident;
    if (ident->name_len == name_len && memcmp(src + ident->name, name, name_len) == 0) return ident;
    slot = (slot + 1) & import_index.ident_mask;
  }
}

static void import_index_grow_idents(const char *src) {
  import_ident_t *old = import_index.idents;
  size_t old_cap = import_index.ident_mask + 1;
  import_index.idents = calloc(old_cap * 2, sizeof(*old));
  if (!import_index.idents) skim_die("out of memory");
  import_index.ident_mask = old_cap * 2 - 1;
  for (size_t i = 0; i < old_cap; i++) {
    if (old[i].name_len) *import_index_slot(src, src + old[i].name, old[i].name_len) = old[i];
  }
  free(old);
}

static void
import_index_record(const char *src, import_use_kind_t kind, size_t scan_pos, size_t name_start, size_t name_len) {
  if (name_len == 0) return;
  if ((import_index.ident_count + 1) * 4 > (import_index.ident_mask + 1) * 3) import_index_grow_idents(src);
  import_ident_t *ident = import_index_slot(src, src + name_start, name_len);
  if (ident->name_len == 0) {
    ident->name = (uint32_t)name_start;
    ident->name_len = (uint32_t)name_len;
    import_index.ident_count++;
  }
  if (import_index.use_count == import_index.use_cap) {
    size_t cap = import_index.use_cap ? import_index.use_cap * 2 : 1024;
    import_use_t *next = realloc(import_index.uses, sizeof(*next) * cap);
    if (!next) skim_die("out of memory");
    import_index.uses = next;
    import_index.use_cap = cap;
  }
  import_index.uses[import_index.use_count++] = (import_use_t){
    .scan_pos = (uint32_t)scan_pos,
    .prev_plus_one = ident->last_use_plus_one,
    .kind = (uint8_t)kind,
  };
  ident->last_use_plus_one = (uint32_t)import_index.use_count;
}

typedef struct {
  const import_binding_set_t *set;
  bool record;
  bool stop_on_recorded_path;
} import_scan_t;

static inline void import_scan_mark(
  const import_scan_t *scan,
  const char *src,
  size_t len,
  import_use_kind_t kind,
  size_t scan_pos,
  size_t name_start,
  size_t name_len
) {
  if (scan->record) {
    import_index_record(src, kind, scan_pos, name_start, name_len);
    return;
  }
  import_binding_t *binding = find_import_binding(scan->set, src + name_start, name_len);
  if (!binding) return;
  if (kind == IMPORT_USE_DECL) binding->later_decl = true;
  else if (kind == IMPORT_USE_TYPE || skim_module_occurrence_is_type_position(src, len, name_start))
    binding->type_use = true;
  else binding->value_use = true;
}

static void mark_bindings_in_type_range(
  const import_scan_t *scan,
  const char *src,
  size_t len,
  size_t scan_pos,
  size_t start,
  size_t end
) {
  for (size_t i = start; i < end && i < len;) {
    if (src[i] == '\'' || src[i] == '"' || src[i] == '`') {
      i = skim_skip_string_raw(src, len, i);
      continue;
    }
    if (skim_skip_comment(src, len, &i)) continue;
    if (skim_is_id_start(src[i])) {
      size_t ident_start = i++;
      while (i < end && i < len && skim_is_id_part(src[i]))
        i++;
      import_scan_mark(scan, src, len, IMPORT_USE_TYPE, scan_pos, ident_start, i - ident_start);
      continue;
    }
    i++;
  }
}

static void mark_import_specifier_list(
  const import_scan_t *scan,
  const char *src,
  size_t len,
  size_t scan_pos,
  size_t brace,
  size_t close
) {
  size_t item_start = brace + 1;
  for (size_t p = brace + 1; p <= close - 1; p++) {
    if (p < close - 1 && src[p] != ',') continue;
    size_t a = trim_left(src, item_start, p);
    size_t b = trim_right(src, a, p);
    if (a < b && !skim_module_specifier_is_explicit_type(src, a, b)) {
      size_t name_start = 0, name_end = 0;
      if (skim_module_local_name_span(src, a, b, &name_start, &name_end))
        import_scan_mark(scan, src, len, IMPORT_USE_DECL, scan_pos, name_start, name_end - name_start);
    }
    item_start = p + 1;
  }
}

static size_t mark_import_decl_bindings(const import_scan_t *scan, const char *src, size_t len, size_t i) {
  size_t j = skim_skip_ws_comments(src, len, i + 6);
  if (skim_word_at(src, len, j, "type")) return skim_module_import_statement_end(src, len, i);

  if (j < len && src[j] == '{') {
    size_t close = skim_skip_balanced(src, len, j, '{', '}');
    mark_import_specifier_list(scan, src, len, i, j, close);
    return skim_module_import_statement_end(src, len, i);
  }

  if (j < len && src[j] == '*') {
    size_t as_pos = skim_skip_ws_comments(src, len, j + 1);
    if (skim_word_at(src, len, as_pos, "as")) {
      size_t name_start = 0, name_end = 0;
      skim_parse_identifier(src, len, as_pos + 2, &name_start, &name_end);
      if (name_start != name_end) {
        import_scan_mark(scan, src, len, IMPORT_USE_DECL, i, name_start, name_end - name_start);
      }
    }
    return skim_module_import_statement_end(src, len, i);
  }

  size_t name_start = 0, name_end = 0;
  size_t after_name = skim_parse_identifier(src, len, j, &name_start, &name_end);
  if (name_start != name_end) import_scan_mark(scan, src, len, IMPORT_USE_DECL, i, name_start, name_end - name_start);

  size_t comma = skim_skip_ws_comments(src, len, after_name);
  if (comma < len && src[comma] == ',') {
    size_t brace = skim_skip_ws_comments(src, len, comma + 1);
    if (brace < len && src[brace] == '{') {
      size_t close = skim_skip_balanced(src, len, brace, '{', '}');
      mark_import_specifier_list(scan, src, len, i, brace, close);
      return skim_module_import_statement_end(src, len, i);
    }
  }
  return skim_module_import_statement_end(src, len, i);
}

static bool value_decl_keyword_at(const char *src, size_t len, size_t i, size_t ident_len, size_t *keyword_len) {
  const char *word = NULL;
  switch (ident_len) {
  case 3:
    word = src[i] == 'l' ? "let" : src[i] == 'v' ? "var" : NULL;
    break;
  case 4:
    word = src[i] == 'e' ? "enum" : NULL;
    break;
  case 5:
    word = src[i] == 'c' ? (src[i + 1] == 'o' ? "const" : "class") : NULL;
    break;
  case 8:
    word = src[i] == 'f' ? "function" : NULL;
    break;
  case 9:
    word = src[i] == 'n' ? "namespace" : NULL;
    break;
  default:
    break;
  }
  if (!word || !skim_word_at(src, len, i, word)) return false;
  *keyword_len = ident_len;
  return true;
}

static inline bool import_index_on_path(size_t pos) {
  return pos >= import_index.path_start && (import_index.on_path[pos / 64] >> (pos % 64)) & 1;
}

static size_t scan_import_uses(const import_scan_t *scan, const char *src, size_t len, size_t start) {
  for (size_t i = start; i < len;) {
    if (scan->stop_on_recorded_path && import_index_on_path(i)) return i;
    if (scan->record) import_index.on_path[i / 64] |= 1ull << (i % 64);
    else if (!scan->stop_on_recorded_path && !skim_char_has(src[i], SKIM_CC_ID_START | SKIM_CC_MARKER)) {
      i++;
      continue;
    }
    if (src[i] == '\'' || src[i] == '"' || src[i] == '`') {
      i = skim_skip_string_raw(src, len, i);
      continue;
    }
    if (skim_skip_comment(src, len, &i)) continue;
    if (!skim_is_id_start(src[i])) {
      i++;
      continue;
    }

    size_t ident_start = i;
    i++;
    while (i < len && skim_is_id_part(src[i]))
      i++;
    size_t ident_end = i;
    size_t ident_len = ident_end - ident_start;

    if (ident_len == 6 && memcmp(src + ident_start, "import", 6) == 0) {
      i = mark_import_decl_bindings(scan, src, len, ident_start);
      continue;
    }

    if (
      ident_len == 6 && memcmp(src + ident_start, "export", 6) == 0 &&
      skim_module_statement_keyword_can_start_here(src, ident_start)
    ) {
      size_t type_pos = skim_skip_ws_comments(src, len, ident_end);
      if (skim_module_type_statement_keyword_at(src, len, type_pos)) {
        size_t end = skim_module_export_type_statement_end(src, len, type_pos);
        mark_bindings_in_type_range(scan, src, len, ident_start, type_pos, end);
        i = end;
        continue;
      }
    }

    if (
      ((ident_len == 4 && memcmp(src + ident_start, "type", 4) == 0) ||
       (ident_len == 9 && memcmp(src + ident_start, "interface", 9) == 0)) &&
      skim_module_type_statement_keyword_at(src, len, ident_start)
    ) {
      size_t end = skim_module_export_type_statement_end(src, len, ident_start);
      mark_bindings_in_type_range(scan, src, len, ident_start, ident_start, end);
      i = end;
      continue;
    }

    size_t keyword_len = 0;
    if (value_decl_keyword_at(src, len, ident_start, ident_len, &keyword_len)) {
      size_t name_start = 0, name_end = 0;
      skim_parse_identifier(src, len, ident_start + keyword_len, &name_start, &name_end);
      if (name_start != name_end) {
        import_scan_mark(scan, src, len, IMPORT_USE_DECL, ident_start, name_start, name_end - name_start);
        i = name_end;
        continue;
      }
    }

    import_scan_mark(scan, src, len, IMPORT_USE_OCCURRENCE, ident_start, ident_start, ident_len);
  }
  return len;
}

static void import_index_build(const char *src, size_t len, size_t start) {
  enum { INITIAL_IDENT_SLOTS = 1024 };
  import_index.path_start = start;
  import_index.on_path = calloc(len / 64 + 1, sizeof(*import_index.on_path));
  import_index.idents = calloc(INITIAL_IDENT_SLOTS, sizeof(*import_index.idents));
  if (!import_index.on_path || !import_index.idents) skim_die("out of memory");
  import_index.ident_mask = INITIAL_IDENT_SLOTS - 1;
  import_scan_t scan = {.record = true};
  scan_import_uses(&scan, src, len, start);
}

static void apply_recorded_import_uses(const import_binding_set_t *set, const char *src, size_t len, size_t join_pos) {
  for (size_t b = 0; b < set->count; b++) {
    import_binding_t *binding = &set->items[b];
    if (find_import_binding(set, binding->local, binding->local_len) != binding) continue;
    const import_ident_t *ident = import_index_slot(src, binding->local, binding->local_len);
    for (uint32_t use_id = ident->last_use_plus_one; use_id;) {
      import_use_t *use = &import_index.uses[use_id - 1];
      if (use->scan_pos < join_pos) break;
      if (use->kind == IMPORT_USE_DECL) binding->later_decl = true;
      else if (use->kind == IMPORT_USE_TYPE) binding->type_use = true;
      else {
        if (use->position == IMPORT_POSITION_UNKNOWN) {
          bool is_type = skim_module_occurrence_is_type_position(src, len, use->scan_pos);
          use->position = is_type ? IMPORT_POSITION_TYPE : IMPORT_POSITION_VALUE;
        }
        if (use->position == IMPORT_POSITION_TYPE) binding->type_use = true;
        else binding->value_use = true;
      }
      use_id = use->prev_plus_one;
    }
  }
}

void skim_module_analyze_import_bindings(
  const char *src,
  size_t len,
  size_t start,
  import_binding_t *bindings,
  size_t count
) {
  import_binding_set_t binding_set = binding_set_init(bindings, count);
  import_scan_t scan = {.set = &binding_set};
  bool indexable = skim_scan_cache_owns(src) && len <= UINT32_MAX;
  if (!indexable) {
    scan_import_uses(&scan, src, len, start);
  } else {
    bool first_query = import_index.src != src || import_index.len != len;
    if (first_query) {
      skim_ts_module_index_invalidate();
      import_index.src = src;
      import_index.len = len;
    }
    if (first_query && !skim_word_at(src, len, skim_skip_ws_comments(src, len, start), "import")) {
      scan_import_uses(&scan, src, len, start);
    } else {
      if (!import_index.on_path) import_index_build(src, len, start);
      scan.stop_on_recorded_path = true;
      size_t join_pos = scan_import_uses(&scan, src, len, start);
      if (join_pos < len) apply_recorded_import_uses(&binding_set, src, len, join_pos);
    }
  }
  binding_set_free(&binding_set);
}

bool skim_module_import_binding_should_keep(const import_binding_t *binding, const char *src, size_t import_pos) {
  if (binding->later_decl || skim_decl_seen_before(src, binding->local, import_pos, SKIM_DECL_VALUE)) return false;
  if (binding->value_use) return true;
  if (binding->type_use) return false;
  return true;
}

bool skim_module_import_local_should_keep(
  const char *src,
  size_t len,
  size_t start,
  const char *local,
  size_t import_pos
) {
  import_binding_t binding = {
    .local = (char *)local,
    .local_len = strlen(local),
  };
  skim_module_analyze_import_bindings(src, len, start, &binding, 1);
  return skim_module_import_binding_should_keep(&binding, src, import_pos);
}
