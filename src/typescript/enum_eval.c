#include "chars.h"
#include "enum_internal.h"
#include "skip.h"
#include "str.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum_value_t skim_enum_value_unknown(void) {
  enum_value_t v = {ENUM_VALUE_UNKNOWN, 0, NULL};
  return v;
}

enum_value_t skim_enum_value_number(double n) {
  enum_value_t v = {ENUM_VALUE_NUMBER, n, NULL};
  return v;
}

enum_value_t skim_enum_value_string_dup(const char *s) {
  enum_value_t v = {ENUM_VALUE_STRING, 0, skim_slice_dup(s, 0, strlen(s))};
  return v;
}

enum_value_t skim_enum_value_clone(enum_value_t v) {
  if (v.kind == ENUM_VALUE_STRING) return skim_enum_value_string_dup(v.string);
  return v;
}

void skim_enum_value_free(enum_value_t v) {
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

char *skim_enum_number_dup(double n) {
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

char *skim_enum_value_to_literal(enum_value_t v) {
  if (v.kind == ENUM_VALUE_STRING) return js_string_literal_dup(v.string);
  if (v.kind == ENUM_VALUE_NUMBER) return skim_enum_number_dup(v.number);
  return NULL;
}

static char *value_to_template_part(enum_value_t v) {
  if (v.kind == ENUM_VALUE_STRING) return skim_slice_dup(v.string, 0, strlen(v.string));
  if (v.kind == ENUM_VALUE_NUMBER) return skim_enum_number_dup(v.number);
  return NULL;
}

static bool
lookup_local_member(const char *name, char **members, enum_value_t *values, size_t member_count, enum_value_t *out) {
  for (size_t i = member_count; i > 0; i--) {
    if (strcmp(name, members[i - 1]) == 0 && values[i - 1].kind != ENUM_VALUE_UNKNOWN) {
      *out = skim_enum_value_clone(values[i - 1]);
      return true;
    }
  }
  return false;
}

static size_t trim_expr_left(const char *s, size_t i, size_t end) {
  while (i < end && skim_is_space(s[i]))
    i++;
  return i;
}

static size_t trim_expr_right(const char *s, size_t start, size_t end) {
  while (end > start && skim_is_space(s[end - 1]))
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

enum_value_t skim_enum_eval_expr(
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

static int32_t js_to_int32(double n) {
  if (!isfinite(n)) return 0;
  double wrapped = fmod(trunc(n), 4294967296.0);
  if (wrapped < 0) wrapped += 4294967296.0;
  return (int32_t)(uint32_t)wrapped;
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
  if (op >= end) return skim_enum_value_unknown();
  enum_value_t left = skim_enum_eval_expr(expr, start, op, enum_name, members, values, member_count);
  enum_value_t right = skim_enum_eval_expr(expr, op + strlen(token), end, enum_name, members, values, member_count);
  if (left.kind != ENUM_VALUE_NUMBER || right.kind != ENUM_VALUE_NUMBER) return skim_enum_value_unknown();
  if (strcmp(token, "*") == 0) return skim_enum_value_number(left.number * right.number);
  if (strcmp(token, "/") == 0) return skim_enum_value_number(left.number / right.number);
  if (strcmp(token, "%") == 0) return skim_enum_value_number(fmod(left.number, right.number));
  int32_t a = js_to_int32(left.number);
  int32_t b = js_to_int32(right.number);
  if (strcmp(token, "|") == 0) return skim_enum_value_number((double)(a | b));
  if (strcmp(token, "&") == 0) return skim_enum_value_number((double)(a & b));
  if (strcmp(token, "^") == 0) return skim_enum_value_number((double)(a ^ b));
  if (strcmp(token, "<<") == 0) return skim_enum_value_number((double)(int32_t)((uint32_t)a << ((uint32_t)b & 31u)));
  if (strcmp(token, ">>") == 0) return skim_enum_value_number((double)(a >> ((uint32_t)b & 31u)));
  return skim_enum_value_unknown();
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
  if (end <= start + 1 || expr[start] != '`' || expr[end - 1] != '`') return skim_enum_value_unknown();
  if (skim_skip_string_raw(expr, end, start) != end) return skim_enum_value_unknown();
  skim_str_t s = {0};
  for (size_t i = start + 1; i + 1 < end;) {
    if (i + 1 < end && expr[i] == '$' && expr[i + 1] == '{') {
      size_t close = skim_skip_balanced(expr, end, i + 1, '{', '}');
      if (close > end) return skim_enum_value_unknown();
      enum_value_t part = skim_enum_eval_expr(expr, i + 2, close - 1, enum_name, members, values, member_count);
      char *text = value_to_template_part(part);
      if (!text) return skim_enum_value_unknown();
      skim_str_puts(&s, text);
      free(text);
      i = close;
      continue;
    }
    if (expr[i] == '\\' && i + 1 < end - 1) i++;
    skim_str_putc(&s, expr[i++]);
  }
  enum_value_t out = skim_enum_value_string_dup(s.data ? s.data : "");
  free(s.data);
  return out;
}

enum_value_t skim_enum_eval_expr(
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
  if (start >= end) return skim_enum_value_unknown();

  if (expr[start] == '(' && skim_skip_balanced(expr, end, start, '(', ')') == end) {
    return skim_enum_eval_expr(expr, start + 1, end - 1, enum_name, members, values, member_count);
  }

  size_t plus = find_top_level_plus(expr, start, end);
  if (plus < end) {
    enum_value_t left = skim_enum_eval_expr(expr, start, plus, enum_name, members, values, member_count);
    enum_value_t right = skim_enum_eval_expr(expr, plus + 1, end, enum_name, members, values, member_count);
    if (left.kind == ENUM_VALUE_UNKNOWN || right.kind == ENUM_VALUE_UNKNOWN) return skim_enum_value_unknown();
    if (left.kind == ENUM_VALUE_STRING || right.kind == ENUM_VALUE_STRING) {
      char *a = value_to_template_part(left);
      char *b = value_to_template_part(right);
      skim_str_t joined = {0};
      skim_str_puts(&joined, a ? a : "");
      skim_str_puts(&joined, b ? b : "");
      free(a);
      free(b);
      enum_value_t out = skim_enum_value_string_dup(joined.data ? joined.data : "");
      free(joined.data);
      return out;
    }
    return skim_enum_value_number(left.number + right.number);
  }

  static const char *numeric_ops[] = {"|", "^", "&", "<<", ">>", "*", "/", "%"};
  for (size_t op = 0; op < sizeof(numeric_ops) / sizeof(numeric_ops[0]); op++) {
    enum_value_t v = eval_numeric_binary(expr, start, end, numeric_ops[op], enum_name, members, values, member_count);
    if (v.kind != ENUM_VALUE_UNKNOWN) return v;
  }

  if (expr[start] == '`') return eval_template_expr(expr, start, end, enum_name, members, values, member_count);

  char *decoded = NULL;
  if (decode_simple_string(expr, start, end, &decoded)) {
    enum_value_t out = skim_enum_value_string_dup(decoded);
    free(decoded);
    return out;
  }

  if (expr[start] == '+' || expr[start] == '-' || expr[start] == '~') {
    enum_value_t inner = skim_enum_eval_expr(expr, start + 1, end, enum_name, members, values, member_count);
    if (expr[start] == '+' && inner.kind == ENUM_VALUE_STRING) return inner;
    if (expr[start] == '-' && inner.kind == ENUM_VALUE_STRING) {
      skim_enum_value_free(inner);
      return skim_enum_value_number(NAN);
    }
    if (expr[start] == '~' && inner.kind == ENUM_VALUE_STRING) {
      skim_enum_value_free(inner);
      return skim_enum_value_number(-1);
    }
    if (inner.kind != ENUM_VALUE_NUMBER) return skim_enum_value_unknown();
    double n = inner.number;
    if (expr[start] == '+') return skim_enum_value_number(n);
    if (expr[start] == '-') return skim_enum_value_number(-n);
    return skim_enum_value_number((double)(~js_to_int32(n)));
  }

  char *tmp = skim_slice_dup(expr, start, end);
  char *stop = NULL;
  double n = strtod(tmp, &stop);
  while (stop && *stop && skim_is_space(*stop))
    stop++;
  if (stop && stop != tmp && *stop == '\0') {
    free(tmp);
    return skim_enum_value_number(n);
  }
  free(tmp);

  if (end - start == 8 && memcmp(expr + start, "Infinity", 8) == 0) return skim_enum_value_number(INFINITY);
  if (end - start == 3 && memcmp(expr + start, "NaN", 3) == 0) return skim_enum_value_number(NAN);

  size_t dot = start;
  while (dot < end && expr[dot] != '.')
    dot++;
  if (dot < end) {
    size_t enum_start = start, enum_end = dot;
    size_t member_start = dot + 1, member_end = end;
    char *qualified_enum = skim_slice_dup(expr, enum_start, enum_end);
    char *qualified_member = skim_slice_dup(expr, member_start, member_end);
    enum_value_t out = skim_enum_value_unknown();
    skim_enum_lookup_qualified_member(qualified_enum, qualified_member, &out);
    free(qualified_enum);
    free(qualified_member);
    return out;
  }

  char *name = skim_slice_dup(expr, start, end);
  enum_value_t out = skim_enum_value_unknown();
  if (!lookup_local_member(name, members, values, member_count, &out)) {
    skim_enum_lookup_qualified_member(enum_name, name, &out);
  }
  free(name);
  return out;
}
