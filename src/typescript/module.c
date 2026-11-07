#include "skim_internal.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

static size_t export_type_statement_end(const char *src, size_t len, size_t i);

static size_t import_statement_end(const char *src, size_t len, size_t i) {
  int paren = 0, bracket = 0, brace = 0;
  bool next_string_is_module_specifier = false;
  bool saw_module_specifier = false;
  while (i < len) {
    char c = src[i];
    if (c == '\'' || c == '"' || c == '`') {
      i = skim_skip_string_raw(src, len, i);
      if (next_string_is_module_specifier) {
        saw_module_specifier = true;
        next_string_is_module_specifier = false;
      }
      continue;
    }
    if (i + 1 < len && c == '/' && src[i + 1] == '/') {
      if (paren == 0 && bracket == 0 && brace == 0 && saw_module_specifier) return i;
      i += 2;
      while (i < len && src[i] != '\n' && src[i] != '\r')
        i++;
      continue;
    }
    if (i + 1 < len && c == '/' && src[i + 1] == '*') {
      i += 2;
      while (i + 1 < len && !(src[i] == '*' && src[i + 1] == '/'))
        i++;
      if (i + 1 < len) i += 2;
      continue;
    }
    if (paren == 0 && bracket == 0 && brace == 0) {
      if (skim_word_at(src, len, i, "import")) {
        size_t next = skim_skip_ws_comments(src, len, i + 6);
        if (next < len && (src[next] == '\'' || src[next] == '"' || src[next] == '`')) {
          next_string_is_module_specifier = true;
        }
        i += 6;
        continue;
      }
      if (skim_word_at(src, len, i, "from")) {
        next_string_is_module_specifier = true;
        i += 4;
        continue;
      }
    }
    if (c == '(') paren++;
    else if (c == ')' && paren > 0) paren--;
    else if (c == '[') bracket++;
    else if (c == ']' && bracket > 0) bracket--;
    else if (c == '{') brace++;
    else if (c == '}' && brace > 0) brace--;
    else if (c == ';' && paren == 0 && bracket == 0 && brace == 0) return i + 1;
    else if (
      (c == '\n' || c == '\r') && paren == 0 && bracket == 0 && brace == 0 && saw_module_specifier
    )
      return i;
    i++;
  }
  return i;
}

static size_t trim_left(const char *src, size_t i, size_t end) {
  while (i < end && (src[i] == ' ' || src[i] == '\t' || src[i] == '\n' || src[i] == '\r'))
    i++;
  return i;
}

static size_t trim_right(const char *src, size_t start, size_t end) {
  while (end > start && (src[end - 1] == ' ' || src[end - 1] == '\t' || src[end - 1] == '\n' || src[end - 1] == '\r'))
    end--;
  return end;
}

static bool word_before_is(const char *src, size_t pos, const char *word) {
  while (pos > 0 && (src[pos - 1] == ' ' || src[pos - 1] == '\t' || src[pos - 1] == '\n' || src[pos - 1] == '\r'))
    pos--;
  size_t end = pos;
  while (pos > 0 && skim_is_id_part(src[pos - 1]))
    pos--;
  return end > pos && strlen(word) == end - pos && memcmp(src + pos, word, end - pos) == 0;
}

static char prev_non_ws(const char *src, size_t pos) {
  while (pos > 0 && (src[pos - 1] == ' ' || src[pos - 1] == '\t' || src[pos - 1] == '\n' || src[pos - 1] == '\r'))
    pos--;
  return pos > 0 ? src[pos - 1] : '\0';
}

static size_t prev_non_ws_pos(const char *src, size_t pos) {
  while (pos > 0 && (src[pos - 1] == ' ' || src[pos - 1] == '\t' || src[pos - 1] == '\n' || src[pos - 1] == '\r'))
    pos--;
  return pos > 0 ? pos - 1 : (size_t)-1;
}

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

static bool typeof_before_is_type_query(const char *src, size_t pos) {
  while (pos > 0 && (src[pos - 1] == ' ' || src[pos - 1] == '\t' || src[pos - 1] == '\n' || src[pos - 1] == '\r'))
    pos--;
  size_t end = pos;
  while (pos > 0 && skim_is_id_part(src[pos - 1]))
    pos--;
  if (end - pos != 6 || memcmp(src + pos, "typeof", 6) != 0) return false;
  char prev = prev_non_ws(src, pos);
  return prev == ':' || prev == '|' || prev == '&' || prev == '<';
}

static bool occurrence_is_type_position(const char *src, size_t len, size_t pos) {
  if (
    word_before_is(src, pos, "as") || word_before_is(src, pos, "satisfies") || word_before_is(src, pos, "type") ||
    word_before_is(src, pos, "interface") || typeof_before_is_type_query(src, pos)
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

static bool statement_keyword_can_start_here(const char *src, size_t pos) {
  size_t line = pos;
  while (line > 0 && src[line - 1] != '\n' && src[line - 1] != '\r')
    line--;
  size_t first = line;
  while (first < pos && (src[first] == ' ' || src[first] == '\t'))
    first++;
  if (first == pos) return true;

  char prev = prev_non_ws(src, pos);
  return prev == '\0' || prev == ';' || prev == '{' || prev == '}';
}

static bool type_statement_keyword_at(const char *src, size_t len, size_t pos) {
  if (!statement_keyword_can_start_here(src, pos) && !word_before_is(src, pos, "export")) return false;
  if (skim_word_at(src, len, pos, "type")) {
    size_t name_start = 0, name_end = 0;
    size_t after_name = skim_parse_identifier(src, len, skim_skip_ws_comments(src, len, pos + 4), &name_start, &name_end);
    size_t eq = skim_skip_ws_comments(src, len, after_name);
    return name_start != name_end && eq < len && src[eq] == '=';
  }
  if (skim_word_at(src, len, pos, "interface")) {
    size_t name_start = 0, name_end = 0;
    skim_parse_identifier(src, len, skim_skip_ws_comments(src, len, pos + 9), &name_start, &name_end);
    return name_start != name_end;
  }
  return false;
}

static bool
local_name_span_from_specifier(const char *src, size_t start, size_t end, size_t *name_start, size_t *name_end) {
  start = trim_left(src, start, end);
  end = trim_right(src, start, end);
  if (skim_word_at(src, end, start, "type")) start = trim_left(src, start + 4, end);
  *name_start = *name_end = 0;
  for (size_t as_pos = start; as_pos < end; as_pos++) {
    if (skim_word_at(src, end, as_pos, "as")) {
      skim_parse_identifier(src, end, as_pos + 2, name_start, name_end);
      if (*name_start != *name_end) return true;
    }
  }
  skim_parse_identifier(src, end, start, name_start, name_end);
  return *name_start != *name_end;
}

static char *local_name_from_specifier(const char *src, size_t start, size_t end) {
  size_t name_start = 0, name_end = 0;
  if (!local_name_span_from_specifier(src, start, end, &name_start, &name_end)) return NULL;
  return skim_slice_dup(src, name_start, name_end);
}

static bool specifier_is_explicit_type(const char *src, size_t start, size_t end) {
  start = trim_left(src, start, end);
  return skim_word_at(src, end, start, "type");
}

static char *exported_binding_from_specifier(const char *src, size_t start, size_t end) {
  start = trim_left(src, start, end);
  if (skim_word_at(src, end, start, "type")) start = trim_left(src, start + 4, end);
  size_t name_start = 0, name_end = 0;
  skim_parse_identifier(src, end, start, &name_start, &name_end);
  if (name_start == name_end) return NULL;
  return skim_slice_dup(src, name_start, name_end);
}

static bool
specifier_list_declares(const char *src, size_t brace, size_t close, const char *name, size_t n, bool type_only) {
  size_t item_start = brace + 1;
  for (size_t p = brace + 1; p <= close - 1; p++) {
    if (p < close - 1 && src[p] != ',') continue;
    if (!type_only || specifier_is_explicit_type(src, item_start, p)) {
      size_t name_start = 0, name_end = 0;
      if (
        local_name_span_from_specifier(src, item_start, p, &name_start, &name_end) && name_end - name_start == n &&
        memcmp(src + name_start, name, n) == 0
      )
        return true;
    }
    item_start = p + 1;
  }
  return false;
}

static bool has_type_declaration(const char *src, size_t len, const char *name) {
  size_t n = strlen(name);
  for (size_t i = 0; i + n < len; i++) {
    if (src[i] != 't' && src[i] != 'i') continue;
    if ((skim_word_at(src, len, i, "type") || skim_word_at(src, len, i, "interface"))) {
      size_t j = skim_skip_ws_comments(src, len, i + (src[i] == 't' ? 4 : 9));
      if (j + n <= len && memcmp(src + j, name, n) == 0 && (j + n == len || !skim_is_id_part(src[j + n]))) return true;
    }
  }
  return false;
}

static bool has_type_only_import_declaration(const char *src, size_t len, const char *name) {
  size_t n = strlen(name);
  for (size_t i = 0; i + n < len; i++) {
    if (src[i] != 'i' || !skim_word_at(src, len, i, "import")) continue;
    size_t j = skim_skip_ws_comments(src, len, i + 6);
    bool whole_import_is_type = false;
    if (skim_word_at(src, len, j, "type")) {
      whole_import_is_type = true;
      j = skim_skip_ws_comments(src, len, j + 4);
    }
    if (j < len && src[j] == '{') {
      size_t close = skim_skip_balanced(src, len, j, '{', '}');
      if (specifier_list_declares(src, j, close, name, n, !whole_import_is_type)) return true;
    } else if (whole_import_is_type) {
      size_t name_start = 0, name_end = 0;
      skim_parse_identifier(src, len, j, &name_start, &name_end);
      if (name_end - name_start == n && memcmp(src + name_start, name, n) == 0) return true;
    }
  }
  return false;
}

static bool has_value_declaration(const char *src, size_t len, const char *name) {
  size_t n = strlen(name);
  static const char *decls[] = {"const", "let", "var", "function", "class", "enum", "namespace"};
  for (size_t i = 0; i + n < len; i++) {
    if (!skim_char_has(src[i], SKIM_CC_DECL_START)) continue;
    if (skim_word_at(src, len, i, "import")) {
      size_t j = skim_skip_ws_comments(src, len, i + 6);
      if (skim_word_at(src, len, j, "type")) continue;
      if (j < len && src[j] == '{') {
        size_t close = skim_skip_balanced(src, len, j, '{', '}');
        if (specifier_list_declares(src, j, close, name, n, false)) return true;
      } else {
        size_t name_start = 0, name_end = 0;
        size_t after_name = skim_parse_identifier(src, len, j, &name_start, &name_end);
        if (name_start != name_end && name_end - name_start == n && memcmp(src + name_start, name, n) == 0) return true;
        size_t comma = skim_skip_ws_comments(src, len, after_name);
        if (comma < len && src[comma] == ',') {
          size_t brace = skim_skip_ws_comments(src, len, comma + 1);
          if (brace < len && src[brace] == '{') {
            size_t close = skim_skip_balanced(src, len, brace, '{', '}');
            if (specifier_list_declares(src, brace, close, name, n, false)) return true;
          }
        }
      }
    }
    for (size_t d = 0; d < sizeof(decls) / sizeof(decls[0]); d++) {
      if (!skim_word_at(src, len, i, decls[d])) continue;
      size_t j = skim_skip_ws_comments(src, len, i + strlen(decls[d]));
      if (j + n <= len && memcmp(src + j, name, n) == 0 && (j + n == len || !skim_is_id_part(src[j + n]))) return true;
    }
  }
  return false;
}

typedef struct {
  char *local;
  size_t local_len;
  size_t spec_start;
  size_t spec_end;
  bool type_use;
  bool value_use;
  bool later_decl;
} import_binding_t;

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

static void free_import_bindings(import_binding_t *bindings, size_t count) {
  for (size_t i = 0; i < count; i++)
    free(bindings[i].local);
  free(bindings);
}

static bool push_import_binding(
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
  else if (kind == IMPORT_USE_TYPE || occurrence_is_type_position(src, len, name_start)) binding->type_use = true;
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
    if (a < b && !specifier_is_explicit_type(src, a, b)) {
      size_t name_start = 0, name_end = 0;
      if (local_name_span_from_specifier(src, a, b, &name_start, &name_end))
        import_scan_mark(scan, src, len, IMPORT_USE_DECL, scan_pos, name_start, name_end - name_start);
    }
    item_start = p + 1;
  }
}

static size_t mark_import_decl_bindings(const import_scan_t *scan, const char *src, size_t len, size_t i) {
  size_t j = skim_skip_ws_comments(src, len, i + 6);
  if (skim_word_at(src, len, j, "type")) return import_statement_end(src, len, i);

  if (j < len && src[j] == '{') {
    size_t close = skim_skip_balanced(src, len, j, '{', '}');
    mark_import_specifier_list(scan, src, len, i, j, close);
    return import_statement_end(src, len, i);
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
    return import_statement_end(src, len, i);
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
      return import_statement_end(src, len, i);
    }
  }
  return import_statement_end(src, len, i);
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
      statement_keyword_can_start_here(src, ident_start)
    ) {
      size_t type_pos = skim_skip_ws_comments(src, len, ident_end);
      if (type_statement_keyword_at(src, len, type_pos)) {
        size_t end = export_type_statement_end(src, len, type_pos);
        mark_bindings_in_type_range(scan, src, len, ident_start, type_pos, end);
        i = end;
        continue;
      }
    }

    if (
      ((ident_len == 4 && memcmp(src + ident_start, "type", 4) == 0) ||
       (ident_len == 9 && memcmp(src + ident_start, "interface", 9) == 0)) &&
      type_statement_keyword_at(src, len, ident_start)
    ) {
      size_t end = export_type_statement_end(src, len, ident_start);
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
          bool is_type = occurrence_is_type_position(src, len, use->scan_pos);
          use->position = is_type ? IMPORT_POSITION_TYPE : IMPORT_POSITION_VALUE;
        }
        if (use->position == IMPORT_POSITION_TYPE) binding->type_use = true;
        else binding->value_use = true;
      }
      use_id = use->prev_plus_one;
    }
  }
}

static void
analyze_import_bindings(const char *src, size_t len, size_t start, import_binding_t *bindings, size_t count) {
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

static bool import_binding_should_keep(const import_binding_t *binding, const char *src, size_t import_pos) {
  if (binding->later_decl || skim_decl_seen_before(src, binding->local, import_pos, SKIM_DECL_VALUE)) return false;
  if (binding->value_use) return true;
  if (binding->type_use) return false;
  return true;
}

static bool import_local_should_keep(const char *src, size_t len, size_t start, const char *local, size_t import_pos) {
  import_binding_t binding = {
    .local = (char *)local,
    .local_len = strlen(local),
  };
  analyze_import_bindings(src, len, start, &binding, 1);
  return import_binding_should_keep(&binding, src, import_pos);
}

static bool try_export_named(skim_str_t *out, const char *src, size_t len, size_t i, size_t brace, size_t *out_end) {
  size_t close = skim_skip_balanced(src, len, brace, '{', '}');
  if (close <= brace + 1 || close > len) return false;
  size_t after = skim_skip_ws_comments(src, len, close);
  size_t end = skim_skip_statement_like(src, len, after);
  bool linebreak_after_brace = false;
  for (size_t p = close; p < after; p++) {
    if (src[p] == '\n' || src[p] == '\r') {
      linebreak_after_brace = true;
      break;
    }
  }
  size_t from_start = 0, from_end = 0;
  if (skim_word_at(src, len, after, "from")) {
    from_start = skim_skip_ws_comments(src, len, after + 4);
    from_end = end;
    for (size_t p = from_start; p < end; p++) {
      if (src[p] == ';') break;
      if (src[p] == '\n' || src[p] == '\r') {
        end = p;
        from_end = p;
        break;
      }
    }
    if (from_end > from_start && src[from_end - 1] == ';') from_end--;
  } else if (after < len && src[after] != ';') {
    if (!linebreak_after_brace) return false;
    end = close;
  }

  skim_str_t kept = {0};
  size_t item_start = brace + 1;
  size_t kept_count = 0;
  size_t non_type_count = 0;
  size_t specifier_count = 0;
  for (size_t p = brace + 1; p <= close - 1; p++) {
    if (p < close - 1 && src[p] != ',') continue;
    size_t a = trim_left(src, item_start, p);
    size_t b = trim_right(src, a, p);
    if (a < b) specifier_count++;
    if (a < b && !specifier_is_explicit_type(src, a, b)) {
      non_type_count++;
      char *binding = exported_binding_from_specifier(src, a, b);
      bool has_local_value =
        binding && (skim_decl_seen_before(src, binding, i, SKIM_DECL_VALUE) || has_value_declaration(src, i, binding) ||
                    has_value_declaration(src + end, len - end, binding));
      bool has_local_type = binding && has_type_declaration(src, len, binding);
      bool has_type_import = binding && has_type_only_import_declaration(src, len, binding);
      bool keep = binding && (from_start || has_local_value || (!has_local_type && !has_type_import)) &&
                  (!has_local_type || has_value_declaration(src, len, binding));
      free(binding);
      if (keep) {
        if (kept_count++) skim_str_puts(&kept, ", ");
        skim_str_putn(&kept, src + a, b - a);
      }
    }
    item_start = p + 1;
  }

  if (kept_count > 0) {
    skim_str_puts(out, "export { ");
    skim_str_putn(out, kept.data, kept.len);
    skim_str_puts(out, " }");
    if (from_start) {
      skim_str_puts(out, " from ");
      skim_str_putn(out, src + from_start, from_end - from_start);
    }
    skim_str_puts(out, ";\n");
  } else {
    if (from_start) {
      if (non_type_count > 0 || specifier_count == 0) {
        skim_str_puts(out, "export {} from ");
        skim_str_putn(out, src + from_start, from_end - from_start);
        skim_str_puts(out, ";\n");
      } else {
        skim_emit_preserved_newlines(out, src, i, end);
      }
    } else if (close > brace + 2) {
      skim_emit_preserved_newlines(out, src, i, end);
    } else {
      skim_str_puts(out, "export {};\n");
    }
  }
  free(kept.data);
  *out_end = end;
  return true;
}

static bool export_default_function_overload_end(const char *src, size_t len, size_t i, size_t *out_end) {
  if (!skim_word_at(src, len, i, "export")) return false;
  size_t j = skim_skip_ws_comments(src, len, i + 6);
  if (!skim_word_at(src, len, j, "default")) return false;
  j = skim_skip_ws_comments(src, len, j + 7);
  if (!skim_word_at(src, len, j, "function")) return false;

  int paren = 0, bracket = 0, brace = 0, angle = 0;
  for (size_t p = j + 8; p < len; p++) {
    char c = src[p];
    if (c == '\'' || c == '"' || c == '`') {
      p = skim_skip_string_raw(src, len, p) - 1;
      continue;
    }
    if (c == '(') paren++;
    else if (c == ')' && paren > 0) paren--;
    else if (c == '[') bracket++;
    else if (c == ']' && bracket > 0) bracket--;
    else if (c == '<') angle++;
    else if (c == '>' && angle > 0) angle--;
    else if (c == '{' && paren == 0 && bracket == 0 && brace == 0 && angle == 0) return false;
    else if (c == ';' && paren == 0 && bracket == 0 && brace == 0 && angle == 0) {
      *out_end = p + 1;
      return true;
    } else if (c == '{') brace++;
    else if (c == '}' && brace > 0) brace--;
  }
  return false;
}

static size_t export_type_statement_end(const char *src, size_t len, size_t i) {
  size_t type = i;
  if (skim_word_at(src, len, type, "export")) type = skim_skip_ws_comments(src, len, type + 6);
  if (skim_word_at(src, len, type, "type")) {
    size_t name = skim_skip_ws_comments(src, len, type + 4);
    if (name < len && skim_is_id_start(src[name])) return skim_skip_type_alias(src, len, type);
  }
  int paren = 0, bracket = 0, brace = 0, angle = 0;
  bool seen_token = false;
  char last_sig = '\0';
  while (i < len) {
    char c = src[i];
    if ((c == '\n' || c == '\r') && paren == 0 && bracket == 0 && brace == 0 && angle == 0 && seen_token) {
      size_t next = skim_skip_ws_comments(src, len, i + 1);
      if (
        last_sig == '=' || last_sig == '|' || last_sig == '&' || last_sig == '?' || last_sig == ':' || last_sig == ','
      ) {
        i++;
        continue;
      }
      if (
        next < len && (src[next] == '|' || src[next] == '&' || src[next] == '{' || src[next] == '?' || src[next] == ':')
      ) {
        i = next;
        continue;
      }
      return i;
    }
    if (c == '\'' || c == '"' || c == '`') {
      seen_token = true;
      i = skim_skip_string_raw(src, len, i);
      continue;
    }
    if (i + 1 < len && c == '/' && src[i + 1] == '/') {
      while (i < len && src[i] != '\n' && src[i] != '\r')
        i++;
      continue;
    }
    if (i + 1 < len && c == '/' && src[i + 1] == '*') {
      i = skim_skip_block_comment(src, len, i + 2);
      continue;
    }
    if (skim_is_space(c)) {
      i++;
      continue;
    }
    if (c == '(') paren++;
    else if (c == ')' && paren > 0) paren--;
    else if (c == '[') bracket++;
    else if (c == ']' && bracket > 0) bracket--;
    else if (c == '{') brace++;
    else if (c == '}' && brace > 0) brace--;
    else if (c == '<') angle++;
    else if (c == '>' && angle > 0) angle--;
    else if (c == ';' && paren == 0 && bracket == 0 && brace == 0 && angle == 0) return i + 1;
    seen_token = true;
    last_sig = c;
    i++;
  }
  return i;
}

static size_t import_equals_end(const char *src, size_t len, size_t rhs) {
  size_t i = rhs;
  while (i < len) {
    if (src[i] == '\'' || src[i] == '"' || src[i] == '`') {
      i = skim_skip_string_raw(src, len, i);
      continue;
    }
    if (src[i] == ';') return i + 1;
    if (src[i] == '\n' || src[i] == '\r') return i;
    i++;
  }
  return i;
}

typedef struct {
  char *name;
  size_t stmt_start;
  size_t stmt_end;
  size_t rhs_start;
  size_t rhs_end;
  bool live;
} import_equals_decl_t;

static bool pos_in_import_equals_decl(import_equals_decl_t *decls, size_t count, size_t pos) {
  for (size_t i = 0; i < count; i++) {
    if (pos >= decls[i].stmt_start && pos < decls[i].stmt_end) return true;
  }
  return false;
}

static bool pos_in_import_equals_statement(const char *src, size_t len, size_t pos) {
  size_t start = pos;
  while (start > 0 && src[start - 1] != '\n' && src[start - 1] != '\r' && src[start - 1] != ';')
    start--;
  start = skim_skip_ws_comments(src, len, start);
  if (!skim_word_at(src, len, start, "import")) return false;
  for (size_t i = start + 6; i < pos; i++) {
    if (src[i] == '=') return true;
    if (src[i] == '\n' || src[i] == '\r' || src[i] == ';') return false;
  }
  return false;
}

static bool identifier_occurs_in_range(const char *src, size_t len, size_t start, size_t end, const char *name) {
  size_t n = strlen(name);
  for (size_t i = start; i + n <= end && i + n <= len;) {
    if (src[i] == '\'' || src[i] == '"' || src[i] == '`') {
      i = skim_skip_string_raw(src, len, i);
      continue;
    }
    if (skim_skip_comment(src, len, &i)) continue;
    if (
      memcmp(src + i, name, n) == 0 && (i == 0 || !skim_is_id_part(src[i - 1])) &&
      (i + n == len || !skim_is_id_part(src[i + n]))
    ) {
      return true;
    }
    i++;
  }
  return false;
}

static bool identifier_has_external_value_use(
  const char *src,
  size_t len,
  import_equals_decl_t *decls,
  size_t decl_count,
  const char *name,
  size_t start
) {
  size_t n = strlen(name);
  for (size_t i = start; i + n <= len;) {
    if (src[i] == '\'' || src[i] == '"' || src[i] == '`') {
      i = skim_skip_string_raw(src, len, i);
      continue;
    }
    if (skim_skip_comment(src, len, &i)) continue;
    if (
      memcmp(src + i, name, n) == 0 && (i == 0 || !skim_is_id_part(src[i - 1])) &&
      (i + n == len || !skim_is_id_part(src[i + n]))
    ) {
      bool type_position = occurrence_is_type_position(src, len, i);
      if (
        !pos_in_import_equals_decl(decls, decl_count, i) && !pos_in_import_equals_statement(src, len, i) &&
        !word_before_is(src, i, "import") && (!type_position || typeof_before_is_type_query(src, i))
      )
        return true;
      i += n;
      continue;
    }
    i++;
  }
  return false;
}

static import_equals_decl_t *collect_import_equals_decls(const char *src, size_t len, size_t *out_count) {
  import_equals_decl_t *decls = NULL;
  size_t count = 0;
  for (size_t i = 0; i < len;) {
    if (src[i] == '\'' || src[i] == '"' || src[i] == '`') {
      i = skim_skip_string_raw(src, len, i);
      continue;
    }
    if (skim_skip_comment(src, len, &i)) continue;
    if (!skim_word_at(src, len, i, "import")) {
      i++;
      continue;
    }
    size_t name_start = 0, name_end = 0;
    size_t import_name = skim_skip_ws_comments(src, len, i + 6);
    size_t after_name = skim_parse_identifier(src, len, import_name, &name_start, &name_end);
    size_t eq = skim_skip_ws_comments(src, len, after_name);
    if (name_start != name_end && eq < len && src[eq] == '=') {
      size_t rhs = skim_skip_ws_comments(src, len, eq + 1);
      size_t end = import_equals_end(src, len, rhs);
      size_t rhs_end = end;
      if (rhs_end > rhs && src[rhs_end - 1] == ';') rhs_end--;
      while (rhs_end > rhs && (src[rhs_end - 1] == ' ' || src[rhs_end - 1] == '\t' || src[rhs_end - 1] == '\n'))
        rhs_end--;
      import_equals_decl_t *next = realloc(decls, sizeof(*decls) * (count + 1));
      if (!next) skim_die("out of memory");
      decls = next;
      decls[count].name = skim_slice_dup(src, name_start, name_end);
      decls[count].stmt_start = i;
      decls[count].stmt_end = end;
      decls[count].rhs_start = rhs;
      decls[count].rhs_end = rhs_end;
      decls[count].live = false;
      count++;
      i = end;
      continue;
    }
    i++;
  }
  *out_count = count;
  return decls;
}

static bool import_equals_is_live(const char *src, size_t len, const char *name) {
  size_t decl_count = 0;
  import_equals_decl_t *decls = collect_import_equals_decls(src, len, &decl_count);
  for (size_t i = 0; i < decl_count; i++) {
    decls[i].live = identifier_has_external_value_use(src, len, decls, decl_count, decls[i].name, decls[i].stmt_end);
  }
  bool changed = true;
  while (changed) {
    changed = false;
    for (size_t i = 0; i < decl_count; i++) {
      if (!decls[i].live) continue;
      for (size_t j = 0; j < decl_count; j++) {
        if (decls[j].live) continue;
        if (identifier_occurs_in_range(src, len, decls[i].rhs_start, decls[i].rhs_end, decls[j].name)) {
          decls[j].live = true;
          changed = true;
        }
      }
    }
  }

  bool live = false;
  for (size_t i = 0; i < decl_count; i++) {
    if (strcmp(decls[i].name, name) == 0) live = decls[i].live;
    free(decls[i].name);
  }
  free(decls);
  return live;
}

static bool try_named_import(skim_str_t *out, const char *src, size_t len, size_t i, size_t brace, size_t *end_out) {
  size_t close = skim_skip_balanced(src, len, brace, '{', '}');
  if (close <= brace || close > len) return false;
  size_t after = skim_skip_ws_comments(src, len, close);
  if (!skim_word_at(src, len, after, "from")) return false;
  size_t module_start = skim_skip_ws_comments(src, len, after + 4);
  size_t end = import_statement_end(src, len, i);
  *end_out = end;
  size_t module_end = end;
  if (module_end > module_start && src[module_end - 1] == ';') module_end--;

  size_t empty_check = skim_skip_ws_comments(src, len, brace + 1);
  if (empty_check + 1 == close) {
    skim_str_puts(out, "import ");
    skim_str_putn(out, src + module_start, module_end - module_start);
    skim_str_puts(out, ";\n");
    return true;
  }

  import_binding_t *bindings = NULL;
  size_t binding_count = 0;
  size_t binding_cap = 0;
  size_t item_start = brace + 1;
  for (size_t p = brace + 1; p <= close - 1; p++) {
    if (p < close - 1 && src[p] != ',') continue;
    size_t a = trim_left(src, item_start, p);
    size_t b = trim_right(src, a, p);
    if (a < b && !specifier_is_explicit_type(src, a, b)) {
      push_import_binding(&bindings, &binding_count, &binding_cap, local_name_from_specifier(src, a, b), a, b);
    }
    item_start = p + 1;
  }

  analyze_import_bindings(src, len, end, bindings, binding_count);

  skim_str_t kept = {0};
  size_t kept_count = 0;
  for (size_t b = 0; b < binding_count; b++) {
    if (!import_binding_should_keep(&bindings[b], src, i)) continue;
    if (kept_count++) skim_str_puts(&kept, ", ");
    skim_str_putn(&kept, src + bindings[b].spec_start, bindings[b].spec_end - bindings[b].spec_start);
  }

  if (kept_count > 0) {
    skim_str_puts(out, "import { ");
    skim_str_putn(out, kept.data, kept.len);
    skim_str_puts(out, " } from ");
    skim_str_putn(out, src + module_start, module_end - module_start);
    skim_str_puts(out, ";\n");
  } else {
    skim_emit_preserved_newlines(out, src, i, end);
  }
  free(kept.data);
  free_import_bindings(bindings, binding_count);
  return true;
}

static bool try_default_named_import(
  skim_str_t *out,
  const char *src,
  size_t len,
  size_t i,
  size_t default_start,
  size_t default_end,
  size_t brace,
  size_t *end_out
) {
  size_t close = skim_skip_balanced(src, len, brace, '{', '}');
  if (close <= brace + 1 || close > len) return false;
  size_t after = skim_skip_ws_comments(src, len, close);
  if (!skim_word_at(src, len, after, "from")) return false;
  size_t module_start = skim_skip_ws_comments(src, len, after + 4);
  size_t end = import_statement_end(src, len, i);
  *end_out = end;
  size_t module_end = end;
  if (module_end > module_start && src[module_end - 1] == ';') module_end--;

  import_binding_t *bindings = NULL;
  size_t binding_count = 0;
  size_t binding_cap = 0;
  push_import_binding(
    &bindings, &binding_count, &binding_cap, skim_slice_dup(src, default_start, default_end), default_start, default_end
  );
  size_t item_start = brace + 1;
  for (size_t p = brace + 1; p <= close - 1; p++) {
    if (p < close - 1 && src[p] != ',') continue;
    size_t a = trim_left(src, item_start, p);
    size_t b = trim_right(src, a, p);
    if (a < b && !specifier_is_explicit_type(src, a, b)) {
      push_import_binding(&bindings, &binding_count, &binding_cap, local_name_from_specifier(src, a, b), a, b);
    }
    item_start = p + 1;
  }

  analyze_import_bindings(src, len, end, bindings, binding_count);
  bool keep_default = binding_count > 0 && import_binding_should_keep(&bindings[0], src, i);

  skim_str_t kept = {0};
  size_t kept_count = 0;
  for (size_t b = 1; b < binding_count; b++) {
    if (!import_binding_should_keep(&bindings[b], src, i)) continue;
    if (kept_count++) skim_str_puts(&kept, ", ");
    skim_str_putn(&kept, src + bindings[b].spec_start, bindings[b].spec_end - bindings[b].spec_start);
  }

  if (keep_default || kept_count > 0) {
    skim_str_puts(out, "import ");
    if (keep_default) {
      skim_str_putn(out, src + default_start, default_end - default_start);
      if (kept_count) skim_str_puts(out, ", ");
    }
    if (kept_count) {
      skim_str_puts(out, "{ ");
      skim_str_putn(out, kept.data, kept.len);
      skim_str_puts(out, " }");
    }
    skim_str_puts(out, " from ");
    skim_str_putn(out, src + module_start, module_end - module_start);
    skim_str_puts(out, ";\n");
  } else {
    skim_emit_preserved_newlines(out, src, i, end);
  }
  free(kept.data);
  free_import_bindings(bindings, binding_count);
  return true;
}

static size_t module_specifier_end(const char *src, size_t module_start, size_t stmt_end) {
  size_t module_end = stmt_end;
  if (module_end > module_start && src[module_end - 1] == ';') module_end--;
  return module_end;
}

static bool try_type_import_statement(skim_str_t *out, const char *src, size_t len, size_t i, size_t j, size_t *io) {
  if (!skim_word_at(src, len, j, "type")) return false;
  size_t end = import_statement_end(src, len, i);
  skim_emit_preserved_newlines(out, src, i, end);
  *io = end;
  return true;
}

static bool
try_namespace_import_statement(skim_str_t *out, const char *src, size_t len, size_t i, size_t j, size_t *io) {
  if (j >= len || src[j] != '*') return false;

  size_t as_pos = skim_skip_ws_comments(src, len, j + 1);
  if (!skim_word_at(src, len, as_pos, "as")) return false;

  size_t name_start = 0, name_end = 0;
  size_t after_name = skim_parse_identifier(src, len, as_pos + 2, &name_start, &name_end);
  size_t from_pos = skim_skip_ws_comments(src, len, after_name);
  if (name_start == name_end || !skim_word_at(src, len, from_pos, "from")) return false;

  size_t module_start = skim_skip_ws_comments(src, len, from_pos + 4);
  size_t end = import_statement_end(src, len, i);
  size_t module_end = module_specifier_end(src, module_start, end);
  char *local = skim_slice_dup(src, name_start, name_end);
  bool keep = import_local_should_keep(src, len, end, local, i);
  free(local);

  if (keep) {
    skim_str_puts(out, "import * as ");
    skim_str_putn(out, src + name_start, name_end - name_start);
    skim_str_puts(out, " from ");
    skim_str_putn(out, src + module_start, module_end - module_start);
    skim_str_puts(out, ";\n");
  } else {
    skim_emit_preserved_newlines(out, src, i, end);
  }
  *io = end;
  return true;
}

static bool try_named_import_statement(skim_str_t *out, const char *src, size_t len, size_t i, size_t j, size_t *io) {
  if (j >= len || src[j] != '{') return false;
  return try_named_import(out, src, len, i, j, io);
}

static bool try_default_named_import_statement(
  skim_str_t *out,
  const char *src,
  size_t len,
  size_t i,
  size_t name_start,
  size_t name_end,
  size_t eq,
  size_t *io
) {
  if (name_start == name_end || eq >= len || src[eq] != ',') return false;
  size_t brace = skim_skip_ws_comments(src, len, eq + 1);
  if (brace >= len || src[brace] != '{') return false;
  return try_default_named_import(out, src, len, i, name_start, name_end, brace, io);
}

static bool try_import_equals_statement(
  skim_str_t *out,
  const char *src,
  size_t len,
  size_t i,
  size_t name_start,
  size_t name_end,
  size_t eq,
  size_t *io
) {
  if (name_start == name_end || eq >= len || src[eq] != '=') return false;

  size_t rhs = skim_skip_ws_comments(src, len, eq + 1);
  size_t end = import_equals_end(src, len, rhs);
  size_t rhs_end = module_specifier_end(src, rhs, end);
  while (rhs_end > rhs && (src[rhs_end - 1] == ' ' || src[rhs_end - 1] == '\t' || src[rhs_end - 1] == '\n'))
    rhs_end--;

  char *local = skim_slice_dup(src, name_start, name_end);
  bool used =
    strcmp(local, "await") != 0 && (skim_options.only_remove_type_imports || import_equals_is_live(src, len, local));
  free(local);

  if (used) {
    bool is_require = skim_word_at(src, len, rhs, "require");
    skim_str_puts(out, is_require ? "const " : "var ");
    skim_str_putn(out, src + name_start, name_end - name_start);
    skim_str_puts(out, " = ");
    skim_str_putn(out, src + rhs, rhs_end - rhs);
    skim_str_puts(out, ";\n");
  } else {
    skim_emit_preserved_newlines(out, src, i, end);
  }
  *io = end;
  return true;
}

static bool try_default_import_statement(
  skim_str_t *out,
  const char *src,
  size_t len,
  size_t i,
  size_t name_start,
  size_t name_end,
  size_t eq,
  size_t *io
) {
  if (name_start == name_end || !skim_word_at(src, len, eq, "from")) return false;

  size_t module_start = skim_skip_ws_comments(src, len, eq + 4);
  size_t end = import_statement_end(src, len, i);
  size_t module_end = module_specifier_end(src, module_start, end);
  char *local = skim_slice_dup(src, name_start, name_end);
  bool keep = import_local_should_keep(src, len, end, local, i);
  free(local);

  if (keep) {
    skim_str_puts(out, "import ");
    skim_str_putn(out, src + name_start, name_end - name_start);
    skim_str_puts(out, " from ");
    skim_str_putn(out, src + module_start, module_end - module_start);
    skim_str_puts(out, ";\n");
  } else {
    skim_emit_preserved_newlines(out, src, i, end);
  }
  *io = end;
  return true;
}

static bool try_import_statement(skim_str_t *out, const char *src, size_t len, size_t i, size_t *io) {
  if (!skim_word_at(src, len, i, "import")) return false;

  size_t j = skim_skip_ws_comments(src, len, i + 6);
  if (try_type_import_statement(out, src, len, i, j, io)) return true;
  if (try_namespace_import_statement(out, src, len, i, j, io)) return true;
  if (try_named_import_statement(out, src, len, i, j, io)) return true;

  size_t name_start = 0, name_end = 0;
  size_t after_name = skim_parse_identifier(src, len, j, &name_start, &name_end);
  size_t eq = skim_skip_ws_comments(src, len, after_name);
  if (try_default_named_import_statement(out, src, len, i, name_start, name_end, eq, io)) return true;
  if (try_import_equals_statement(out, src, len, i, name_start, name_end, eq, io)) return true;
  if (try_default_import_statement(out, src, len, i, name_start, name_end, eq, io)) return true;
  return false;
}

bool skim_ts_module_try(skim_str_t *out, const char *src, size_t len, size_t *io) {
  size_t i = *io;

  if (try_import_statement(out, src, len, i, io)) return true;

  if (skim_word_at(src, len, i, "export")) {
    size_t j = skim_skip_ws_comments(src, len, i + 6);
    size_t default_overload_end = 0;
    if (export_default_function_overload_end(src, len, i, &default_overload_end)) {
      skim_emit_preserved_newlines(out, src, i, default_overload_end);
      *io = default_overload_end;
      return true;
    }
    if (skim_word_at(src, len, j, "type") || skim_word_at(src, len, j, "interface")) {
      size_t end = i;
      if (skim_word_at(src, len, j, "interface")) {
        size_t body = j + 9;
        while (body < len && src[body] != '{' && src[body] != ';')
          body++;
        if (body < len && src[body] == '{') end = skim_skip_balanced(src, len, body, '{', '}');
        else end = body < len ? body + 1 : body;
      } else {
        end = export_type_statement_end(src, len, i);
      }
      skim_emit_preserved_newlines(out, src, i, end);
      *io = end;
      return true;
    }
    if (skim_word_at(src, len, j, "as")) {
      size_t ns = skim_skip_ws_comments(src, len, j + 2);
      if (skim_word_at(src, len, ns, "namespace")) {
        size_t end = skim_skip_statement_like(src, len, i);
        skim_emit_preserved_newlines(out, src, i, end);
        *io = end;
        return true;
      }
    }
    size_t export_end = 0;
    if (j < len && src[j] == '{' && try_export_named(out, src, len, i, j, &export_end)) {
      *io = export_end;
      return true;
    }
    if (j < len && src[j] == '=') {
      size_t rhs = skim_skip_ws_comments(src, len, j + 1);
      size_t end = skim_skip_statement_like(src, len, rhs);
      size_t rhs_end = end;
      if (rhs_end > rhs && src[rhs_end - 1] == ';') rhs_end--;
      skim_str_puts(out, "module.exports = ");
      skim_str_putn(out, src + rhs, rhs_end - rhs);
      skim_str_puts(out, ";\n");
      *io = end;
      return true;
    }
  }

  return false;
}
