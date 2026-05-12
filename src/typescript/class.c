#include "skim_internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  char *name;
  char *param;
  bool is_param_property;
} param_t;

typedef struct {
  bool is_export;
  bool is_default;
  bool has_decorators;
  size_t decorators_start;
  size_t decorators_end;
} class_prefix_t;

static bool modifier_at(const char *src, size_t len, size_t i, const char **word, size_t *word_len) {
  static const char *mods[] = {"public", "private", "protected", "readonly", "override"};
  for (size_t k = 0; k < sizeof(mods) / sizeof(mods[0]); k++) {
    if (skim_word_at(src, len, i, mods[k])) {
      *word = mods[k];
      *word_len = strlen(mods[k]);
      return true;
    }
  }
  return false;
}

static bool class_member_modifier_at(
  const char *src,
  size_t len,
  size_t i,
  const char **word,
  size_t *word_len,
  bool *is_abstract
) {
  static const char *mods[] = {
    "public", "private", "protected", "readonly", "override", "static", "accessor", "declare", "abstract"
  };
  for (size_t k = 0; k < sizeof(mods) / sizeof(mods[0]); k++) {
    if (skim_word_at(src, len, i, mods[k])) {
      *word = mods[k];
      *word_len = strlen(mods[k]);
      *is_abstract = strcmp(mods[k], "abstract") == 0;
      return true;
    }
  }
  return false;
}

static size_t skip_class_type_tail(const char *src, size_t len, size_t i) {
  int paren = 0, bracket = 0, brace = 0, angle = 0;
  bool seen_type_token = false;
  while (i < len) {
    char c = src[i];
    if ((c == '\n' || c == '\r') && paren == 0 && bracket == 0 && brace == 0 && angle == 0 && seen_type_token) break;
    if (isspace((unsigned char)c)) {
      i++;
      continue;
    }
    if (c == '\'' || c == '"' || c == '`') {
      seen_type_token = true;
      i = skim_skip_string_raw(src, len, i);
      continue;
    }
    if (i + 1 < len && c == '/' && src[i + 1] == '/') {
      if (paren == 0 && bracket == 0 && brace == 0 && angle == 0) break;
      i += 2;
      while (i < len && src[i] != '\n')
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
    if (c == '(') paren++;
    else if (c == ')' && paren-- <= 0) break;
    else if (c == '[') bracket++;
    else if (c == ']' && bracket-- <= 0) break;
    else if (c == '{') {
      if (paren == 0 && bracket == 0 && angle == 0 && brace == 0 && seen_type_token) break;
      brace++;
    } else if (c == '}' && brace-- <= 0) break;
    else if (c == '<') angle++;
    else if (c == '>' && angle > 0) angle--;
    else if (c == '=' && i + 1 < len && src[i + 1] == '>') {
      i += 2;
      seen_type_token = true;
      continue;
    } else if (paren == 0 && bracket == 0 && brace == 0 && angle == 0) {
      if (c == ';' || c == '=') break;
    }
    seen_type_token = true;
    i++;
  }
  return i;
}

static size_t skip_class_index_signature_tail(const char *src, size_t len, size_t i) {
  int paren = 0, bracket = 0, brace = 0, angle = 0;
  bool seen_type_token = false;
  while (i < len) {
    char c = src[i];
    if (paren == 0 && bracket == 0 && brace == 0 && angle == 0 && seen_type_token) {
      const char *mod = NULL;
      size_t mod_len = 0;
      bool is_abstract = false;
      if (class_member_modifier_at(src, len, i, &mod, &mod_len, &is_abstract)) return i;
    }
    if ((c == '\n' || c == '\r') && paren == 0 && bracket == 0 && brace == 0 && angle == 0 && seen_type_token) return i;
    if (c == '\'' || c == '"' || c == '`') {
      seen_type_token = true;
      i = skim_skip_string_raw(src, len, i);
      continue;
    }
    if (i + 1 < len && c == '/' && src[i + 1] == '/') {
      if (paren == 0 && bracket == 0 && brace == 0 && angle == 0) return i;
      i += 2;
      while (i < len && src[i] != '\n')
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
    if (c == '(') paren++;
    else if (c == ')' && paren > 0) paren--;
    else if (c == '[') bracket++;
    else if (c == ']' && bracket > 0) bracket--;
    else if (c == '{') brace++;
    else if (c == '}' && brace > 0) brace--;
    else if (c == '<') angle++;
    else if (c == '>' && angle > 0) angle--;
    else if (c == ';' && paren == 0 && bracket == 0 && brace == 0 && angle == 0) return i + 1;
    if (!isspace((unsigned char)c)) seen_type_token = true;
    i++;
  }
  return i;
}

static size_t skip_class_member_name(const char *src, size_t len, size_t i) {
  i = skim_skip_ws_comments(src, len, i);
  if (i >= len) return i;
  if (src[i] == '[') return skim_skip_balanced(src, len, i, '[', ']');
  if (src[i] == '\'' || src[i] == '"' || src[i] == '`') return skim_skip_string_raw(src, len, i);
  if (isdigit((unsigned char)src[i])) {
    while (i < len && (isdigit((unsigned char)src[i]) || src[i] == '.'))
      i++;
    return i;
  }
  if (src[i] == '#') i++;
  size_t start = 0, end = 0;
  size_t after = skim_parse_identifier(src, len, i, &start, &end);
  return start != end ? after : i;
}

static bool bracket_contains_type_colon(const char *src, size_t len, size_t i) {
  if (i >= len || src[i] != '[') return false;
  int depth = 0;
  int paren = 0, brace = 0;
  int ternary = 0;
  for (size_t j = i; j < len; j++) {
    if (src[j] == '\'' || src[j] == '"' || src[j] == '`') {
      j = skim_skip_string_raw(src, len, j) - 1;
      continue;
    }
    if (src[j] == '(') paren++;
    else if (src[j] == ')' && paren > 0) paren--;
    else if (src[j] == '{') brace++;
    else if (src[j] == '}' && brace > 0) brace--;
    else if (src[j] == '[') depth++;
    else if (src[j] == ']') {
      depth--;
      if (depth == 0) return false;
    } else if (src[j] == '?' && depth == 1 && paren == 0 && brace == 0) {
      ternary++;
    } else if (src[j] == ':' && depth == 1 && paren == 0 && brace == 0) {
      if (ternary > 0) {
        ternary--;
        continue;
      }
      return true;
    }
  }
  return false;
}

static bool range_has_newline(const char *src, size_t start, size_t end) {
  for (size_t i = start; i < end; i++) {
    if (src[i] == '\n' || src[i] == '\r') return true;
  }
  return false;
}

static size_t skip_class_angle_type_list(const char *src, size_t len, size_t i) {
  int depth = 0;
  int paren = 0, bracket = 0, brace = 0;
  while (i < len) {
    if (src[i] == '\'' || src[i] == '"' || src[i] == '`') {
      i = skim_skip_string_raw(src, len, i);
      continue;
    }
    if (src[i] == '(') paren++;
    else if (src[i] == ')' && paren > 0) paren--;
    else if (src[i] == '[') bracket++;
    else if (src[i] == ']' && bracket > 0) bracket--;
    else if (src[i] == '{') brace++;
    else if (src[i] == '}' && brace > 0) brace--;
    else if (src[i] == '=' && i + 1 < len && src[i + 1] == '>') i++;
    else if (src[i] == '<') depth++;
    else if (src[i] == '>' && depth > 0) {
      if (depth > 1) {
        depth--;
      } else if (paren == 0 && bracket == 0 && brace == 0) {
        depth--;
        return i + 1;
      }
    }
    i++;
  }
  return i;
}

static size_t skip_decorator_line(const char *src, size_t len, size_t i) {
  if (i >= len || src[i] != '@') return i;
  i++;
  i = skim_skip_ws(src, len, i);
  if (i < len && src[i] == '(') {
    i = skim_skip_balanced(src, len, i, '(', ')');
    size_t call = skim_skip_ws(src, len, i);
    if (call < len && src[call] == '(') i = skim_skip_balanced(src, len, call, '(', ')');
    return i;
  }

  size_t start = 0, end = 0;
  i = skim_parse_identifier(src, len, i, &start, &end);
  if (start == end) return i;

  for (;;) {
    size_t j = skim_skip_ws(src, len, i);
    if (range_has_newline(src, i, j)) break;
    if (j >= len) break;
    if (src[j] == '<') {
      i = skip_class_angle_type_list(src, len, j);
      continue;
    }
    if (src[j] == '!') {
      i = j + 1;
      continue;
    }
    if (src[j] == '.') {
      size_t next_start = 0, next_end = 0;
      size_t next = skim_parse_identifier(src, len, j + 1, &next_start, &next_end);
      if (next_start == next_end) break;
      i = next;
      continue;
    }
    if (j + 1 < len && src[j] == '?' && src[j + 1] == '.') {
      size_t next = j + 2;
      if (next < len && src[next] == '[') {
        i = skim_skip_balanced(src, len, next, '[', ']');
        continue;
      }
      size_t next_start = 0, next_end = 0;
      next = skim_parse_identifier(src, len, next, &next_start, &next_end);
      if (next_start == next_end) break;
      i = next;
      continue;
    }
    if (src[j] == '[') {
      i = skim_skip_balanced(src, len, j, '[', ']');
      continue;
    }
    if (src[j] == '(') {
      i = skim_skip_balanced(src, len, j, '(', ')');
      continue;
    }
    break;
  }
  return i;
}

static bool looks_like_class_member_start(const char *src, size_t i) {
  while (i > 0 && (src[i - 1] == ' ' || src[i - 1] == '\t'))
    i--;
  if (i == 0) return true;
  char c = src[i - 1];
  return c == '{' || c == '}' || c == ';' || c == '\n' || c == '\r';
}

static bool class_modifier_word_used_as_method_name(const char *src, size_t len, size_t i) {
  static const char *words[] = {"public", "private", "protected", "readonly", "override", "accessor"};
  for (size_t w = 0; w < sizeof(words) / sizeof(words[0]); w++) {
    if (!skim_word_at(src, len, i, words[w])) continue;
    size_t j = skim_skip_ws_comments(src, len, i + strlen(words[w]));
    return j < len && src[j] == '(';
  }
  return false;
}

static bool class_modifier_word_used_as_field_name(const char *src, size_t len, size_t i, size_t end) {
  static const char *words[] = {"public", "private", "protected", "readonly", "override", "accessor"};
  if (!looks_like_class_member_start(src, i)) return false;
  for (size_t w = 0; w < sizeof(words) / sizeof(words[0]); w++) {
    if (!skim_word_at(src, len, i, words[w])) continue;
    size_t j = i + strlen(words[w]);
    bool saw_newline = false;
    while (j < end && isspace((unsigned char)src[j])) {
      saw_newline = saw_newline || src[j] == '\n' || src[j] == '\r';
      j++;
    }
    return j >= end || saw_newline || src[j] == '}' || src[j] == ';' || src[j] == '=';
  }
  return false;
}

static bool class_keyword_used_as_field_name(const char *src, size_t len, size_t i) {
  static const char *words[] = {
    "abstract",
    "class",
    "const",
    "declare",
    "enum",
    "export",
    "function",
    "implements",
    "import",
    "interface",
    "module",
    "namespace",
    "type"
  };
  if (!looks_like_class_member_start(src, i)) return false;
  for (size_t w = 0; w < sizeof(words) / sizeof(words[0]); w++) {
    if (!skim_word_at(src, len, i, words[w])) continue;
    size_t j = skim_skip_ws_comments(src, len, i + strlen(words[w]));
    return j < len && (src[j] == ';' || src[j] == '=');
  }
  return false;
}

static char out_prev_non_ws(const skim_str_t *out) {
  size_t i = out->len;
  while (i > 0 && isspace((unsigned char)out->data[i - 1]))
    i--;
  return i > 0 ? out->data[i - 1] : '\0';
}

static bool class_semicolon_is_empty_element(const skim_str_t *out) {
  char prev = out_prev_non_ws(out);
  return prev == '\0' || prev == '{' || prev == ';';
}

static bool class_prev_needs_member_separator(const skim_str_t *out) {
  char prev = out_prev_non_ws(out);
  return prev != '\0' && prev != '{' && prev != '}' && prev != ';';
}

static bool decimal_number_literal_at(const char *src, size_t len, size_t i) {
  if (i >= len || !isdigit((unsigned char)src[i])) return false;
  if (i > 0 && (skim_is_id_part(src[i - 1]) || src[i - 1] == '.')) return false;
  return true;
}

static size_t count_digits_stripped(const char *src, size_t start, size_t end) {
  size_t count = 0;
  for (size_t i = start; i < end; i++) {
    if (isdigit((unsigned char)src[i])) count++;
  }
  return count;
}

static void put_digits_stripped(skim_str_t *out, const char *src, size_t start, size_t end) {
  for (size_t i = start; i < end; i++) {
    if (isdigit((unsigned char)src[i])) skim_str_putc(out, src[i]);
  }
}

static void put_n_digits_stripped(skim_str_t *out, const char *src, size_t start, size_t end, size_t n) {
  for (size_t i = start; i < end && n > 0; i++) {
    if (isdigit((unsigned char)src[i])) {
      skim_str_putc(out, src[i]);
      n--;
    }
  }
}

static size_t trailing_zero_digits_stripped(const char *src, size_t start, size_t end) {
  size_t count = 0;
  for (size_t i = end; i > start;) {
    i--;
    if (src[i] == '_') continue;
    if (src[i] != '0') break;
    count++;
  }
  return count;
}

static bool stripped_int_is_zero(const char *src, size_t start, size_t end) {
  bool saw_digit = false;
  for (size_t i = start; i < end; i++) {
    if (!isdigit((unsigned char)src[i])) continue;
    saw_digit = true;
    if (src[i] != '0') return false;
  }
  return saw_digit;
}

static bool stripped_digits_all_zero(const char *src, size_t start, size_t end) {
  for (size_t i = start; i < end; i++) {
    if (isdigit((unsigned char)src[i]) && src[i] != '0') return false;
  }
  return true;
}

static long long parse_exponent_digits(const char *src, size_t start, size_t end, bool negative) {
  long long value = 0;
  for (size_t i = start; i < end; i++) {
    if (!isdigit((unsigned char)src[i])) continue;
    if (value < 1000000000ll) value = value * 10 + (long long)(src[i] - '0');
  }
  return negative ? -value : value;
}

static void put_js_scientific_number(skim_str_t *out, long double value) {
  char buf[128];
  snprintf(buf, sizeof(buf), "%.15Le", value);
  char digits[32];
  size_t digit_count = 0;
  size_t e = 0;
  for (; buf[e] && buf[e] != 'e' && buf[e] != 'E'; e++) {
    if (isdigit((unsigned char)buf[e])) digits[digit_count++] = buf[e];
  }
  while (digit_count > 1 && digits[digit_count - 1] == '0')
    digit_count--;
  int exp_value = 0;
  int exp_sign = 1;
  if (buf[e]) {
    e++;
    if (buf[e] == '+') e++;
    else if (buf[e] == '-') {
      exp_sign = -1;
      e++;
    }
    while (buf[e]) {
      if (isdigit((unsigned char)buf[e])) exp_value = exp_value * 10 + (buf[e] - '0');
      e++;
    }
  }
  int compact_exp = exp_sign * exp_value - (int)(digit_count - 1);
  for (size_t k = 0; k < digit_count; k++)
    skim_str_putc(out, digits[k]);
  if (compact_exp != 0) {
    char exp_buf[32];
    snprintf(exp_buf, sizeof(exp_buf), "e%d", compact_exp);
    skim_str_puts(out, exp_buf);
  }
}

static size_t copy_decimal_number_literal(skim_str_t *out, const char *src, size_t len, size_t i) {
  size_t j = i;
  while (j < len && (isdigit((unsigned char)src[j]) || src[j] == '_'))
    j++;
  size_t int_end = j;
  if (j < len && src[j] == 'n') {
    put_digits_stripped(out, src, i, j);
    skim_str_putc(out, 'n');
    return j + 1;
  }

  bool has_fraction = false;
  size_t frac_start = j;
  size_t frac_end = j;
  if (j < len && src[j] == '.' && !(j + 1 < len && src[j + 1] == '.')) {
    has_fraction = true;
    frac_start = j + 1;
    frac_end = frac_start;
    while (frac_end < len && (isdigit((unsigned char)src[frac_end]) || src[frac_end] == '_'))
      frac_end++;
    j = frac_end;
  }

  bool has_exp = false;
  bool exp_negative = false;
  size_t exp_start = j;
  size_t exp_end = j;
  if (j < len && (src[j] == 'e' || src[j] == 'E')) {
    size_t exp = j + 1;
    if (exp < len && (src[exp] == '+' || src[exp] == '-')) {
      exp_negative = src[exp] == '-';
      exp++;
    }
    exp_start = exp;
    while (exp < len && (isdigit((unsigned char)src[exp]) || src[exp] == '_'))
      exp++;
    if (count_digits_stripped(src, exp_start, exp) > 0) {
      has_exp = true;
      exp_end = exp;
      j = exp;
    }
  }

  if (has_fraction || has_exp) {
    size_t frac_digits = count_digits_stripped(src, frac_start, frac_end);
    if (has_exp) {
      long long exponent = parse_exponent_digits(src, exp_start, exp_end, exp_negative);
      exponent -= (long long)frac_digits;
      size_t int_digits = count_digits_stripped(src, i, int_end);
      if (
        !has_fraction && exponent > 0 && exponent <= 8 &&
        int_digits + (size_t)exponent < int_digits + 2 + count_digits_stripped(src, exp_start, exp_end)
      ) {
        put_digits_stripped(out, src, i, int_end);
        for (long long z = 0; z < exponent; z++)
          skim_str_putc(out, '0');
        return j;
      }
      put_digits_stripped(out, src, i, has_fraction ? frac_end : int_end);
      if (exponent != 0) {
        char buf[48];
        snprintf(buf, sizeof(buf), "e%lld", exponent);
        skim_str_puts(out, buf);
      }
      return j;
    }
    if (stripped_digits_all_zero(src, frac_start, frac_end)) {
      put_digits_stripped(out, src, i, frac_start - 1);
      return j;
    }
    if (stripped_int_is_zero(src, i, frac_start - 1) && frac_digits > 0) {
      skim_str_putc(out, '.');
      put_digits_stripped(out, src, frac_start, frac_end);
    } else {
      put_digits_stripped(out, src, i, frac_start - 1);
      skim_str_putc(out, '.');
      put_digits_stripped(out, src, frac_start, frac_end);
    }
    return j;
  }

  size_t int_len = count_digits_stripped(src, i, j);
  size_t exponent = trailing_zero_digits_stripped(src, i, j);
  if (exponent > 0 && exponent < int_len) {
    size_t mantissa_len = int_len - exponent;
    size_t exp_digits = 1;
    for (size_t n = exponent; n >= 10; n /= 10)
      exp_digits++;
    if (mantissa_len + 1 + exp_digits < int_len) {
      put_n_digits_stripped(out, src, i, j, mantissa_len);
      skim_str_putc(out, 'e');
      char buf[32];
      snprintf(buf, sizeof(buf), "%zu", exponent);
      skim_str_puts(out, buf);
      return j;
    }
  }
  put_digits_stripped(out, src, i, j);
  return j;
}

static bool based_number_literal_at(const char *src, size_t len, size_t i) {
  if (i + 2 >= len || src[i] != '0') return false;
  if (i > 0 && (skim_is_id_part(src[i - 1]) || src[i - 1] == '.')) return false;
  return src[i + 1] == 'b' || src[i + 1] == 'B' || src[i + 1] == 'o' || src[i + 1] == 'O' || src[i + 1] == 'x' ||
         src[i + 1] == 'X';
}

static size_t copy_based_number_literal(skim_str_t *out, const char *src, size_t len, size_t i) {
  int base = (src[i + 1] == 'b' || src[i + 1] == 'B') ? 2 : (src[i + 1] == 'o' || src[i + 1] == 'O') ? 8 : 16;
  size_t j = i + 2;
  unsigned long long value = 0;
  long double approx = 0.0L;
  bool ok = true;
  bool saw_digit = false;
  for (; j < len; j++) {
    if (src[j] == '_') continue;
    int digit = -1;
    if (src[j] >= '0' && src[j] <= '9') digit = src[j] - '0';
    else if (src[j] >= 'a' && src[j] <= 'f') digit = src[j] - 'a' + 10;
    else if (src[j] >= 'A' && src[j] <= 'F') digit = src[j] - 'A' + 10;
    else break;
    if (digit >= base) break;
    saw_digit = true;
    approx = approx * (long double)base + (long double)digit;
    if (value > (~0ull - (unsigned long long)digit) / (unsigned long long)base) ok = false;
    if (ok) value = value * (unsigned long long)base + (unsigned long long)digit;
  }
  bool bigint = j < len && src[j] == 'n';
  if (!saw_digit) {
    skim_str_putn(out, src + i, j - i);
    if (bigint) {
      skim_str_putc(out, 'n');
      j++;
    }
    return j;
  }
  if (!ok) {
    if (bigint) {
      skim_str_putn(out, src + i, j + 1 - i);
      return j + 1;
    }
    if (approx > 1.7976931348623157e308L) {
      size_t next = skim_skip_ws(src, len, j);
      if (next < len && src[next] == ':') skim_str_puts(out, "[Infinity]");
      else skim_str_puts(out, "Infinity");
    } else {
      put_js_scientific_number(out, approx);
    }
    return j;
  }
  char buf[32];
  snprintf(buf, sizeof(buf), "%llu", value);
  skim_str_puts(out, buf);
  if (bigint) {
    skim_str_putc(out, 'n');
    j++;
  }
  return j;
}

static bool next_looks_like_class_member(const char *src, size_t len, size_t i, size_t end) {
  i = skim_skip_ws_comments(src, len, i);
  if (i >= end) return false;
  if (
    src[i] == '@' || src[i] == '[' || src[i] == '#' || src[i] == '*' || src[i] == '\'' || src[i] == '"' ||
    src[i] == '`' || isdigit((unsigned char)src[i])
  )
    return true;
  if (!skim_is_id_start(src[i])) return false;
  size_t name_start = 0, name_end = 0;
  size_t after = skim_parse_identifier(src, len, i, &name_start, &name_end);
  if (name_start == name_end) return false;
  after = skim_skip_ws_comments(src, len, after);
  return after < len && (after < end || src[after] == '}');
}

static void emit_decorator_range(skim_str_t *out, const char *src, size_t len, size_t start, size_t end) {
  for (size_t i = start; i < end;) {
    if (src[i] == '\'' || src[i] == '"' || src[i] == '`') {
      i = skim_copy_string(out, src, len, i);
      continue;
    }
    if (i + 1 < len && src[i] == '/' && src[i + 1] == '/') {
      i = skim_copy_line_comment(out, src, len, i);
      continue;
    }
    if (i + 1 < len && src[i] == '/' && src[i + 1] == '*') {
      i = skim_copy_block_comment(out, src, len, i);
      continue;
    }
    if (src[i] == '<') {
      size_t after = skip_class_angle_type_list(src, len, i);
      if (after > i && after <= end) {
        i = after;
        continue;
      }
    }
    if (src[i] == '!' && (i + 1 >= len || src[i + 1] != '=')) {
      i++;
      continue;
    }
    if (src[i] != '@' && skim_transform_try_at(out, src, len, &i)) continue;
    skim_str_putc(out, src[i++]);
  }
}

static void emit_preserved_class_member_prefix(
  skim_str_t *out,
  const char *src,
  size_t len,
  size_t start,
  size_t decorators_end,
  size_t member_prefix_start,
  bool saw_static,
  bool saw_override,
  bool saw_accessor
) {
  size_t leading = start;
  if (decorators_end > start) {
    emit_decorator_range(out, src, len, start, decorators_end);
    leading = decorators_end;
  }
  while (leading < member_prefix_start && isspace((unsigned char)src[leading])) {
    skim_str_putc(out, src[leading++]);
  }
  if (saw_static) skim_str_puts(out, "static ");
  if (saw_override && saw_accessor) skim_str_puts(out, "override ");
  if (saw_accessor) skim_str_puts(out, "accessor ");
}

static bool try_consume_following_class_type_member(
  skim_str_t *out,
  const char *src,
  size_t len,
  size_t end,
  size_t stmt_end,
  size_t *io
) {
  size_t next_member = skim_skip_ws_comments(src, len, stmt_end);
  const char *next_mod = NULL;
  size_t next_mod_len = 0;
  bool next_abstract = false;
  bool saw_next_modifier = false;
  while (next_member < end &&
         class_member_modifier_at(src, len, next_member, &next_mod, &next_mod_len, &next_abstract)) {
    saw_next_modifier = true;
    next_member = skim_skip_ws_comments(src, len, next_member + next_mod_len);
  }
  if (!saw_next_modifier || next_member >= end) return false;

  if (bracket_contains_type_colon(src, len, next_member)) {
    size_t next_bracket_end = skip_class_member_name(src, len, next_member);
    size_t next_colon = skim_skip_ws_comments(src, len, next_bracket_end);
    if (next_colon < end && src[next_colon] == ':') {
      *io = skip_class_index_signature_tail(src, len, next_colon + 1);
      return true;
    }
  }

  size_t next_name_start = next_member;
  size_t next_name_end = skip_class_member_name(src, len, next_member);
  if (next_name_end <= next_name_start) return false;

  size_t after_next_name = skim_skip_ws_comments(src, len, next_name_end);
  if (after_next_name >= end || src[after_next_name] != ':') return false;

  size_t next_type_end = skip_class_type_tail(src, len, after_next_name + 1);
  next_type_end = skim_skip_ws_comments(src, len, next_type_end);
  skim_str_putn(out, src + next_name_start, next_name_end - next_name_start);
  if (next_type_end < end && src[next_type_end] == ';') {
    skim_str_putc(out, ';');
    next_type_end++;
  }
  *io = next_type_end;
  return true;
}

static bool
try_remove_class_type_member(skim_str_t *out, const char *src, size_t len, size_t i, size_t end, size_t *io) {
  if (!looks_like_class_member_start(src, i)) return false;
  size_t j = i;
  size_t decorators_end = i;
  bool saw_abstract = false;
  bool saw_declare = false;
  bool saw_static = false;
  bool saw_override = false;
  bool saw_accessor = false;
  bool saw_erased_modifier = false;
  for (;;) {
    j = skim_skip_ws_comments(src, len, j);
    if (j >= end || src[j] != '@') break;
    size_t after = skip_decorator_line(src, len, j);
    if (after <= j) break;
    decorators_end = after;
    j = after;
  }
  for (;;) {
    const char *mod = NULL;
    size_t mod_len = 0;
    bool is_abstract = false;
    j = skim_skip_ws_comments(src, len, j);
    if (class_modifier_word_used_as_method_name(src, len, j)) break;
    if (!class_member_modifier_at(src, len, j, &mod, &mod_len, &is_abstract)) break;
    if (strcmp(mod, "accessor") == 0 && saw_accessor) break;
    saw_abstract = saw_abstract || is_abstract;
    saw_declare = saw_declare || strcmp(mod, "declare") == 0;
    saw_static = saw_static || strcmp(mod, "static") == 0;
    saw_override = saw_override || strcmp(mod, "override") == 0;
    saw_accessor = saw_accessor || strcmp(mod, "accessor") == 0;
    saw_erased_modifier = saw_erased_modifier || (strcmp(mod, "static") != 0 && strcmp(mod, "accessor") != 0);
    j += mod_len;
  }
  j = skim_skip_ws_comments(src, len, j);
  size_t member_prefix_start = j;
  if (j < end && src[j] == '*') j = skim_skip_ws_comments(src, len, j + 1);

  bool computed_type_member = j < end && bracket_contains_type_colon(src, len, j);
  j = skip_class_member_name(src, len, j);
  if (j >= end || j == i || j == member_prefix_start) return false;
  size_t first_name_end = j;
  size_t after_first_name = skim_skip_ws_comments(src, len, first_name_end);
  if (
    (first_name_end - member_prefix_start == 3 && memcmp(src + member_prefix_start, "get", 3) == 0) ||
    (first_name_end - member_prefix_start == 3 && memcmp(src + member_prefix_start, "set", 3) == 0)
  ) {
    if (
      after_first_name < end && src[after_first_name] != '(' && src[after_first_name] != ':' &&
      src[after_first_name] != ';' && src[after_first_name] != '='
    ) {
      size_t property_end = skip_class_member_name(src, len, after_first_name);
      if (property_end > after_first_name) j = property_end;
    }
  }
  size_t member_name_end = j;
  j = skim_skip_ws_comments(src, len, j);
  if (j < end && (src[j] == '?' || src[j] == '!')) j = skim_skip_ws_comments(src, len, j + 1);
  if (j < end && src[j] == '<') j = skip_class_angle_type_list(src, len, j);
  j = skim_skip_ws_comments(src, len, j);

  if (saw_declare) {
    size_t stmt_end = skim_skip_statement_like(src, len, j);
    if (stmt_end > end) stmt_end = end;
    skim_emit_preserved_newlines(out, src, i, stmt_end);
    if (try_consume_following_class_type_member(out, src, len, end, stmt_end, io)) return true;
    *io = stmt_end;
    return true;
  }

  if (computed_type_member) {
    size_t stmt_end = j;
    if (j < end && src[j] == ':') {
      stmt_end = skip_class_index_signature_tail(src, len, j + 1);
      size_t semi = skim_skip_ws_comments(src, len, stmt_end);
      if (semi < end && src[semi] == ';') stmt_end = semi + 1;
    } else {
      stmt_end = skim_skip_statement_like(src, len, j);
    }
    if (stmt_end > end) stmt_end = end;
    skim_emit_preserved_newlines(out, src, i, stmt_end);
    if (try_consume_following_class_type_member(out, src, len, end, stmt_end, io)) return true;
    *io = stmt_end;
    return true;
  }

  if (j < end && src[j] == '(') {
    size_t params_open = j;
    if (params_open == member_prefix_start) return false;
    size_t params_after = skim_skip_balanced(src, len, j, '(', ')');
    size_t params_close = params_after > params_open ? params_after - 1 : params_open;
    j = params_after;
    size_t after_params = j;
    j = skim_skip_ws_comments(src, len, j);
    if (j < end && src[j] == ':') j = skim_skip_ws_comments(src, len, skip_class_type_tail(src, len, j + 1));
    if (saw_abstract) {
      size_t stmt_end =
        j < end && src[j] == '{' ? skim_skip_balanced(src, len, j, '{', '}') : skim_skip_statement_like(src, len, j);
      if (stmt_end > end) stmt_end = end;
      if (stmt_end < end && src[stmt_end] == ';') stmt_end++;
      skim_emit_preserved_newlines(out, src, i, stmt_end);
      *io = stmt_end;
      return true;
    }
    if (j < end && src[j] == ';') {
      skim_emit_preserved_newlines(out, src, i, j + 1);
      *io = j + 1;
      return true;
    }
    if (j < end && src[j] != '{' && range_has_newline(src, after_params, j)) {
      skim_emit_preserved_newlines(out, src, i, j);
      *io = j;
      return true;
    }
    if (j < end && src[j] == '{') {
      emit_preserved_class_member_prefix(
        out, src, len, i, decorators_end, member_prefix_start, saw_static, saw_override, saw_accessor
      );
      size_t prefix_end = member_name_end;
      while (prefix_end > member_prefix_start && isspace((unsigned char)src[prefix_end - 1]))
        prefix_end--;
      if (prefix_end > member_prefix_start && (src[prefix_end - 1] == '?' || src[prefix_end - 1] == '!')) prefix_end--;
      bool has_computed_name = false;
      for (size_t k = member_prefix_start; k < prefix_end; k++) {
        if (src[k] == '[') {
          has_computed_name = true;
          break;
        }
      }
      if (has_computed_name) skim_transform_range(src, len, member_prefix_start, prefix_end, out);
      else skim_str_putn(out, src + member_prefix_start, prefix_end - member_prefix_start);
      skim_str_putc(out, '(');
      skim_transform_range(src, len, params_open + 1, params_close, out);
      skim_str_putc(out, ')');
      skim_transform_range(src, len, params_after, j, out);
      *io = j;
      return true;
    }
    return false;
  }

  if (j < end && src[j] == ':') {
    size_t colon = j;
    size_t type_end = skip_class_type_tail(src, len, j + 1);
    j = skim_skip_ws_comments(src, len, type_end);
    if ((saw_abstract || saw_declare) && j < end && src[j] == ';') {
      skim_emit_preserved_newlines(out, src, i, j + 1);
      *io = j + 1;
      return true;
    }
    if (!saw_abstract) {
      if (saw_declare) {
        size_t stmt_end = skim_skip_statement_like(src, len, j);
        if (stmt_end > end) stmt_end = end;
        skim_emit_preserved_newlines(out, src, i, stmt_end);
        *io = stmt_end;
        return true;
      }
      if (j < end && src[j] == '=') {
        size_t stmt_end = skim_skip_statement_like(src, len, j);
        if (stmt_end > end) stmt_end = end;
        if (saw_static || saw_accessor) {
          emit_preserved_class_member_prefix(
            out, src, len, i, decorators_end, member_prefix_start, saw_static, saw_override, saw_accessor
          );
          skim_transform_range(src, len, member_prefix_start, colon, out);
        } else {
          skim_transform_range(src, len, i, colon, out);
        }
        skim_transform_range(src, len, j, stmt_end, out);
        *io = stmt_end;
        return true;
      }
      if (j < end && src[j] == ';') {
        if (skim_options.remove_class_fields_without_initializer || skim_options.allow_declare_fields_false) {
          skim_emit_preserved_newlines(out, src, i, j + 1);
          *io = j + 1;
          return true;
        }
        if (saw_static || saw_accessor) {
          emit_preserved_class_member_prefix(
            out, src, len, i, decorators_end, member_prefix_start, saw_static, saw_override, saw_accessor
          );
          skim_transform_range(src, len, member_prefix_start, colon, out);
        } else {
          skim_transform_range(src, len, i, colon, out);
        }
        skim_str_putc(out, ';');
        skim_emit_preserved_newlines(out, src, colon, j + 1);
        *io = j + 1;
        return true;
      }
      if (type_end < end && ((src[type_end] == '\n' || src[type_end] == '\r') || range_has_newline(src, type_end, j))) {
        if (skim_options.remove_class_fields_without_initializer || skim_options.allow_declare_fields_false) {
          skim_emit_preserved_newlines(out, src, i, type_end);
          *io = type_end;
          return true;
        }
        if (saw_static || saw_accessor) {
          emit_preserved_class_member_prefix(
            out, src, len, i, decorators_end, member_prefix_start, saw_static, saw_override, saw_accessor
          );
          skim_transform_range(src, len, member_prefix_start, colon, out);
        } else {
          skim_transform_range(src, len, i, colon, out);
        }
        skim_str_putc(out, ';');
        *io = type_end;
        return true;
      }
      if (type_end >= end || j >= end) {
        if (skim_options.remove_class_fields_without_initializer || skim_options.allow_declare_fields_false) {
          skim_emit_preserved_newlines(out, src, i, end);
          *io = end;
          return true;
        }
        if (saw_static || saw_accessor) {
          emit_preserved_class_member_prefix(
            out, src, len, i, decorators_end, member_prefix_start, saw_static, saw_override, saw_accessor
          );
          skim_transform_range(src, len, member_prefix_start, colon, out);
        } else {
          skim_transform_range(src, len, i, colon, out);
        }
        skim_str_putc(out, ';');
        *io = end;
        return true;
      }
    }
  } else if ((saw_abstract || saw_declare) && j < end && src[j] == ';') {
    skim_emit_preserved_newlines(out, src, i, j + 1);
    *io = j + 1;
    return true;
  } else if (saw_erased_modifier && j < end && src[j] == '=') {
    size_t stmt_end = skim_skip_statement_like(src, len, j);
    if (stmt_end > end) stmt_end = end;
    emit_preserved_class_member_prefix(
      out, src, len, i, decorators_end, member_prefix_start, saw_static, saw_override, saw_accessor
    );
    skim_transform_range(src, len, member_prefix_start, stmt_end, out);
    *io = stmt_end;
    return true;
  } else if (saw_erased_modifier && (j >= end || src[j] == ';' || src[j] == '\n' || src[j] == '\r' || src[j] == '}')) {
    emit_preserved_class_member_prefix(
      out, src, len, i, decorators_end, member_prefix_start, saw_static, saw_override, saw_accessor
    );
    skim_transform_range(src, len, member_prefix_start, member_name_end, out);
    if (j < end && src[j] == ';') {
      skim_str_putc(out, ';');
      j++;
    }
    *io = j;
    return true;
  } else if (saw_accessor && (j >= end || src[j] == ';' || src[j] == '\n' || src[j] == '\r' || src[j] == '}')) {
    emit_preserved_class_member_prefix(
      out, src, len, i, decorators_end, member_prefix_start, saw_static, saw_override, true
    );
    skim_transform_range(src, len, member_prefix_start, member_name_end, out);
    if (j < end && src[j] == ';') {
      skim_str_putc(out, ';');
      j++;
    }
    *io = j;
    return true;
  } else if (saw_accessor && j < end && src[j] == '=') {
    size_t stmt_end = skim_skip_statement_like(src, len, j);
    if (stmt_end > end) stmt_end = end;
    emit_preserved_class_member_prefix(
      out, src, len, i, decorators_end, member_prefix_start, saw_static, saw_override, true
    );
    skim_transform_range(src, len, member_prefix_start, stmt_end, out);
    *io = stmt_end;
    return true;
  }

  return false;
}

static bool try_remove_literal_named_class_type_member(
  skim_str_t *out,
  const char *src,
  size_t len,
  size_t i,
  size_t end,
  size_t *io
) {
  if (!looks_like_class_member_start(src, i)) return false;
  size_t name_end = i;
  if (src[i] == '\'' || src[i] == '"' || src[i] == '`') {
    name_end = skim_skip_string_raw(src, len, i);
  } else if (isdigit((unsigned char)src[i])) {
    while (name_end < end && (isdigit((unsigned char)src[name_end]) || src[name_end] == '.'))
      name_end++;
  } else {
    return false;
  }
  size_t colon = skim_skip_ws_comments(src, len, name_end);
  if (colon >= end || src[colon] != ':') return false;
  size_t type_start = skim_skip_ws_comments(src, len, colon + 1);
  size_t type_end = skip_class_type_tail(src, len, colon + 1);
  if (type_start < end && src[type_start] == '{') type_end = skim_skip_balanced(src, len, type_start, '{', '}');
  size_t after_type = skim_skip_ws_comments(src, len, type_end);
  if (after_type < end && src[after_type] == '=') {
    size_t stmt_end = skim_skip_statement_like(src, len, after_type);
    if (stmt_end > end) stmt_end = end;
    skim_transform_range(src, len, i, colon, out);
    skim_transform_range(src, len, after_type, stmt_end, out);
    *io = stmt_end;
    return true;
  }
  if (after_type < end && src[after_type] == ';') {
    skim_transform_range(src, len, i, colon, out);
    skim_str_putc(out, ';');
    *io = after_type + 1;
    return true;
  }
  if (
    type_end < end && ((src[type_end] == '\n' || src[type_end] == '\r') || range_has_newline(src, type_end, after_type))
  ) {
    skim_transform_range(src, len, i, colon, out);
    skim_str_putc(out, ';');
    *io = type_end;
    return true;
  }
  return false;
}

static bool
copy_class_body_common_token(skim_str_t *out, const char *src, size_t len, size_t end, size_t *io, bool allow_ts) {
  size_t i = *io;
  if (based_number_literal_at(src, len, i)) {
    *io = copy_based_number_literal(out, src, len, i);
    return true;
  }
  if (decimal_number_literal_at(src, len, i)) {
    *io = copy_decimal_number_literal(out, src, len, i);
    return true;
  }
  if (src[i] == '\'' || src[i] == '"' || src[i] == '`') {
    *io = skim_copy_string(out, src, len, i);
    return true;
  }
  if (i + 1 < len && src[i] == '/' && src[i + 1] == '/') {
    *io = skim_copy_line_comment(out, src, len, i);
    return true;
  }
  if (i + 1 < len && src[i] == '/' && src[i + 1] == '*') {
    *io = skim_copy_block_comment(out, src, len, i);
    return true;
  }
  if (allow_ts && src[i] != '@' && skim_transform_try_at(out, src, len, io)) return true;
  (void)end;
  return false;
}

static bool
copy_nested_class_body_token(skim_str_t *out, const char *src, size_t len, size_t end, size_t *io, int *depth) {
  size_t i = *io;
  if (copy_class_body_common_token(out, src, len, end, io, true)) return true;
  if (src[i] == '{') {
    (*depth)++;
    skim_str_putc(out, src[i]);
    *io = i + 1;
    return true;
  }
  if (src[i] == '}') {
    (*depth)--;
    skim_str_putc(out, src[i]);
    *io = i + 1;
    return true;
  }
  if (skim_emit_copy_plain_identifier(out, src, end, io)) return true;
  if (skim_emit_copy_plain_span(out, src, end, io)) return true;
  skim_str_putc(out, src[i]);
  *io = i + 1;
  return true;
}

static bool copy_class_top_level_space(skim_str_t *out, const char *src, size_t len, size_t end, size_t *io) {
  size_t i = *io;
  if (!isspace((unsigned char)src[i])) return false;

  size_t j = i;
  bool has_newline = false;
  while (j < end && isspace((unsigned char)src[j])) {
    has_newline = has_newline || src[j] == '\n' || src[j] == '\r';
    j++;
  }
  if (has_newline && class_prev_needs_member_separator(out) && next_looks_like_class_member(src, len, j, end)) {
    skim_str_putc(out, ';');
  }
  skim_str_putn(out, src + i, j - i);
  *io = j;
  return true;
}

static bool class_reserved_word_before_method_call(const char *src, size_t len, size_t i, size_t end) {
  if (!skim_is_id_start(src[i])) return false;

  size_t name_start = 0, name_end = 0;
  size_t after_name = skim_parse_identifier(src, len, i, &name_start, &name_end);
  size_t next = skim_skip_ws_comments(src, len, after_name);
  if (name_start != i || next >= end || src[next] != '(') return false;

  return skim_word_at(src, len, i, "class") || skim_word_at(src, len, i, "enum") ||
         skim_word_at(src, len, i, "export") || skim_word_at(src, len, i, "function") ||
         skim_word_at(src, len, i, "import") || skim_word_at(src, len, i, "interface") ||
         skim_word_at(src, len, i, "module") || skim_word_at(src, len, i, "namespace") ||
         skim_word_at(src, len, i, "type");
}

static void transform_class_body_range(const char *src, size_t len, size_t start, size_t end, skim_str_t *out) {
  int depth = 0;
  for (size_t i = start; i < end;) {
    if (depth > 0) {
      copy_nested_class_body_token(out, src, len, end, &i, &depth);
      continue;
    }
    if (copy_class_top_level_space(out, src, len, end, &i)) continue;
    if (try_remove_class_type_member(out, src, len, i, end, &i)) continue;
    if (try_remove_literal_named_class_type_member(out, src, len, i, end, &i)) continue;
    if (copy_class_body_common_token(out, src, len, end, &i, false)) continue;
    if (src[i] == ';') {
      if (!class_semicolon_is_empty_element(out)) skim_str_putc(out, src[i]);
      i++;
      continue;
    }
    if (class_keyword_used_as_field_name(src, len, i)) {
      skim_str_putc(out, src[i++]);
      continue;
    }
    if (skim_word_at(src, len, i, "abstract")) {
      i += 8;
      continue;
    }
    if (class_modifier_word_used_as_method_name(src, len, i)) {
      skim_str_putc(out, src[i++]);
      continue;
    }
    if (class_modifier_word_used_as_field_name(src, len, i, end)) {
      skim_str_putc(out, src[i++]);
      continue;
    }
    if (class_reserved_word_before_method_call(src, len, i, end)) {
      skim_str_putc(out, src[i++]);
      continue;
    }
    if (src[i] != '@' && skim_transform_try_at(out, src, len, &i)) continue;
    if (src[i] == '{') depth++;
    if (depth > 0 && skim_emit_copy_plain_identifier(out, src, end, &i)) continue;
    if (depth > 0 && skim_emit_copy_plain_span(out, src, end, &i)) continue;
    skim_str_putc(out, src[i++]);
  }
}

static void emit_class_header(
  skim_str_t *out,
  const char *src,
  size_t len,
  size_t class_word,
  size_t body_open,
  const class_prefix_t *prefix
) {
  if (prefix->is_export) skim_str_puts(out, "export ");
  if (prefix->is_default) skim_str_puts(out, "default ");
  if (prefix->has_decorators) {
    emit_decorator_range(out, src, len, prefix->decorators_start, prefix->decorators_end);
    skim_str_putc(out, ' ');
  }
  skim_str_putn(out, src + class_word, 5);
  for (size_t i = class_word + 5; i <= body_open;) {
    if (i < body_open && skim_word_at(src, len, i, "extends")) {
      size_t expr = skim_skip_ws_comments(src, len, i + 7);
      if (expr < body_open && src[expr] == '(') {
        size_t close = skim_skip_balanced(src, len, expr, '(', ')');
        size_t after = skim_skip_ws_comments(src, len, close);
        size_t inner = skim_skip_ws_comments(src, len, expr + 1);
        if (
          close <= body_open && after == body_open && !skim_word_at(src, len, inner, "await") &&
          !skim_word_at(src, len, inner, "yield")
        ) {
          skim_str_puts(out, "extends ");
          skim_transform_range(src, len, expr + 1, close - 1, out);
          i = close;
          continue;
        }
      }
    }
    if (i < body_open && src[i] == '<') {
      i = skip_class_angle_type_list(src, len, i);
      continue;
    }
    if (skim_transform_try_at(out, src, len, &i)) continue;
    if (skim_emit_copy_plain_identifier(out, src, body_open + 1, &i)) continue;
    if (skim_emit_copy_plain_span(out, src, body_open + 1, &i)) continue;
    skim_str_putc(out, src[i++]);
  }
}

static void emit_normal_class(
  skim_str_t *out,
  const char *src,
  size_t len,
  size_t class_word,
  size_t body_open,
  size_t body_close,
  const class_prefix_t *prefix
) {
  emit_class_header(out, src, len, class_word, body_open, prefix);
  transform_class_body_range(src, len, body_open + 1, body_close, out);
  skim_str_putc(out, src[body_close]);
}

static size_t find_class_body_open(const char *src, size_t len, size_t i) {
  int paren = 0, bracket = 0, angle = 0;
  while (i < len) {
    char c = src[i];
    if (c == '\'' || c == '"' || c == '`') {
      i = skim_skip_string_raw(src, len, i);
      continue;
    }
    if (i + 1 < len && c == '/' && src[i + 1] == '/') {
      i += 2;
      while (i < len && src[i] != '\n')
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
    if (c == '(') paren++;
    else if (c == ')' && paren > 0) paren--;
    else if (c == '[') bracket++;
    else if (c == ']' && bracket > 0) bracket--;
    else if (c == '<') {
      i = skip_class_angle_type_list(src, len, i);
      continue;
    } else if (c == '>' && angle > 0) angle--;
    else if (c == '{' && paren == 0 && bracket == 0 && angle == 0) return i;
    i++;
  }
  return i;
}

static size_t
split_params(const char *src, size_t len, size_t start, size_t end, param_t **out_params, size_t *out_count) {
  param_t *params = NULL;
  size_t count = 0;
  size_t item_start = start;
  int paren = 0, bracket = 0, brace = 0, angle = 0;
  for (size_t i = start; i <= end; i++) {
    char c = i < end ? src[i] : ',';
    if (i < end && (c == '\'' || c == '"' || c == '`')) {
      i = skim_skip_string_raw(src, len, i) - 1;
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
    if (paren || bracket || brace || angle || c != ',') continue;

    size_t a = skim_skip_ws(src, len, item_start);
    size_t b = i;
    while (b > a && (src[b - 1] == ' ' || src[b - 1] == '\t' || src[b - 1] == '\n'))
      b--;
    if (a < b) {
      bool is_prop = false;
      const char *mod = NULL;
      size_t mod_len = 0;
      size_t decorators_start = a;
      size_t decorators_end = a;
      for (;;) {
        a = skim_skip_ws(src, len, a);
        if (a >= b || src[a] != '@') break;
        size_t after = skip_decorator_line(src, len, a);
        if (after <= a || after > b) break;
        decorators_end = after;
        a = after;
      }
      a = skim_skip_ws(src, len, a);
      for (;;) {
        size_t m = skim_skip_ws(src, len, a);
        if (!modifier_at(src, len, m, &mod, &mod_len)) break;
        is_prop = true;
        a = m + mod_len;
      }

      size_t name_start = 0, name_end = 0;
      size_t after_name = skim_parse_identifier(src, len, a, &name_start, &name_end);
      if (name_start != name_end) {
        size_t tail = after_name;
        if (tail < b && src[tail] == '?') tail++;
        size_t colon = skim_skip_ws(src, len, tail);
        size_t init = b;
        if (colon < b && src[colon] == ':') {
          init = colon + 1;
          int angle = 0;
          while (init < b) {
            if (src[init] == '<') angle++;
            else if (src[init] == '>' && angle > 0) angle--;
            else if (src[init] == '=' && angle == 0) {
              if (init + 1 < b && src[init + 1] == '>') {
                init += 2;
                continue;
              }
              break;
            }
            init++;
          }
        } else {
          init = skim_skip_ws(src, len, tail);
        }

        skim_str_t param = {0};
        if (decorators_end > decorators_start) {
          emit_decorator_range(&param, src, len, decorators_start, decorators_end);
          skim_str_putc(&param, ' ');
        }
        skim_str_putn(&param, src + name_start, name_end - name_start);
        if (init < b && src[init] == '=') {
          skim_str_putc(&param, ' ');
          skim_str_putn(&param, src + init, b - init);
        }

        params = realloc(params, sizeof(param_t) * (count + 1));
        if (!params) skim_die("out of memory");
        params[count].name = skim_slice_dup(src, name_start, name_end);
        params[count].param = param.data;
        params[count].is_param_property = is_prop;
        count++;
      }
    }
    item_start = i + 1;
  }
  *out_params = params;
  *out_count = count;
  return end;
}

static void free_params(param_t *params, size_t count) {
  for (size_t i = 0; i < count; i++) {
    free(params[i].name);
    free(params[i].param);
  }
  free(params);
}

static size_t find_after_top_level_super(const char *src, size_t len, size_t start, size_t end) {
  int brace = 0, paren = 0, bracket = 0;
  for (size_t i = start; i < end;) {
    if (src[i] == '\'' || src[i] == '"' || src[i] == '`') {
      i = skim_skip_string_raw(src, len, i);
      continue;
    }
    if (i + 1 < len && src[i] == '/' && src[i + 1] == '/') {
      i += 2;
      while (i < end && src[i] != '\n')
        i++;
      continue;
    }
    if (i + 1 < len && src[i] == '/' && src[i + 1] == '*') {
      i += 2;
      while (i + 1 < end && !(src[i] == '*' && src[i + 1] == '/'))
        i++;
      if (i + 1 < end) i += 2;
      continue;
    }
    if (brace == 0 && paren == 0 && bracket == 0 && skim_word_at(src, len, i, "super")) {
      size_t call = skim_skip_ws_comments(src, len, i + 5);
      if (call < end && src[call] == '(') {
        size_t after = skim_skip_balanced(src, len, call, '(', ')');
        after = skim_skip_ws_comments(src, len, after);
        if (after < end && src[after] == ';') after++;
        return after;
      }
    }
    if (src[i] == '{') brace++;
    else if (src[i] == '}' && brace > 0) brace--;
    else if (src[i] == '(') paren++;
    else if (src[i] == ')' && paren > 0) paren--;
    else if (src[i] == '[') bracket++;
    else if (src[i] == ']' && bracket > 0) bracket--;
    i++;
  }
  return start;
}

static bool range_contains(const char *src, size_t start, size_t end, const char *needle) {
  size_t n = strlen(needle);
  if (n == 0 || end < start || end - start < n) return false;
  for (size_t i = start; i + n <= end; i++) {
    if (memcmp(src + i, needle, n) == 0) return true;
  }
  return false;
}

static bool scan_class_decorators(const char *src, size_t len, size_t *io, class_prefix_t *prefix) {
  size_t j = skim_skip_ws_comments(src, len, *io);
  if (j >= len || src[j] != '@') return false;
  prefix->has_decorators = true;
  prefix->decorators_start = j;
  for (;;) {
    j = skim_skip_ws_comments(src, len, j);
    if (j >= len || src[j] != '@') break;
    size_t after = skip_decorator_line(src, len, j);
    if (after <= j) break;
    prefix->decorators_end = after;
    j = after;
  }
  *io = j;
  return true;
}

bool skim_ts_class_try(skim_str_t *out, const char *src, size_t len, size_t *io) {
  size_t i = *io;
  if (!skim_word_at(src, len, i, "class") && !skim_word_at(src, len, i, "export") && src[i] != '@') return false;

  size_t class_word = i;
  class_prefix_t prefix = {0};
  size_t j = i;
  scan_class_decorators(src, len, &j, &prefix);
  j = skim_skip_ws_comments(src, len, j);
  if (skim_word_at(src, len, j, "export")) {
    prefix.is_export = true;
    j = skim_skip_ws_comments(src, len, j + 6);
    if (skim_word_at(src, len, j, "default")) {
      prefix.is_default = true;
      j = skim_skip_ws_comments(src, len, j + 7);
    }
  }
  if (!prefix.has_decorators) scan_class_decorators(src, len, &j, &prefix);
  j = skim_skip_ws_comments(src, len, j);
  if (skim_word_at(src, len, j, "abstract")) j = skim_skip_ws_comments(src, len, j + 8);
  if (!skim_word_at(src, len, j, "class")) return false;
  class_word = j;

  size_t body_open = find_class_body_open(src, len, class_word);
  if (body_open >= len) return false;
  if (range_contains(src, class_word, body_open, "extends class")) return false;
  size_t body_close = skim_skip_balanced(src, len, body_open, '{', '}') - 1;

  size_t ctor = body_open + 1;
  size_t params_open = 0;
  size_t params_close = 0;
  size_t ctor_body_open = 0;
  size_t ctor_member_start = 0;
  bool found = false;
  while (ctor < body_close) {
    if (src[ctor] == '\'' || src[ctor] == '"' || src[ctor] == '`') {
      ctor = skim_skip_string_raw(src, len, ctor);
      continue;
    }
    if (skim_word_at(src, len, ctor, "constructor")) {
      ctor_member_start = ctor;
      size_t line_start = ctor;
      while (line_start > body_open + 1 && src[line_start - 1] != '\n' && src[line_start - 1] != '\r')
        line_start--;
      size_t maybe_modifier = skim_skip_ws_comments(src, len, line_start);
      if (maybe_modifier < ctor) {
        const char *mod = NULL;
        size_t mod_len = 0;
        bool is_abstract = false;
        size_t after_mods = maybe_modifier;
        bool saw_ctor_modifier = false;
        for (;;) {
          after_mods = skim_skip_ws_comments(src, len, after_mods);
          if (!class_member_modifier_at(src, len, after_mods, &mod, &mod_len, &is_abstract)) break;
          if (strcmp(mod, "static") == 0 || strcmp(mod, "accessor") == 0) break;
          saw_ctor_modifier = true;
          after_mods += mod_len;
        }
        after_mods = skim_skip_ws_comments(src, len, after_mods);
        if (saw_ctor_modifier && after_mods == ctor) ctor_member_start = maybe_modifier;
      }
      params_open = skim_skip_ws_comments(src, len, ctor + 11);
      if (params_open < len && src[params_open] == '(') {
        params_close = skim_skip_balanced(src, len, params_open, '(', ')') - 1;
        ctor_body_open = skim_skip_ws_comments(src, len, params_close + 1);
        if (ctor_body_open < len && src[ctor_body_open] == ':') {
          ctor_body_open = skim_skip_ws_comments(src, len, skip_class_type_tail(src, len, ctor_body_open + 1));
        }
        if (ctor_body_open < len && src[ctor_body_open] == '{') {
          found = true;
          break;
        }
        if (ctor_body_open < len && src[ctor_body_open] == ';') {
          ctor = ctor_body_open + 1;
          continue;
        }
      }
    }
    ctor++;
  }
  if (!found) {
    emit_normal_class(out, src, len, class_word, body_open, body_close, &prefix);
    *io = body_close + 1;
    if (*io < len && src[*io] == '}') (*io)++;
    return true;
  }
  if (params_open >= len || src[params_open] != '(') return false;
  if (ctor_body_open >= len || src[ctor_body_open] != '{') return false;
  size_t ctor_body_close = skim_skip_balanced(src, len, ctor_body_open, '{', '}') - 1;

  param_t *params = NULL;
  size_t param_count = 0;
  split_params(src, len, params_open + 1, params_close, &params, &param_count);
  bool has_prop = false;
  for (size_t p = 0; p < param_count; p++)
    has_prop = has_prop || params[p].is_param_property;
  if (!has_prop) {
    free_params(params, param_count);
    emit_normal_class(out, src, len, class_word, body_open, body_close, &prefix);
    *io = body_close + 1;
    if (*io < len && src[*io] == '}') (*io)++;
    return true;
  }

  emit_class_header(out, src, len, class_word, body_open, &prefix);
  skim_str_putc(out, '\n');
  for (size_t p = 0; p < param_count; p++) {
    if (!params[p].is_param_property) continue;
    skim_str_puts(out, "  ");
    skim_str_puts(out, params[p].name);
    skim_str_puts(out, ";\n");
  }
  transform_class_body_range(src, len, body_open + 1, ctor_member_start ? ctor_member_start : ctor, out);
  skim_str_puts(out, "constructor(");
  for (size_t p = 0; p < param_count; p++) {
    if (p) skim_str_puts(out, ", ");
    skim_str_puts(out, params[p].param);
  }
  skim_str_puts(out, ") {");
  skim_str_putc(out, '\n');
  size_t assignment_pos = find_after_top_level_super(src, len, ctor_body_open + 1, ctor_body_close);
  skim_transform_range(src, len, ctor_body_open + 1, assignment_pos, out);
  for (size_t p = 0; p < param_count; p++) {
    if (!params[p].is_param_property) continue;
    skim_str_puts(out, "    this.");
    skim_str_puts(out, params[p].name);
    skim_str_puts(out, " = ");
    skim_str_puts(out, params[p].name);
    skim_str_puts(out, ";\n");
  }
  skim_transform_range(src, len, assignment_pos, ctor_body_close, out);
  skim_str_putc(out, '\n');
  skim_str_putc(out, src[ctor_body_close]);
  transform_class_body_range(src, len, ctor_body_close + 1, body_close, out);
  skim_str_putc(out, src[body_close]);

  *io = body_close + 1;
  if (*io < len && src[*io] == '}') (*io)++;
  free_params(params, param_count);
  return true;
}
