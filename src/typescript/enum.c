#include "skim_internal.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
  ENUM_VALUE_UNKNOWN,
  ENUM_VALUE_NUMBER,
  ENUM_VALUE_STRING,
} enum_value_kind_t;

typedef struct {
  enum_value_kind_t kind;
  double number;
  char *string;
} enum_value_t;

typedef struct {
  char *enum_name;
  char *member_name;
  size_t enum_name_len;
  size_t member_name_len;
  enum_value_t value;
  bool inline_ref;
  size_t scope_start;
  size_t next_hash;
} enum_known_member_t;

static enum_known_member_t *known_members;
static size_t known_member_count;
static size_t known_member_buckets[4096];
static int enum_expression_depth;
static bool current_enum_inline_refs;
static size_t current_enum_scope_start;
static int enum_suppress_depth;

static void value_free(enum_value_t v);

static void clear_known_member_buckets(void) {
  for (size_t i = 0; i < sizeof(known_member_buckets) / sizeof(known_member_buckets[0]); i++) {
    known_member_buckets[i] = (size_t)-1;
  }
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
  return h & ((sizeof(known_member_buckets) / sizeof(known_member_buckets[0])) - 1);
}

void skim_ts_enum_reset(void) {
  for (size_t i = 0; i < known_member_count; i++) {
    free(known_members[i].enum_name);
    free(known_members[i].member_name);
    value_free(known_members[i].value);
  }
  free(known_members);
  known_members = NULL;
  known_member_count = 0;
  clear_known_member_buckets();
  enum_expression_depth = 0;
  current_enum_inline_refs = false;
  current_enum_scope_start = 0;
  enum_suppress_depth = 0;
}

void skim_ts_enum_suppress_push(void) {
  enum_suppress_depth++;
}

void skim_ts_enum_suppress_pop(void) {
  if (enum_suppress_depth > 0) enum_suppress_depth--;
}

static enum_value_t value_unknown(void) {
  enum_value_t v = {ENUM_VALUE_UNKNOWN, 0, NULL};
  return v;
}

static enum_value_t value_number(double n) {
  enum_value_t v = {ENUM_VALUE_NUMBER, n, NULL};
  return v;
}

static enum_value_t value_string_dup(const char *s) {
  enum_value_t v = {ENUM_VALUE_STRING, 0, skim_slice_dup(s, 0, strlen(s))};
  return v;
}

static enum_value_t value_clone(enum_value_t v) {
  if (v.kind == ENUM_VALUE_STRING) return value_string_dup(v.string);
  return v;
}

static void value_free(enum_value_t v) {
  free(v.string);
}

static void js_number_to_str(skim_str_t *out, double n) {
  if (isnan(n)) {
    skim_str_puts(out, "NaN");
    return;
  }
  if (isinf(n)) {
    skim_str_puts(out, n < 0 ? "-Infinity" : "Infinity");
    return;
  }
  double absn = fabs(n);
  char buf[64];
  if (floor(n) == n && absn < 1e21) {
    snprintf(buf, sizeof(buf), "%.0f", n);
  } else {
    snprintf(buf, sizeof(buf), "%.15g", n);
  }
  char *text = buf;
  if (text[0] == '0' && text[1] == '.') {
    text++;
  } else if (text[0] == '-' && text[1] == '0' && text[2] == '.') {
    skim_str_putc(out, '-');
    text += 2;
  }
  skim_str_puts(out, text);
}

static char *js_number_dup(double n) {
  skim_str_t out = {0};
  js_number_to_str(&out, n);
  return out.data ? out.data : skim_slice_dup("", 0, 0);
}

static void emit_js_string(skim_str_t *out, const char *s) {
  skim_str_putc(out, '"');
  for (size_t i = 0; s[i]; i++) {
    if (s[i] == '"' || s[i] == '\\') skim_str_putc(out, '\\');
    skim_str_putc(out, s[i]);
  }
  skim_str_putc(out, '"');
}

static char *js_string_literal_dup(const char *s) {
  skim_str_t out = {0};
  emit_js_string(&out, s);
  return out.data ? out.data : skim_slice_dup("\"\"", 0, 2);
}

static char *value_to_literal(enum_value_t v) {
  if (v.kind == ENUM_VALUE_STRING) return js_string_literal_dup(v.string);
  if (v.kind == ENUM_VALUE_NUMBER) return js_number_dup(v.number);
  return NULL;
}

static char *value_to_template_part(enum_value_t v) {
  if (v.kind == ENUM_VALUE_STRING) return skim_slice_dup(v.string, 0, strlen(v.string));
  if (v.kind == ENUM_VALUE_NUMBER) return js_number_dup(v.number);
  return NULL;
}

static void remember_member(const char *enum_name, const char *member_name, enum_value_t value) {
  if (enum_suppress_depth > 0) return;
  enum_known_member_t *next = realloc(known_members, sizeof(*known_members) * (known_member_count + 1));
  if (!next) skim_die("out of memory");
  known_members = next;
  size_t enum_len = strlen(enum_name);
  size_t member_len = strlen(member_name);
  size_t bucket = enum_member_hash(enum_name, enum_len, member_name, member_len);
  known_members[known_member_count].enum_name = skim_slice_dup(enum_name, 0, enum_len);
  known_members[known_member_count].member_name = skim_slice_dup(member_name, 0, member_len);
  known_members[known_member_count].enum_name_len = enum_len;
  known_members[known_member_count].member_name_len = member_len;
  known_members[known_member_count].value = value_clone(value);
  known_members[known_member_count].inline_ref = current_enum_inline_refs;
  known_members[known_member_count].scope_start = current_enum_scope_start;
  known_members[known_member_count].next_hash = known_member_buckets[bucket];
  known_member_buckets[bucket] = known_member_count;
  known_member_count++;
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

static bool
lookup_local_member(const char *name, char **members, enum_value_t *values, size_t member_count, enum_value_t *out) {
  for (size_t i = member_count; i > 0; i--) {
    if (strcmp(name, members[i - 1]) == 0 && values[i - 1].kind != ENUM_VALUE_UNKNOWN) {
      *out = value_clone(values[i - 1]);
      return true;
    }
  }
  return false;
}

static bool lookup_qualified_member_span(
  const char *enum_name,
  size_t enum_len,
  const char *member_name,
  size_t member_len,
  enum_value_t *out
) {
  for (size_t i = known_member_buckets[enum_member_hash(enum_name, enum_len, member_name, member_len)]; i != (size_t)-1;
       i = known_members[i].next_hash) {
    enum_known_member_t *m = &known_members[i];
    if (m->enum_name_len != enum_len || m->member_name_len != member_len) continue;
    if (memcmp(enum_name, m->enum_name, enum_len) != 0 || memcmp(member_name, m->member_name, member_len) != 0)
      continue;
    if (m->value.kind == ENUM_VALUE_UNKNOWN) return false;
    *out = value_clone(m->value);
    return true;
  }
  return false;
}

static bool lookup_qualified_member(const char *enum_name, const char *member_name, enum_value_t *out) {
  return lookup_qualified_member_span(enum_name, strlen(enum_name), member_name, strlen(member_name), out);
}

static bool lookup_inline_member_span(
  const char *enum_name,
  size_t enum_len,
  const char *member_name,
  size_t member_len,
  enum_value_t *out
) {
  for (size_t i = known_member_buckets[enum_member_hash(enum_name, enum_len, member_name, member_len)]; i != (size_t)-1;
       i = known_members[i].next_hash) {
    enum_known_member_t *m = &known_members[i];
    if (m->enum_name_len != enum_len || m->member_name_len != member_len) continue;
    if (memcmp(enum_name, m->enum_name, enum_len) != 0 || memcmp(member_name, m->member_name, member_len) != 0)
      continue;
    if (!m->inline_ref || m->value.kind == ENUM_VALUE_UNKNOWN) return false;
    *out = value_clone(m->value);
    return true;
  }
  return false;
}

static bool known_enum_member_name(const char *enum_name, const char *member_name) {
  size_t enum_len = strlen(enum_name);
  size_t member_len = strlen(member_name);
  for (size_t i = known_member_buckets[enum_member_hash(enum_name, enum_len, member_name, member_len)]; i != (size_t)-1;
       i = known_members[i].next_hash) {
    enum_known_member_t *m = &known_members[i];
    if (
      m->enum_name_len == enum_len && m->member_name_len == member_len &&
      memcmp(enum_name, m->enum_name, enum_len) == 0 && memcmp(member_name, m->member_name, member_len) == 0
    ) {
      return true;
    }
  }
  return false;
}

static bool enum_name_has_value_use(const char *src, size_t len, size_t start, const char *name) {
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
      size_t before = i;
      while (before > 0 && isspace((unsigned char)src[before - 1]))
        before--;
      size_t word_end = before;
      while (before > 0 && skim_is_id_part(src[before - 1]))
        before--;
      if (word_end > before && word_end - before == 4 && memcmp(src + before, "enum", 4) == 0) {
        i += n;
        continue;
      }
      size_t j = skim_skip_ws(src, len, i + n);
      if (j < len && src[j] == '?') j = skim_skip_ws(src, len, j + 1);
      if (j < len && (src[j] == '.' || src[j] == '[')) {
        i += n;
        continue;
      }
      return true;
    }
    i++;
  }
  return false;
}

static bool enum_name_has_any_use(const char *src, size_t len, size_t start, const char *name) {
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
      size_t before = i;
      while (before > 0 && isspace((unsigned char)src[before - 1]))
        before--;
      size_t word_end = before;
      while (before > 0 && skim_is_id_part(src[before - 1]))
        before--;
      if (word_end > before && word_end - before == 4 && memcmp(src + before, "enum", 4) == 0) {
        i += n;
        continue;
      }
      return true;
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
  if (member_start != member_end) {
    enum_value_t value = value_unknown();
    if (lookup_inline_member_span(src + enum_start, enum_len, src + member_start, member_end - member_start, &value)) {
      char *literal = value_to_literal(value);
      if (literal) {
        if (value.kind == ENUM_VALUE_NUMBER && end < len && src[end] == '.') skim_str_putc(out, '(');
        skim_str_puts(out, literal);
        if (value.kind == ENUM_VALUE_NUMBER && end < len && src[end] == '.') skim_str_putc(out, ')');
        free(literal);
        *io = end;
        replaced = true;
      }
      value_free(value);
    }
  }
  return replaced;
}

static size_t trim_expr_left(const char *s, size_t i, size_t end) {
  while (i < end && isspace((unsigned char)s[i]))
    i++;
  return i;
}

static size_t trim_expr_right(const char *s, size_t start, size_t end) {
  while (end > start && isspace((unsigned char)s[end - 1]))
    end--;
  return end;
}

static bool decode_simple_string(const char *src, size_t start, size_t end, char **out) {
  if (end <= start + 1 || (src[start] != '\'' && src[start] != '"')) return false;
  if (skim_skip_string_raw(src, end, start) != end) return false;
  char quote = src[start];
  if (src[end - 1] != quote) return false;
  skim_str_t s = {0};
  for (size_t i = start + 1; i + 1 < end;) {
    if (src[i] == '\\' && i + 1 < end - 1) i++;
    skim_str_putc(&s, src[i++]);
  }
  *out = s.data ? s.data : skim_slice_dup("", 0, 0);
  return true;
}

static enum_value_t eval_enum_expr(
  const char *expr,
  size_t start,
  size_t end,
  const char *enum_name,
  char **members,
  enum_value_t *values,
  size_t member_count
);

static size_t find_top_level_plus(const char *expr, size_t start, size_t end) {
  int paren = 0, bracket = 0, brace = 0;
  for (size_t i = start; i < end;) {
    if (expr[i] == '\'' || expr[i] == '"' || expr[i] == '`') {
      i = skim_skip_string_raw(expr, end, i);
      continue;
    }
    if (expr[i] == '(') paren++;
    else if (expr[i] == ')' && paren > 0) paren--;
    else if (expr[i] == '[') bracket++;
    else if (expr[i] == ']' && bracket > 0) bracket--;
    else if (expr[i] == '{') brace++;
    else if (expr[i] == '}' && brace > 0) brace--;
    else if (expr[i] == '+' && paren == 0 && bracket == 0 && brace == 0 && i > start) return i;
    i++;
  }
  return end;
}

static size_t find_top_level_token(const char *expr, size_t start, size_t end, const char *token) {
  size_t token_len = strlen(token);
  int paren = 0, bracket = 0, brace = 0;
  for (size_t i = start; i + token_len <= end;) {
    if (expr[i] == '\'' || expr[i] == '"' || expr[i] == '`') {
      i = skim_skip_string_raw(expr, end, i);
      continue;
    }
    if (expr[i] == '(') paren++;
    else if (expr[i] == ')' && paren > 0) paren--;
    else if (expr[i] == '[') bracket++;
    else if (expr[i] == ']' && bracket > 0) bracket--;
    else if (expr[i] == '{') brace++;
    else if (expr[i] == '}' && brace > 0) brace--;
    else if (paren == 0 && bracket == 0 && brace == 0 && i > start && memcmp(expr + i, token, token_len) == 0) return i;
    i++;
  }
  return end;
}

static enum_value_t eval_numeric_binary(
  const char *expr,
  size_t start,
  size_t end,
  const char *token,
  const char *enum_name,
  char **members,
  enum_value_t *values,
  size_t member_count
) {
  size_t op = find_top_level_token(expr, start, end, token);
  if (op >= end) return value_unknown();
  enum_value_t left = eval_enum_expr(expr, start, op, enum_name, members, values, member_count);
  enum_value_t right = eval_enum_expr(expr, op + strlen(token), end, enum_name, members, values, member_count);
  if (left.kind != ENUM_VALUE_NUMBER || right.kind != ENUM_VALUE_NUMBER) return value_unknown();
  if (strcmp(token, "*") == 0) return value_number(left.number * right.number);
  if (strcmp(token, "/") == 0) return value_number(left.number / right.number);
  if (strcmp(token, "%") == 0) return value_number(fmod(left.number, right.number));
  if (strcmp(token, "|") == 0) return value_number((double)((int)left.number | (int)right.number));
  if (strcmp(token, "&") == 0) return value_number((double)((int)left.number & (int)right.number));
  if (strcmp(token, "^") == 0) return value_number((double)((int)left.number ^ (int)right.number));
  if (strcmp(token, "<<") == 0) return value_number((double)((int)left.number << (int)right.number));
  if (strcmp(token, ">>") == 0) return value_number((double)((int)left.number >> (int)right.number));
  return value_unknown();
}

static enum_value_t eval_template_expr(
  const char *expr,
  size_t start,
  size_t end,
  const char *enum_name,
  char **members,
  enum_value_t *values,
  size_t member_count
) {
  if (end <= start + 1 || expr[start] != '`' || expr[end - 1] != '`') return value_unknown();
  if (skim_skip_string_raw(expr, end, start) != end) return value_unknown();
  skim_str_t s = {0};
  for (size_t i = start + 1; i + 1 < end;) {
    if (i + 1 < end && expr[i] == '$' && expr[i + 1] == '{') {
      size_t close = skim_skip_balanced(expr, end, i + 1, '{', '}');
      if (close > end) return value_unknown();
      enum_value_t part = eval_enum_expr(expr, i + 2, close - 1, enum_name, members, values, member_count);
      char *text = value_to_template_part(part);
      if (!text) return value_unknown();
      skim_str_puts(&s, text);
      free(text);
      i = close;
      continue;
    }
    if (expr[i] == '\\' && i + 1 < end - 1) i++;
    skim_str_putc(&s, expr[i++]);
  }
  enum_value_t out = value_string_dup(s.data ? s.data : "");
  free(s.data);
  return out;
}

static enum_value_t eval_enum_expr(
  const char *expr,
  size_t start,
  size_t end,
  const char *enum_name,
  char **members,
  enum_value_t *values,
  size_t member_count
) {
  start = trim_expr_left(expr, start, end);
  end = trim_expr_right(expr, start, end);
  if (start >= end) return value_unknown();

  if (expr[start] == '(' && skim_skip_balanced(expr, end, start, '(', ')') == end) {
    return eval_enum_expr(expr, start + 1, end - 1, enum_name, members, values, member_count);
  }

  size_t plus = find_top_level_plus(expr, start, end);
  if (plus < end) {
    enum_value_t left = eval_enum_expr(expr, start, plus, enum_name, members, values, member_count);
    enum_value_t right = eval_enum_expr(expr, plus + 1, end, enum_name, members, values, member_count);
    if (left.kind == ENUM_VALUE_UNKNOWN || right.kind == ENUM_VALUE_UNKNOWN) return value_unknown();
    if (left.kind == ENUM_VALUE_STRING || right.kind == ENUM_VALUE_STRING) {
      char *a = value_to_template_part(left);
      char *b = value_to_template_part(right);
      skim_str_t joined = {0};
      skim_str_puts(&joined, a ? a : "");
      skim_str_puts(&joined, b ? b : "");
      free(a);
      free(b);
      enum_value_t out = value_string_dup(joined.data ? joined.data : "");
      free(joined.data);
      return out;
    }
    return value_number(left.number + right.number);
  }

  static const char *numeric_ops[] = {"|", "^", "&", "<<", ">>", "*", "/", "%"};
  for (size_t op = 0; op < sizeof(numeric_ops) / sizeof(numeric_ops[0]); op++) {
    enum_value_t v = eval_numeric_binary(expr, start, end, numeric_ops[op], enum_name, members, values, member_count);
    if (v.kind != ENUM_VALUE_UNKNOWN) return v;
  }

  if (expr[start] == '`') return eval_template_expr(expr, start, end, enum_name, members, values, member_count);

  char *decoded = NULL;
  if (decode_simple_string(expr, start, end, &decoded)) {
    enum_value_t out = value_string_dup(decoded);
    free(decoded);
    return out;
  }

  if (expr[start] == '+' || expr[start] == '-' || expr[start] == '~') {
    enum_value_t inner = eval_enum_expr(expr, start + 1, end, enum_name, members, values, member_count);
    if (expr[start] == '+' && inner.kind == ENUM_VALUE_STRING) return inner;
    if (expr[start] == '-' && inner.kind == ENUM_VALUE_STRING) {
      value_free(inner);
      return value_number(NAN);
    }
    if (expr[start] == '~' && inner.kind == ENUM_VALUE_STRING) {
      value_free(inner);
      return value_number(-1);
    }
    if (inner.kind != ENUM_VALUE_NUMBER) return value_unknown();
    double n = inner.number;
    if (expr[start] == '+') return value_number(n);
    if (expr[start] == '-') return value_number(-n);
    return value_number((double)(~(int)n));
  }

  char *tmp = skim_slice_dup(expr, start, end);
  char *stop = NULL;
  double n = strtod(tmp, &stop);
  while (stop && *stop && isspace((unsigned char)*stop))
    stop++;
  if (stop && stop != tmp && *stop == '\0') {
    free(tmp);
    return value_number(n);
  }
  free(tmp);

  if (end - start == 8 && memcmp(expr + start, "Infinity", 8) == 0) return value_number(INFINITY);
  if (end - start == 3 && memcmp(expr + start, "NaN", 3) == 0) return value_number(NAN);

  size_t dot = start;
  while (dot < end && expr[dot] != '.')
    dot++;
  if (dot < end) {
    size_t enum_start = start, enum_end = dot;
    size_t member_start = dot + 1, member_end = end;
    char *qualified_enum = skim_slice_dup(expr, enum_start, enum_end);
    char *qualified_member = skim_slice_dup(expr, member_start, member_end);
    enum_value_t out = value_unknown();
    lookup_qualified_member(qualified_enum, qualified_member, &out);
    free(qualified_enum);
    free(qualified_member);
    return out;
  }

  char *name = skim_slice_dup(expr, start, end);
  enum_value_t out = value_unknown();
  if (!lookup_local_member(name, members, values, member_count, &out)) {
    lookup_qualified_member(enum_name, name, &out);
  }
  free(name);
  return out;
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

static size_t enum_scope_start(const char *src, size_t len, size_t pos) {
  int depth = 0;
  size_t stack[256];
  for (size_t i = 0; i < pos && i < len;) {
    if (src[i] == '\'' || src[i] == '"' || src[i] == '`') {
      i = skim_skip_string_raw(src, len, i);
      continue;
    }
    if (i + 1 < len && src[i] == '/' && src[i + 1] == '/') {
      i += 2;
      while (i < pos && i < len && src[i] != '\n')
        i++;
      continue;
    }
    if (i + 1 < len && src[i] == '/' && src[i + 1] == '*') {
      i += 2;
      while (i + 1 < pos && i + 1 < len && !(src[i] == '*' && src[i + 1] == '/'))
        i++;
      if (i + 1 < len) i += 2;
      continue;
    }
    if (src[i] == '{') {
      if (depth < (int)(sizeof(stack) / sizeof(stack[0]))) stack[depth] = i;
      depth++;
    } else if (src[i] == '}' && depth > 0) depth--;
    i++;
  }
  if (depth <= 0) return 0;
  if (depth > (int)(sizeof(stack) / sizeof(stack[0]))) return pos;
  return stack[depth - 1];
}

static bool known_enum_in_scope(const char *name, size_t scope_start) {
  size_t enum_len = strlen(name);
  for (size_t i = 0; i < known_member_count; i++) {
    if (known_members[i].scope_start != scope_start) continue;
    if (known_members[i].enum_name_len == enum_len && memcmp(known_members[i].enum_name, name, enum_len) == 0)
      return true;
  }
  return false;
}

static bool removed_enum_needs_empty_block(const char *src, size_t pos) {
  while (pos > 0 && isspace((unsigned char)src[pos - 1]))
    pos--;
  if (pos == 0) return false;
  if (src[pos - 1] == ')') return true;
  return false;
}

static bool removed_enum_after_else(const char *src, size_t pos) {
  while (pos > 0 && isspace((unsigned char)src[pos - 1]))
    pos--;
  size_t end = pos;
  while (pos > 0 && skim_is_id_part(src[pos - 1]))
    pos--;
  return end - pos == 4 && memcmp(src + pos, "else", 4) == 0;
}

static void trim_trailing_else(skim_str_t *out) {
  size_t end = out->len;
  while (end > 0 && isspace((unsigned char)out->data[end - 1]))
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

  bool prev_inline_refs = current_enum_inline_refs;
  current_enum_inline_refs = (is_const && skim_options.optimize_const_enums) || skim_options.optimize_enums;
  skim_str_t enum_out = {0};
  size_t scope_start = enum_scope_start(src, len, *io);
  size_t prev_scope_start = current_enum_scope_start;
  current_enum_scope_start = scope_start;
  bool scoped_enum = scope_start != 0;
  bool merge_assign = scoped_enum ? known_enum_in_scope(name, scope_start)
                                  : (prior_value_decl(src, *io, name) || known_enum_in_scope(name, scope_start));
  if (merge_assign) {
    skim_str_puts(&enum_out, name);
    skim_str_puts(&enum_out, " = function (");
  } else {
    if (is_export) skim_str_puts(&enum_out, "export ");
    skim_str_puts(&enum_out, (is_export || enum_expression_depth || scoped_enum) ? "let " : "var ");
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
    enum_value_t member_value = value_unknown();
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
      while (trimmed > init_start && isspace((unsigned char)src[trimmed - 1]))
        trimmed--;
      char *raw = skim_slice_dup(src, init_start, trimmed);
      member_value = eval_enum_expr(raw, 0, strlen(raw), name, members, member_values, member_count);
      char *literal = value_to_literal(member_value);
      char *transformed = NULL;
      if (!literal && strstr(raw, "enum ")) {
        enum_expression_depth++;
        transformed = skim_transform_typescript(raw, strlen(raw));
        enum_expression_depth--;
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
        member_value = value_unknown();
      } else {
        member_value = value_number(next_number);
        char *num = js_number_dup(next_number);
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
    member_values[member_count] = value_clone(member_value);
    member_count++;
    value_free(member_value);
    free(member);
    i = skim_skip_ws_comments(src, len, i);
    if (i < len && src[i] == ',') i++;
  }

  skim_str_puts(&enum_out, "  return ");
  skim_str_puts(&enum_out, name);
  skim_str_puts(&enum_out, ";\n}(");
  if (!merge_assign && (is_export || enum_expression_depth || scoped_enum)) {
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
     (skim_options.optimize_const_enums || (member_count == 0 && !enum_name_has_any_use(src, len, i, name)))) ||
    (skim_options.optimize_enums && all_known && !is_export && !enum_name_has_value_use(src, len, i, name));
  if (should_remove) {
    bool after_else = is_const && member_count == 0 && removed_enum_after_else(src, *io);
    if (after_else) trim_trailing_else(out);
    skim_emit_preserved_newlines(out, src, *io, i);
    if (!after_else && is_const && member_count == 0 && removed_enum_needs_empty_block(src, *io))
      skim_str_puts(out, "{}");
  } else {
    if (skim_options.optimize_enums && is_export && !merge_assign) {
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
  current_enum_inline_refs = prev_inline_refs;
  current_enum_scope_start = prev_scope_start;
  free(enum_out.data);
  *io = i;
  for (size_t m = 0; m < member_count; m++) {
    free(members[m]);
    value_free(member_values[m]);
  }
  free(members);
  free(member_values);
  free(name);
  return true;
}
