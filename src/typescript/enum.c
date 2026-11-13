#include "chars.h"
#include "decl.h"
#include "emit.h"
#include "enum_internal.h"
#include "run.h"
#include "scan_cache.h"
#include "skip.h"
#include "str.h"
#include "transform.h"
#include "typescript.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t *known_member_buckets(void) {
  skim_enum_state_t *es = &skim_run->ts.enums;
  return es->known_member_buckets;
}

static size_t enum_member_hash(const char *enum_name, size_t enum_len, const char *member_name, size_t member_len) {
  size_t h = 1469598103934665603ull;
  for (size_t i = 0; i < enum_len; i++) {
    h ^= (unsigned char)enum_name[i];
    h *= 1099511628211ull;
  }
  h ^= '.';
  h *= 1099511628211ull;
  for (size_t i = 0; i < member_len; i++) {
    h ^= (unsigned char)member_name[i];
    h *= 1099511628211ull;
  }
  return h & (SKIM_BUCKET_COUNT - 1);
}

void skim_ts_enum_reset(void) {
  skim_enum_state_t *es = &skim_run->ts.enums;
  if (!es->known_member_buckets) {
    es->known_member_buckets = malloc(sizeof(*es->known_member_buckets) * SKIM_BUCKET_COUNT);
    if (!es->known_member_buckets) skim_die("out of memory");
    memset(es->known_member_buckets, 0xff, sizeof(*es->known_member_buckets) * SKIM_BUCKET_COUNT);
  }
  for (size_t i = 0; i < es->known_member_count; i++) {
    es->known_member_buckets[es->known_members[i].hash_bucket] = (size_t)-1;
    free(es->known_members[i].enum_name);
    free(es->known_members[i].member_name);
    skim_enum_value_free(es->known_members[i].value);
  }
  free(es->known_members);
  es->known_members = NULL;
  es->known_member_count = 0;
  es->known_member_cap = 0;
  es->inline_member_count = 0;
  es->enum_expression_depth = 0;
  es->current_enum_inline_refs = false;
  es->current_enum_scope_start = 0;
  es->enum_suppress_depth = 0;
}

void skim_ts_enum_suppress_push(void) {
  skim_enum_state_t *es = &skim_run->ts.enums;
  es->enum_suppress_depth++;
}

void skim_ts_enum_suppress_pop(void) {
  skim_enum_state_t *es = &skim_run->ts.enums;
  if (es->enum_suppress_depth > 0) es->enum_suppress_depth--;
}

static void remember_member(const char *enum_name, const char *member_name, enum_value_t value) {
  skim_enum_state_t *es = &skim_run->ts.enums;
  if (es->enum_suppress_depth > 0) return;
  if (es->known_member_count == es->known_member_cap) {
    size_t cap = es->known_member_cap ? es->known_member_cap * 2 : 64;
    enum_known_member_t *next = realloc(es->known_members, sizeof(*es->known_members) * cap);
    if (!next) skim_die("out of memory");
    es->known_members = next;
    es->known_member_cap = cap;
  }
  size_t enum_len = strlen(enum_name);
  size_t member_len = strlen(member_name);
  size_t bucket = enum_member_hash(enum_name, enum_len, member_name, member_len);
  size_t *buckets = known_member_buckets();
  es->known_members[es->known_member_count].enum_name = skim_slice_dup(enum_name, 0, enum_len);
  es->known_members[es->known_member_count].member_name = skim_slice_dup(member_name, 0, member_len);
  es->known_members[es->known_member_count].enum_name_len = enum_len;
  es->known_members[es->known_member_count].member_name_len = member_len;
  es->known_members[es->known_member_count].value = skim_enum_value_clone(value);
  es->known_members[es->known_member_count].inline_ref = es->current_enum_inline_refs;
  es->known_members[es->known_member_count].scope_start = es->current_enum_scope_start;
  es->known_members[es->known_member_count].hash_bucket = bucket;
  es->known_members[es->known_member_count].next_hash = buckets[bucket];
  buckets[bucket] = es->known_member_count;
  es->known_member_count++;
  if (es->current_enum_inline_refs) es->inline_member_count++;
}

static void
emit_enum_assignment(skim_str_t *out, const char *name, const char *member, const char *init, bool is_string) {
  if (is_string) {
    skim_str_puts(out, "  ");
    skim_str_puts(out, name);
    skim_str_puts(out, "[\"");
    skim_str_puts(out, member);
    skim_str_puts(out, "\"] = ");
    skim_str_puts(out, init);
    skim_str_puts(out, ";\n");
    return;
  }
  skim_str_puts(out, "  ");
  skim_str_puts(out, name);
  skim_str_putc(out, '[');
  skim_str_puts(out, name);
  skim_str_puts(out, "[\"");
  skim_str_puts(out, member);
  skim_str_puts(out, "\"] = ");
  skim_str_puts(out, init);
  skim_str_puts(out, "] = \"");
  skim_str_puts(out, member);
  skim_str_puts(out, "\";\n");
}

static bool member_name_matches(const char *name, char **members, size_t member_count) {
  for (size_t i = 0; i < member_count; i++) {
    if (strcmp(name, members[i]) == 0) return true;
  }
  return false;
}

static enum_known_member_t *
find_known_member(const char *enum_name, size_t enum_len, const char *member_name, size_t member_len) {
  skim_enum_state_t *es = &skim_run->ts.enums;
  size_t *buckets = known_member_buckets();
  for (size_t i = buckets[enum_member_hash(enum_name, enum_len, member_name, member_len)]; i != (size_t)-1;
       i = es->known_members[i].next_hash) {
    enum_known_member_t *m = &es->known_members[i];
    if (
      m->enum_name_len == enum_len && m->member_name_len == member_len &&
      memcmp(enum_name, m->enum_name, enum_len) == 0 && memcmp(member_name, m->member_name, member_len) == 0
    ) {
      return m;
    }
  }
  return NULL;
}

bool skim_enum_lookup_qualified_member(const char *enum_name, const char *member_name, enum_value_t *out) {
  enum_known_member_t *m = find_known_member(enum_name, strlen(enum_name), member_name, strlen(member_name));
  if (!m || m->value.kind == ENUM_VALUE_UNKNOWN) return false;
  *out = skim_enum_value_clone(m->value);
  return true;
}

static bool lookup_inline_member_span(
  const char *enum_name,
  size_t enum_len,
  const char *member_name,
  size_t member_len,
  enum_value_t *out
) {
  enum_known_member_t *m = find_known_member(enum_name, enum_len, member_name, member_len);
  if (!m || !m->inline_ref || m->value.kind == ENUM_VALUE_UNKNOWN) return false;
  *out = skim_enum_value_clone(m->value);
  return true;
}

static bool known_enum_member_name(const char *enum_name, const char *member_name) {
  return find_known_member(enum_name, strlen(enum_name), member_name, strlen(member_name)) != NULL;
}

static bool enum_name_has_use(const char *src, size_t len, size_t start, const char *name, bool value_only) {
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
      bool skip = skim_word_before(src, i, "enum");
      if (!skip && value_only) {
        size_t j = skim_skip_ws(src, len, i + n);
        if (j < len && src[j] == '?') j = skim_skip_ws(src, len, j + 1);
        skip = j < len && (src[j] == '.' || src[j] == '[');
      }
      if (!skip) return true;
      i += n;
      continue;
    }
    i++;
  }
  return false;
}

bool skim_ts_enum_ref_try(skim_str_t *out, const char *src, size_t len, size_t *io) {
  size_t i = *io;
  if (!skim_is_id_start(src[i])) return false;
  size_t enum_start = i++;
  while (i < len && skim_is_id_part(src[i]))
    i++;
  size_t enum_len = i - enum_start;
  size_t j = skim_skip_ws(src, len, i);
  bool optional_chain = false;
  if (j + 1 < len && src[j] == '?' && src[j + 1] == '.') {
    optional_chain = true;
    j = skim_skip_ws(src, len, j + 2);
  }

  size_t member_start = 0;
  size_t member_end = 0;
  size_t end = j;
  if (optional_chain && j < len && skim_is_id_start(src[j])) {
    member_start = j++;
    while (j < len && skim_is_id_part(src[j]))
      j++;
    member_end = j;
    end = j;
  } else if (j < len && src[j] == '.') {
    end = skim_parse_identifier(src, len, j + 1, &member_start, &member_end);
  } else if (j < len && src[j] == '[') {
    size_t k = skim_skip_ws(src, len, j + 1);
    if (k < len && (src[k] == '\'' || src[k] == '"')) {
      char quote = src[k++];
      member_start = k;
      while (k < len && src[k] != quote)
        k++;
      if (k < len) {
        member_end = k;
        k = skim_skip_ws(src, len, k + 1);
        if (k < len && src[k] == ']') end = k + 1;
      }
    }
  }

  bool replaced = false;
  if (member_end > member_start) {
    enum_value_t value = skim_enum_value_unknown();
    if (lookup_inline_member_span(src + enum_start, enum_len, src + member_start, member_end - member_start, &value)) {
      char *literal = skim_enum_value_to_literal(value);
      if (literal) {
        if (value.kind == ENUM_VALUE_NUMBER && end < len && src[end] == '.') skim_str_putc(out, '(');
        skim_str_puts(out, literal);
        if (value.kind == ENUM_VALUE_NUMBER && end < len && src[end] == '.') skim_str_putc(out, ')');
        free(literal);
        *io = end;
        replaced = true;
      }
      skim_enum_value_free(value);
    }
  }
  return replaced;
}

static char *rewrite_member_refs(const char *raw, const char *enum_name, char **members, size_t member_count) {
  skim_str_t out = {0};
  size_t len = strlen(raw);
  for (size_t i = 0; i < len;) {
    if (raw[i] == '\'' || raw[i] == '"' || raw[i] == '`') {
      i = skim_copy_string(&out, raw, len, i);
      continue;
    }
    if (skim_is_id_start(raw[i])) {
      size_t start = i++;
      while (i < len && skim_is_id_part(raw[i]))
        i++;
      char *name = skim_slice_dup(raw, start, i);
      if (member_name_matches(name, members, member_count) || known_enum_member_name(enum_name, name)) {
        skim_str_puts(&out, enum_name);
        skim_str_putc(&out, '.');
      }
      skim_str_putn(&out, raw + start, i - start);
      free(name);
      continue;
    }
    skim_str_putc(&out, raw[i++]);
  }
  return out.data ? out.data : skim_slice_dup("", 0, 0);
}

static bool prior_value_decl(const char *src, size_t pos, const char *name) {
  return skim_decl_seen_before(src, name, pos, SKIM_DECL_VALUE);
}

void skim_ts_enum_scope_invalidate(void) {
  skim_enum_state_t *es = &skim_run->ts.enums;
  es->enum_scope_scan.src = NULL;
}

static size_t enum_scope_start(const char *src, size_t len, size_t pos) {
  skim_enum_state_t *es = &skim_run->ts.enums;
  if (es->enum_scope_scan.src != src || !skim_scan_cache_owns(src) || es->enum_scope_scan.i > pos) {
    es->enum_scope_scan.src = src;
    es->enum_scope_scan.i = 0;
    es->enum_scope_scan.depth = 0;
  }
  size_t i = es->enum_scope_scan.i;
  int depth = es->enum_scope_scan.depth;
  size_t *stack = es->enum_scope_scan.stack;
  while (i < pos) {
    if (!skim_char_has(src[i], SKIM_CC_BALANCED)) {
      i++;
      continue;
    }
    if (src[i] == '\'' || src[i] == '"' || src[i] == '`') {
      size_t end = skim_skip_string_raw(src, len, i);
      if (end >= pos) break;
      i = end;
      continue;
    }
    if (i + 1 < len && src[i] == '/' && src[i + 1] == '/') {
      size_t end = skim_skip_line_comment(src, len, i + 2);
      if (end >= pos) break;
      i = end;
      continue;
    }
    if (i + 1 < len && src[i] == '/' && src[i + 1] == '*') {
      size_t close = skim_find_block_comment_close(src, len, i + 2);
      if (close >= len || close + 2 > pos) break;
      i = close + 2;
      continue;
    }
    if (src[i] == '{') {
      if (depth < SKIM_ENUM_SCOPE_STACK) stack[depth] = i;
      depth++;
    } else if (src[i] == '}' && depth > 0) depth--;
    i++;
  }
  es->enum_scope_scan.i = i;
  es->enum_scope_scan.depth = depth;
  if (depth <= 0) return 0;
  if (depth > SKIM_ENUM_SCOPE_STACK) return pos;
  return stack[depth - 1];
}

static bool known_enum_in_scope(const char *name, size_t scope_start) {
  skim_enum_state_t *es = &skim_run->ts.enums;
  size_t enum_len = strlen(name);
  for (size_t i = 0; i < es->known_member_count; i++) {
    if (es->known_members[i].scope_start != scope_start) continue;
    if (es->known_members[i].enum_name_len == enum_len && memcmp(es->known_members[i].enum_name, name, enum_len) == 0)
      return true;
  }
  return false;
}

static void trim_trailing_else(skim_str_t *out) {
  size_t end = out->len;
  while (end > 0 && skim_is_space(out->data[end - 1]))
    end--;
  size_t start = end;
  while (start > 0 && skim_is_id_part(out->data[start - 1]))
    start--;
  if (end - start == 4 && memcmp(out->data + start, "else", 4) == 0) {
    while (start > 0 && (out->data[start - 1] == ' ' || out->data[start - 1] == '\t'))
      start--;
    out->len = start;
    out->data[out->len] = '\0';
  }
}

bool skim_ts_enum_try(skim_str_t *out, const char *src, size_t len, size_t *io) {
  skim_enum_state_t *es = &skim_run->ts.enums;
  size_t i = *io;
  bool is_export = false;
  bool is_const = false;
  if (skim_word_at(src, len, i, "export")) {
    size_t j = skim_skip_ws_comments(src, len, i + 6);
    is_export = true;
    if (skim_word_at(src, len, j, "const")) {
      is_const = true;
      j = skim_skip_ws_comments(src, len, j + 5);
    }
    if (!skim_word_at(src, len, j, "enum")) return false;
    i = j;
  } else {
    if (skim_word_at(src, len, i, "const")) {
      size_t j = skim_skip_ws_comments(src, len, i + 5);
      if (!skim_word_at(src, len, j, "enum")) return false;
      is_const = true;
      i = j;
    } else if (!skim_word_at(src, len, i, "enum")) {
      return false;
    }
  }

  size_t name_start = 0, name_end = 0;
  i = skim_parse_identifier(src, len, i + 4, &name_start, &name_end);
  if (name_start == name_end) return false;
  char *name = skim_slice_dup(src, name_start, name_end);
  i = skim_skip_ws_comments(src, len, i);
  if (i >= len || src[i] != '{') {
    free(name);
    return false;
  }
  i++;

  bool prev_inline_refs = es->current_enum_inline_refs;
  es->current_enum_inline_refs =
    (is_const && skim_run->options.optimize_const_enums) || skim_run->options.optimize_enums;
  skim_str_t enum_out = {0};
  size_t scope_start = enum_scope_start(src, len, *io);
  size_t prev_scope_start = es->current_enum_scope_start;
  es->current_enum_scope_start = scope_start;
  bool scoped_enum = scope_start != 0;
  bool merge_assign = scoped_enum ? known_enum_in_scope(name, scope_start)
                                  : (prior_value_decl(src, *io, name) || known_enum_in_scope(name, scope_start));
  if (merge_assign) {
    skim_str_puts(&enum_out, name);
    skim_str_puts(&enum_out, " = function (");
  } else {
    if (is_export) skim_str_puts(&enum_out, "export ");
    skim_str_puts(&enum_out, (is_export || es->enum_expression_depth || scoped_enum) ? "let " : "var ");
    skim_str_puts(&enum_out, name);
    skim_str_puts(&enum_out, " = function (");
  }
  skim_str_puts(&enum_out, name);
  skim_str_puts(&enum_out, ") {\n");

  double next_number = 0;
  char **members = NULL;
  enum_value_t *member_values = NULL;
  size_t member_count = 0;
  while (i < len) {
    i = skim_skip_ws_comments(src, len, i);
    if (i >= len || src[i] == '}') break;

    size_t member_start = i, member_end = i;
    if (src[i] == '\'' || src[i] == '"') {
      char quote = src[i++];
      member_start = i;
      while (i < len && src[i] != quote) {
        if (src[i] == '\\' && i + 1 < len) i += 2;
        else i++;
      }
      member_end = i;
      if (i < len) i++;
    } else if (skim_is_id_start(src[i])) {
      member_start = i++;
      while (i < len && skim_is_id_part(src[i]))
        i++;
      member_end = i;
    } else break;
    char *member = skim_slice_dup(src, member_start, member_end);
    i = skim_skip_ws_comments(src, len, i);

    char init_buf[128];
    const char *init = init_buf;
    bool is_string = false;
    enum_value_t member_value = skim_enum_value_unknown();
    if (i < len && src[i] == '=') {
      size_t init_start = skim_skip_ws_comments(src, len, i + 1);
      size_t init_end = init_start;
      int paren = 0, bracket = 0;
      while (init_end < len) {
        char c = src[init_end];
        if (paren == 0 && bracket == 0 && c == '/' && init_end + 1 < len && src[init_end + 1] == '/') break;
        if (c == '\'' || c == '"' || c == '`') {
          init_end = skim_skip_string_raw(src, len, init_end);
          continue;
        }
        if (c == '(') paren++;
        else if (c == ')' && paren > 0) paren--;
        else if (c == '[') bracket++;
        else if (c == ']' && bracket > 0) bracket--;
        else if (paren == 0 && bracket == 0 && (c == ',' || c == '}')) break;
        init_end++;
      }
      size_t trimmed = init_end;
      while (trimmed > init_start && skim_is_space(src[trimmed - 1]))
        trimmed--;
      char *raw = skim_slice_dup(src, init_start, trimmed);
      member_value = skim_enum_eval_expr(raw, 0, strlen(raw), name, members, member_values, member_count);
      char *literal = skim_enum_value_to_literal(member_value);
      char *transformed = NULL;
      if (!literal && strstr(raw, "enum ")) {
        es->enum_expression_depth++;
        transformed = skim_transform_typescript(raw, strlen(raw));
        es->enum_expression_depth--;
      }
      char *rewritten =
        literal ? literal : (transformed ? transformed : rewrite_member_refs(raw, name, members, member_count));
      init = rewritten;
      is_string = member_value.kind == ENUM_VALUE_STRING;
      if (member_value.kind == ENUM_VALUE_NUMBER) next_number = member_value.number + 1;
      else if (member_value.kind == ENUM_VALUE_STRING) next_number = 0;
      emit_enum_assignment(&enum_out, name, member, init, is_string);
      free(rewritten);
      free(raw);
      i = init_end;
    } else {
      if (member_count > 0 && member_values[member_count - 1].kind != ENUM_VALUE_NUMBER) {
        snprintf(init_buf, sizeof(init_buf), "1 + %s[\"%s\"]", name, members[member_count - 1]);
        member_value = skim_enum_value_unknown();
      } else {
        member_value = skim_enum_value_number(next_number);
        char *num = skim_enum_number_dup(next_number);
        snprintf(init_buf, sizeof(init_buf), "%s", num);
        free(num);
        next_number++;
      }
      emit_enum_assignment(&enum_out, name, member, init, false);
    }
    remember_member(name, member, member_value);

    members = realloc(members, sizeof(char *) * (member_count + 1));
    if (!members) skim_die("out of memory");
    enum_value_t *next_values = realloc(member_values, sizeof(*member_values) * (member_count + 1));
    if (!next_values) skim_die("out of memory");
    member_values = next_values;
    members[member_count] = skim_slice_dup(member, 0, strlen(member));
    member_values[member_count] = skim_enum_value_clone(member_value);
    member_count++;
    skim_enum_value_free(member_value);
    free(member);
    i = skim_skip_ws_comments(src, len, i);
    if (i < len && src[i] == ',') i++;
  }

  skim_str_puts(&enum_out, "  return ");
  skim_str_puts(&enum_out, name);
  skim_str_puts(&enum_out, ";\n}(");
  if (!merge_assign && (is_export || es->enum_expression_depth || scoped_enum)) {
    skim_str_puts(&enum_out, "{}");
  } else {
    skim_str_puts(&enum_out, name);
    skim_str_puts(&enum_out, " || {}");
  }
  skim_str_puts(&enum_out, ");\n");

  while (i < len && src[i] != '}')
    i++;
  if (i < len) i++;
  if (i < len && src[i] == ';') i++;
  bool all_known = member_count > 0;
  for (size_t m = 0; m < member_count; m++) {
    if (member_values[m].kind == ENUM_VALUE_UNKNOWN) all_known = false;
  }
  bool should_remove =
    (is_const &&
     (skim_run->options.optimize_const_enums || (member_count == 0 && !enum_name_has_use(src, len, i, name, false)))) ||
    (skim_run->options.optimize_enums && all_known && !is_export && !enum_name_has_use(src, len, i, name, true));
  if (should_remove) {
    bool after_else = is_const && member_count == 0 && skim_word_before(src, *io, "else");
    if (after_else) trim_trailing_else(out);
    skim_emit_preserved_newlines(out, src, *io, i);
    if (!after_else && is_const && member_count == 0 && skim_prev_non_ws(src, *io) == ')')
      skim_str_puts(out, "{}");
  } else {
    if (skim_run->options.optimize_enums && is_export && !merge_assign) {
      skim_str_t rewritten = {0};
      char *call = strstr(enum_out.data, "}(");
      if (call) {
        size_t before = (size_t)(call - enum_out.data) + 2;
        skim_str_putn(&rewritten, enum_out.data, before);
        skim_str_puts(&rewritten, "{})");
        char *after = strstr(call, ");");
        if (after) skim_str_puts(&rewritten, after + 1);
        free(enum_out.data);
        enum_out = rewritten;
      }
    }
    skim_str_putn(out, enum_out.data, enum_out.len);
  }
  es->current_enum_inline_refs = prev_inline_refs;
  es->current_enum_scope_start = prev_scope_start;
  free(enum_out.data);
  *io = i;
  for (size_t m = 0; m < member_count; m++) {
    free(members[m]);
    skim_enum_value_free(member_values[m]);
  }
  free(members);
  free(member_values);
  free(name);
  return true;
}
