#include "skim_internal.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static size_t trim_left(const char *src, size_t len, size_t i, size_t end) {
  (void)len;
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

static bool occurrence_is_type_position(const char *src, size_t pos) {
  if (
    word_before_is(src, pos, "as") || word_before_is(src, pos, "satisfies") || word_before_is(src, pos, "type") ||
    word_before_is(src, pos, "interface") || typeof_before_is_type_query(src, pos)
  )
    return true;
  char prev = prev_non_ws(src, pos);
  return prev == ':' || prev == '|' || prev == '&' || prev == '<';
}

static bool identifier_has_value_use(const char *src, size_t len, size_t start, const char *name) {
  size_t n = strlen(name);
  for (size_t i = start; i + n <= len;) {
    if (src[i] == '\'' || src[i] == '"' || src[i] == '`') {
      i = skim_skip_string_raw(src, len, i);
      continue;
    }
    if (i + 1 < len && src[i] == '/' && src[i + 1] == '/') {
      i += 2;
      while (i < len && src[i] != '\n')
        i++;
      continue;
    }
    if (i + 1 < len && src[i] == '/' && src[i + 1] == '*') {
      i += 2;
      while (i + 1 < len && !(src[i] == '*' && src[i + 1] == '/'))
        i++;
      if (i + 1 < len) i += 2;
      continue;
    }
    if (
      memcmp(src + i, name, n) == 0 && (i == 0 || !skim_is_id_part(src[i - 1])) &&
      (i + n == len || !skim_is_id_part(src[i + n]))
    ) {
      if (!occurrence_is_type_position(src, i)) return true;
      i += n;
      continue;
    }
    i++;
  }
  return false;
}

static char *local_name_from_specifier(const char *src, size_t start, size_t end) {
  start = trim_left(src, end, start, end);
  end = trim_right(src, start, end);
  if (skim_word_at(src, end, start, "type")) start = trim_left(src, end, start + 4, end);
  size_t as_pos = start;
  while (as_pos < end) {
    if (skim_word_at(src, end, as_pos, "as")) {
      size_t name_start = 0, name_end = 0;
      skim_parse_identifier(src, end, as_pos + 2, &name_start, &name_end);
      if (name_start != name_end) return skim_slice_dup(src, name_start, name_end);
    }
    as_pos++;
  }
  size_t name_start = 0, name_end = 0;
  skim_parse_identifier(src, end, start, &name_start, &name_end);
  if (name_start == name_end) return NULL;
  return skim_slice_dup(src, name_start, name_end);
}

static bool specifier_is_explicit_type(const char *src, size_t start, size_t end) {
  start = trim_left(src, end, start, end);
  return skim_word_at(src, end, start, "type");
}

static char *exported_binding_from_specifier(const char *src, size_t start, size_t end) {
  start = trim_left(src, end, start, end);
  if (skim_word_at(src, end, start, "type")) start = trim_left(src, end, start + 4, end);
  size_t name_start = 0, name_end = 0;
  skim_parse_identifier(src, end, start, &name_start, &name_end);
  if (name_start == name_end) return NULL;
  return skim_slice_dup(src, name_start, name_end);
}

static bool has_type_declaration(const char *src, size_t len, const char *name) {
  size_t n = strlen(name);
  for (size_t i = 0; i + n < len; i++) {
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
    if (!skim_word_at(src, len, i, "import")) continue;
    size_t j = skim_skip_ws_comments(src, len, i + 6);
    bool whole_import_is_type = false;
    if (skim_word_at(src, len, j, "type")) {
      whole_import_is_type = true;
      j = skim_skip_ws_comments(src, len, j + 4);
    }
    if (j < len && src[j] == '{') {
      size_t close = skim_skip_balanced(src, len, j, '{', '}');
      size_t item_start = j + 1;
      for (size_t p = j + 1; p <= close - 1; p++) {
        if (p < close - 1 && src[p] != ',') continue;
        bool spec_type = whole_import_is_type || specifier_is_explicit_type(src, item_start, p);
        char *local = local_name_from_specifier(src, item_start, p);
        bool matches = spec_type && local && strcmp(local, name) == 0;
        free(local);
        if (matches) return true;
        item_start = p + 1;
      }
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
    if (skim_word_at(src, len, i, "import")) {
      size_t j = skim_skip_ws_comments(src, len, i + 6);
      if (skim_word_at(src, len, j, "type")) continue;
      if (j < len && src[j] == '{') {
        size_t close = skim_skip_balanced(src, len, j, '{', '}');
        size_t item_start = j + 1;
        for (size_t p = j + 1; p <= close - 1; p++) {
          if (p < close - 1 && src[p] != ',') continue;
          char *local = local_name_from_specifier(src, item_start, p);
          bool matches = local && strcmp(local, name) == 0;
          free(local);
          if (matches) return true;
          item_start = p + 1;
        }
      } else {
        size_t name_start = 0, name_end = 0;
        size_t after_name = skim_parse_identifier(src, len, j, &name_start, &name_end);
        if (name_start != name_end && name_end - name_start == n && memcmp(src + name_start, name, n) == 0) return true;
        size_t comma = skim_skip_ws_comments(src, len, after_name);
        if (comma < len && src[comma] == ',') {
          size_t brace = skim_skip_ws_comments(src, len, comma + 1);
          if (brace < len && src[brace] == '{') {
            size_t close = skim_skip_balanced(src, len, brace, '{', '}');
            size_t item_start = brace + 1;
            for (size_t p = brace + 1; p <= close - 1; p++) {
              if (p < close - 1 && src[p] != ',') continue;
              char *local = local_name_from_specifier(src, item_start, p);
              bool matches = local && strcmp(local, name) == 0;
              free(local);
              if (matches) return true;
              item_start = p + 1;
            }
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
  bool value_use;
  bool later_decl;
} import_binding_t;

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

static import_binding_t *find_import_binding(import_binding_t *bindings, size_t count, const char *name, size_t name_len) {
  for (size_t i = 0; i < count; i++) {
    if (bindings[i].local_len == name_len && memcmp(bindings[i].local, name, name_len) == 0) return &bindings[i];
  }
  return NULL;
}

static void mark_import_binding_decl(import_binding_t *bindings, size_t count, const char *name, size_t name_len) {
  import_binding_t *binding = find_import_binding(bindings, count, name, name_len);
  if (binding) binding->later_decl = true;
}

static size_t mark_import_decl_bindings(import_binding_t *bindings, size_t count, const char *src, size_t len, size_t i) {
  size_t j = skim_skip_ws_comments(src, len, i + 6);
  if (skim_word_at(src, len, j, "type")) return skim_skip_statement_like(src, len, i);

  if (j < len && src[j] == '{') {
    size_t close = skim_skip_balanced(src, len, j, '{', '}');
    size_t item_start = j + 1;
    for (size_t p = j + 1; p <= close - 1; p++) {
      if (p < close - 1 && src[p] != ',') continue;
      size_t a = trim_left(src, len, item_start, p);
      size_t b = trim_right(src, a, p);
      if (a < b && !specifier_is_explicit_type(src, a, b)) {
        char *local = local_name_from_specifier(src, a, b);
        if (local) mark_import_binding_decl(bindings, count, local, strlen(local));
        free(local);
      }
      item_start = p + 1;
    }
    return skim_skip_statement_like(src, len, close);
  }

  if (j < len && src[j] == '*') {
    size_t as_pos = skim_skip_ws_comments(src, len, j + 1);
    if (skim_word_at(src, len, as_pos, "as")) {
      size_t name_start = 0, name_end = 0;
      skim_parse_identifier(src, len, as_pos + 2, &name_start, &name_end);
      if (name_start != name_end) mark_import_binding_decl(bindings, count, src + name_start, name_end - name_start);
    }
    return skim_skip_statement_like(src, len, j);
  }

  size_t name_start = 0, name_end = 0;
  size_t after_name = skim_parse_identifier(src, len, j, &name_start, &name_end);
  if (name_start != name_end) mark_import_binding_decl(bindings, count, src + name_start, name_end - name_start);

  size_t comma = skim_skip_ws_comments(src, len, after_name);
  if (comma < len && src[comma] == ',') {
    size_t brace = skim_skip_ws_comments(src, len, comma + 1);
    if (brace < len && src[brace] == '{') {
      size_t close = skim_skip_balanced(src, len, brace, '{', '}');
      size_t item_start = brace + 1;
      for (size_t p = brace + 1; p <= close - 1; p++) {
        if (p < close - 1 && src[p] != ',') continue;
        size_t a = trim_left(src, len, item_start, p);
        size_t b = trim_right(src, a, p);
        if (a < b && !specifier_is_explicit_type(src, a, b)) {
          char *local = local_name_from_specifier(src, a, b);
          if (local) mark_import_binding_decl(bindings, count, local, strlen(local));
          free(local);
        }
        item_start = p + 1;
      }
      return skim_skip_statement_like(src, len, close);
    }
  }
  return skim_skip_statement_like(src, len, i);
}

static bool value_decl_keyword_at(const char *src, size_t len, size_t i, size_t *keyword_len) {
  static const char *decls[] = {"const", "let", "var", "function", "class", "enum", "namespace"};
  for (size_t d = 0; d < sizeof(decls) / sizeof(decls[0]); d++) {
    if (skim_word_at(src, len, i, decls[d])) {
      *keyword_len = strlen(decls[d]);
      return true;
    }
  }
  return false;
}

static void analyze_import_bindings(const char *src, size_t len, size_t start, import_binding_t *bindings, size_t count) {
  size_t live = 0;
  for (size_t b = 0; b < count; b++)
    live += !bindings[b].later_decl && !bindings[b].value_use;
  if (live == 0) return;

  for (size_t i = start; i < len;) {
    if (src[i] == '\'' || src[i] == '"' || src[i] == '`') {
      i = skim_skip_string_raw(src, len, i);
      continue;
    }
    if (i + 1 < len && src[i] == '/' && src[i + 1] == '/') {
      i += 2;
      while (i < len && src[i] != '\n')
        i++;
      continue;
    }
    if (i + 1 < len && src[i] == '/' && src[i + 1] == '*') {
      i += 2;
      while (i + 1 < len && !(src[i] == '*' && src[i + 1] == '/'))
        i++;
      if (i + 1 < len) i += 2;
      continue;
    }
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
      i = mark_import_decl_bindings(bindings, count, src, len, ident_start);
      continue;
    }

    size_t keyword_len = 0;
    if (value_decl_keyword_at(src, len, ident_start, &keyword_len)) {
      size_t name_start = 0, name_end = 0;
      skim_parse_identifier(src, len, ident_start + keyword_len, &name_start, &name_end);
      if (name_start != name_end) mark_import_binding_decl(bindings, count, src + name_start, name_end - name_start);
    }

    import_binding_t *binding = find_import_binding(bindings, count, src + ident_start, ident_len);
    if (binding && !occurrence_is_type_position(src, ident_start)) binding->value_use = true;
  }
}

static bool import_binding_should_keep(const import_binding_t *binding, const char *src, size_t import_pos) {
  return binding->value_use && !binding->later_decl &&
         !skim_decl_seen_before(src, binding->local, import_pos, SKIM_DECL_VALUE);
}

static bool try_export_named(skim_str_t *out, const char *src, size_t len, size_t i, size_t brace, size_t *out_end) {
  (void)i;
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
    size_t a = trim_left(src, len, item_start, p);
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
  int paren = 0, bracket = 0, brace = 0, angle = 0;
  bool seen_token = false;
  char last_sig = '\0';
  while (i < len) {
    char c = src[i];
    if ((c == '\n' || c == '\r') && paren == 0 && bracket == 0 && brace == 0 && angle == 0 && seen_token) {
      size_t next = skim_skip_ws(src, len, i + 1);
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
      i += 2;
      while (i + 1 < len && !(src[i] == '*' && src[i + 1] == '/'))
        i++;
      if (i + 1 < len) i += 2;
      continue;
    }
    if (isspace((unsigned char)c)) {
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
    if (i + 1 < len && src[i] == '/' && src[i + 1] == '/') {
      i += 2;
      while (i < len && src[i] != '\n')
        i++;
      continue;
    }
    if (i + 1 < len && src[i] == '/' && src[i + 1] == '*') {
      i += 2;
      while (i + 1 < len && !(src[i] == '*' && src[i + 1] == '/'))
        i++;
      if (i + 1 < len) i += 2;
      continue;
    }
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
    if (i + 1 < len && src[i] == '/' && src[i + 1] == '/') {
      i += 2;
      while (i < len && src[i] != '\n')
        i++;
      continue;
    }
    if (i + 1 < len && src[i] == '/' && src[i + 1] == '*') {
      i += 2;
      while (i + 1 < len && !(src[i] == '*' && src[i + 1] == '/'))
        i++;
      if (i + 1 < len) i += 2;
      continue;
    }
    if (
      memcmp(src + i, name, n) == 0 && (i == 0 || !skim_is_id_part(src[i - 1])) &&
      (i + n == len || !skim_is_id_part(src[i + n]))
    ) {
      bool type_position = occurrence_is_type_position(src, i);
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
    if (i + 1 < len && src[i] == '/' && src[i + 1] == '/') {
      i += 2;
      while (i < len && src[i] != '\n')
        i++;
      continue;
    }
    if (i + 1 < len && src[i] == '/' && src[i + 1] == '*') {
      i += 2;
      while (i + 1 < len && !(src[i] == '*' && src[i + 1] == '/'))
        i++;
      if (i + 1 < len) i += 2;
      continue;
    }
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

static bool try_named_import(skim_str_t *out, const char *src, size_t len, size_t i, size_t brace) {
  size_t close = skim_skip_balanced(src, len, brace, '{', '}');
  if (close <= brace || close > len) return false;
  size_t after = skim_skip_ws_comments(src, len, close);
  if (!skim_word_at(src, len, after, "from")) return false;
  size_t module_start = skim_skip_ws_comments(src, len, after + 4);
  size_t end = skim_skip_statement_like(src, len, module_start);
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
    size_t a = trim_left(src, len, item_start, p);
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
  size_t brace
) {
  size_t close = skim_skip_balanced(src, len, brace, '{', '}');
  if (close <= brace + 1 || close > len) return false;
  size_t after = skim_skip_ws_comments(src, len, close);
  if (!skim_word_at(src, len, after, "from")) return false;
  size_t module_start = skim_skip_ws_comments(src, len, after + 4);
  size_t end = skim_skip_statement_like(src, len, module_start);
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
    size_t a = trim_left(src, len, item_start, p);
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

bool skim_ts_module_try(skim_str_t *out, const char *src, size_t len, size_t *io) {
  size_t i = *io;

  if (skim_word_at(src, len, i, "import")) {
    size_t j = skim_skip_ws_comments(src, len, i + 6);
    if (skim_word_at(src, len, j, "type")) {
      size_t end = skim_skip_statement_like(src, len, i);
      skim_emit_preserved_newlines(out, src, i, end);
      *io = end;
      return true;
    }

    if (j < len && src[j] == '*') {
      size_t as_pos = skim_skip_ws_comments(src, len, j + 1);
      if (skim_word_at(src, len, as_pos, "as")) {
        size_t name_start = 0, name_end = 0;
        size_t after_name = skim_parse_identifier(src, len, as_pos + 2, &name_start, &name_end);
        size_t from_pos = skim_skip_ws_comments(src, len, after_name);
        if (name_start != name_end && skim_word_at(src, len, from_pos, "from")) {
          size_t module_start = skim_skip_ws_comments(src, len, from_pos + 4);
          size_t end = skim_skip_statement_like(src, len, module_start);
          size_t module_end = end;
          if (module_end > module_start && src[module_end - 1] == ';') module_end--;
          char *local = skim_slice_dup(src, name_start, name_end);
          bool keep = identifier_has_value_use(src, len, end, local) &&
                      !skim_decl_seen_before(src, local, i, SKIM_DECL_VALUE) &&
                      !has_value_declaration(src + end, len - end, local);
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
      }
    }

    if (j < len && src[j] == '{') {
      if (try_named_import(out, src, len, i, j)) {
        *io = skim_skip_statement_like(src, len, j);
        return true;
      }
    }

    size_t name_start = 0, name_end = 0;
    size_t after_name = skim_parse_identifier(src, len, j, &name_start, &name_end);
    size_t eq = skim_skip_ws_comments(src, len, after_name);
    if (name_start != name_end && eq < len && src[eq] == ',') {
      size_t brace = skim_skip_ws_comments(src, len, eq + 1);
      if (brace < len && src[brace] == '{' && try_default_named_import(out, src, len, i, name_start, name_end, brace)) {
        *io = skim_skip_statement_like(src, len, brace);
        return true;
      }
    }
    if (name_start != name_end && eq < len && src[eq] == '=') {
      size_t rhs = skim_skip_ws_comments(src, len, eq + 1);
      size_t end = import_equals_end(src, len, rhs);
      size_t rhs_end = end;
      if (rhs_end > rhs && src[rhs_end - 1] == ';') rhs_end--;
      while (rhs_end > rhs && (src[rhs_end - 1] == ' ' || src[rhs_end - 1] == '\t' || src[rhs_end - 1] == '\n'))
        rhs_end--;

      char *local = skim_slice_dup(src, name_start, name_end);
      bool used = strcmp(local, "await") != 0 &&
                  (skim_options.only_remove_type_imports || import_equals_is_live(src, len, local));
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
    if (name_start != name_end && skim_word_at(src, len, eq, "from")) {
      size_t module_start = skim_skip_ws_comments(src, len, eq + 4);
      size_t end = skim_skip_statement_like(src, len, module_start);
      size_t module_end = end;
      if (module_end > module_start && src[module_end - 1] == ';') module_end--;
      char *local = skim_slice_dup(src, name_start, name_end);
      bool keep = identifier_has_value_use(src, len, end, local) &&
                  !skim_decl_seen_before(src, local, i, SKIM_DECL_VALUE) &&
                  !has_value_declaration(src + end, len - end, local);
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
  }

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
