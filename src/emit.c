#include "skim_internal.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static bool next_non_ws_is_member_access(const char *src, size_t len, size_t i) {
  if (i >= len || !skim_is_id_start(src[i])) return false;
  i++;
  while (i < len && skim_is_id_part(src[i]))
    i++;
  i = skim_skip_ws(src, len, i);
  return i < len && (src[i] == '.' || src[i] == '[' || (i + 1 < len && src[i] == '?' && src[i + 1] == '.'));
}

static bool slash_starts_regex(const char *src, size_t start, size_t i) {
  bool saw_newline = false;
  while (i > start && (src[i - 1] == ' ' || src[i - 1] == '\t' || src[i - 1] == '\n' || src[i - 1] == '\r')) {
    if (src[i - 1] == '\n' || src[i - 1] == '\r') saw_newline = true;
    i--;
  }
  if (i == start) return true;
  if (saw_newline) return true;
  char c = src[i - 1];
  if ((c == '-' || c == '+') && i >= 2 && src[i - 2] == c) return false;
  return c == '(' || c == '[' || c == '{' || c == ',' || c == ';' || c == ':' || c == '=' || c == '!' || c == '?' ||
         c == '&' || c == '|' || c == '+' || c == '-' || c == '*' || c == '~' || c == '^' || c == '<' || c == '>';
}

static size_t copy_regex_literal(skim_str_t *out, const char *src, size_t len, size_t i) {
  skim_str_putc(out, src[i++]);
  bool in_class = false;
  while (i < len) {
    char c = src[i++];
    skim_str_putc(out, c);
    if (c == '\\' && i < len) {
      skim_str_putc(out, src[i++]);
      continue;
    }
    if (c == '[') in_class = true;
    else if (c == ']') in_class = false;
    else if (c == '/' && !in_class) break;
  }
  while (i < len && skim_is_id_part(src[i]))
    skim_str_putc(out, src[i++]);
  return i;
}

static bool word_before_out_is(const skim_str_t *out, size_t pos, const char *word) {
  while (pos > 0 && isspace((unsigned char)out->data[pos - 1]))
    pos--;
  size_t end = pos;
  while (pos > 0 && skim_is_id_part(out->data[pos - 1]))
    pos--;
  size_t n = strlen(word);
  return end - pos == n && memcmp(out->data + pos, word, n) == 0;
}

static void trim_tagged_template_space(skim_str_t *out) {
  size_t end = out->len;
  while (end > 0 && (out->data[end - 1] == ' ' || out->data[end - 1] == '\t'))
    end--;
  if (end == out->len || end == 0) return;
  char prev = out->data[end - 1];
  if (!(skim_is_id_part(prev) || prev == ')' || prev == ']')) return;
  if (
    word_before_out_is(out, end, "return") || word_before_out_is(out, end, "throw") ||
    word_before_out_is(out, end, "yield") || word_before_out_is(out, end, "case") ||
    word_before_out_is(out, end, "delete") || word_before_out_is(out, end, "typeof") ||
    word_before_out_is(out, end, "void")
  )
    return;
  out->len = end;
  out->data[out->len] = '\0';
}

static bool legacy_octal_literal_at(const char *src, size_t len, size_t i) {
  if (i + 1 >= len || src[i] != '0' || !isdigit((unsigned char)src[i + 1])) return false;
  if (i > 0 && (skim_is_id_part(src[i - 1]) || src[i - 1] == '.')) return false;
  return true;
}

static size_t copy_legacy_octal_literal(skim_str_t *out, const char *src, size_t len, size_t i) {
  size_t j = i;
  unsigned long long value = 0;
  bool octal = true;
  while (j < len && isdigit((unsigned char)src[j])) {
    if (src[j] >= '8') octal = false;
    j++;
  }
  if (j < len && (src[j] == '.' || src[j] == 'e' || src[j] == 'E')) {
    skim_str_putn(out, src + i, j - i);
    return j;
  }
  for (size_t k = i; k < j; k++) {
    value = value * (octal ? 8ull : 10ull) + (unsigned long long)(src[k] - '0');
  }
  char buf[32];
  snprintf(buf, sizeof(buf), "%llu", value);
  skim_str_puts(out, buf);
  return j;
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

static void maybe_put_number_member_separator(skim_str_t *out, const char *src, size_t len, size_t i) {
  if (i + 1 < len && src[i] == '.' && skim_is_id_start(src[i + 1])) skim_str_putc(out, ' ');
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
  if (j < len && src[j] == '.' && j + 1 < len && src[j + 1] == '.') {
    put_digits_stripped(out, src, i, j);
    skim_str_putc(out, ' ');
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
        maybe_put_number_member_separator(out, src, len, j);
        return j;
      }
      put_digits_stripped(out, src, i, has_fraction ? frac_end : int_end);
      if (exponent != 0) {
        char buf[48];
        snprintf(buf, sizeof(buf), "e%lld", exponent);
        skim_str_puts(out, buf);
      }
      maybe_put_number_member_separator(out, src, len, j);
      return j;
    }
    if (stripped_digits_all_zero(src, frac_start, frac_end)) {
      put_digits_stripped(out, src, i, frac_start - 1);
      maybe_put_number_member_separator(out, src, len, j);
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
    maybe_put_number_member_separator(out, src, len, j);
    return j;
  }

  size_t int_len = count_digits_stripped(src, i, j);
  if (int_len >= 12 && int_len < 20) {
    unsigned long long value = 0;
    bool ok = true;
    for (size_t p = i; p < j; p++) {
      if (src[p] == '_') continue;
      unsigned digit = (unsigned)(src[p] - '0');
      if (value > (~0ull - digit) / 10ull) {
        ok = false;
        break;
      }
      value = value * 10ull + digit;
    }
    if (ok) {
      char hex[32];
      snprintf(hex, sizeof(hex), "0x%llx", value);
      if (strlen(hex) <= int_len) {
        skim_str_puts(out, hex);
        maybe_put_number_member_separator(out, src, len, j);
        return j;
      }
    }
  }
  size_t exponent = trailing_zero_digits_stripped(src, i, j);
  if (exponent > 0 && exponent < int_len) {
    size_t mantissa_len = int_len - exponent;
    size_t exp_digits = 1;
    for (size_t n = exponent; n >= 10; n /= 10)
      exp_digits++;
    size_t scientific_len = mantissa_len + 1 + exp_digits;
    if (scientific_len < int_len) {
      put_n_digits_stripped(out, src, i, j, mantissa_len);
      skim_str_putc(out, 'e');
      char buf[32];
      snprintf(buf, sizeof(buf), "%zu", exponent);
      skim_str_puts(out, buf);
      maybe_put_number_member_separator(out, src, len, j);
      return j;
    }
  }

  put_digits_stripped(out, src, i, j);
  maybe_put_number_member_separator(out, src, len, j);
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
    maybe_put_number_member_separator(out, src, len, j);
    return j;
  }
  char buf[32];
  snprintf(buf, sizeof(buf), "%llu", value);
  skim_str_puts(out, buf);
  if (bigint) {
    skim_str_putc(out, 'n');
    j++;
  }
  maybe_put_number_member_separator(out, src, len, j);
  return j;
}

static size_t parse_simple_new_callee(const char *src, size_t len, size_t i) {
  if (!skim_word_at(src, len, i, "new")) return i;
  i = skim_skip_ws(src, len, i + 3);
  size_t start = 0, end = 0;
  i = skim_parse_identifier(src, len, i, &start, &end);
  if (start == end) return start;
  for (;;) {
    size_t dot = skim_skip_ws(src, len, i);
    if (dot >= len || src[dot] != '.') break;
    size_t part_start = 0, part_end = 0;
    size_t after = skim_parse_identifier(src, len, dot + 1, &part_start, &part_end);
    if (part_start == part_end) break;
    i = after;
  }
  return i;
}

static char prev_non_ws_in_source(const char *src, size_t start, size_t i) {
  while (i > start && isspace((unsigned char)src[i - 1]))
    i--;
  return i > start ? src[i - 1] : '\0';
}

static bool try_copy_shorthand_property(skim_str_t *out, const char *src, size_t len, size_t start, size_t *io) {
  size_t i = *io;
  char prev = prev_non_ws_in_source(src, start, i);
  if (prev != '{' && prev != ',') return false;
  if (!skim_is_id_start(src[i])) return false;
  size_t key_start = 0, key_end = 0;
  size_t after_key = skim_parse_identifier(src, len, i, &key_start, &key_end);
  if (key_start != i || key_end == key_start) return false;
  size_t colon = skim_skip_ws(src, len, after_key);
  if (colon >= len || src[colon] != ':') return false;
  size_t value_start = 0, value_end = 0;
  size_t after_value = skim_parse_identifier(src, len, skim_skip_ws(src, len, colon + 1), &value_start, &value_end);
  if (value_end - value_start != key_end - key_start) return false;
  if (memcmp(src + key_start, src + value_start, key_end - key_start) != 0) return false;
  size_t next = skim_skip_ws(src, len, after_value);
  if (next < len && src[next] != ',' && src[next] != '}') return false;
  skim_str_putn(out, src + key_start, key_end - key_start);
  *io = after_value;
  return true;
}

static bool new_argless_boundary(const char *src, size_t len, size_t i) {
  i = skim_skip_ws(src, len, i);
  if (i >= len) return true;
  char c = src[i];
  return c == ';' || c == '\n' || c == '\r' || c == ',' || c == ')' || c == ']' || c == '}';
}

static bool word_before_source_pos_is(const char *src, size_t pos, const char *word) {
  while (pos > 0 && isspace((unsigned char)src[pos - 1]))
    pos--;
  size_t end = pos;
  while (pos > 0 && skim_is_id_part(src[pos - 1]))
    pos--;
  size_t n = strlen(word);
  return end - pos == n && memcmp(src + pos, word, n) == 0;
}

static bool trailing_parameter_comma_at(const char *src, size_t len, size_t comma) {
  if (src[comma] != ',') return false;
  size_t close = skim_skip_ws_comments(src, len, comma + 1);
  if (close >= len || src[close] != ')') return false;
  return true;

  int paren = 0, bracket = 0, brace = 0;
  size_t open = comma;
  while (open > 0) {
    char c = src[--open];
    if (c == '\'' || c == '"' || c == '`') return false;
    if (c == ')') paren++;
    else if (c == '(') {
      if (paren == 0 && bracket == 0 && brace == 0) break;
      if (paren > 0) paren--;
    } else if (c == ']') bracket++;
    else if (c == '[' && bracket > 0) bracket--;
    else if (c == '}') brace++;
    else if (c == '{') {
      if (brace == 0 && paren == 0 && bracket == 0) return false;
      if (brace > 0) brace--;
    } else if ((c == ';' || c == '\n' || c == '\r') && paren == 0 && bracket == 0 && brace == 0) {
      return false;
    }
  }
  if (src[open] != '(') return false;

  size_t after = skim_skip_ws_comments(src, len, close + 1);
  bool function_like_tail = after < len && (src[after] == '{' || src[after] == ':' ||
                                            (after + 1 < len && src[after] == '=' && src[after + 1] == '>'));
  if (!function_like_tail) return false;
  if (
    word_before_source_pos_is(src, open, "if") || word_before_source_pos_is(src, open, "for") ||
    word_before_source_pos_is(src, open, "while") || word_before_source_pos_is(src, open, "switch") ||
    word_before_source_pos_is(src, open, "catch") || word_before_source_pos_is(src, open, "with")
  )
    return false;
  return true;
}

static bool try_copy_argless_new(skim_str_t *out, const char *src, size_t len, size_t *io) {
  size_t i = *io;
  size_t new_start = i;
  size_t callee_end = parse_simple_new_callee(src, len, i);
  if (callee_end <= i) return false;
  size_t after = skim_skip_ws(src, len, callee_end);
  if (after < len && src[after] == '(') return false;
  if (!new_argless_boundary(src, len, after)) return false;
  skim_str_putn(out, src + new_start, callee_end - new_start);
  skim_str_puts(out, "()");
  *io = callee_end;
  return true;
}

static bool try_copy_parenthesized_argless_new(skim_str_t *out, const char *src, size_t len, size_t *io) {
  size_t i = *io;
  if (src[i] != '(') return false;
  size_t new_start = skim_skip_ws(src, len, i + 1);
  size_t callee_end = parse_simple_new_callee(src, len, new_start);
  if (callee_end <= new_start) return false;
  size_t close = skim_skip_ws(src, len, callee_end);
  if (close >= len || src[close] != ')') return false;
  size_t after = skim_skip_ws(src, len, close + 1);
  if (after >= len || (src[after] != '[' && src[after] != '.')) return false;
  skim_str_putn(out, src + new_start, callee_end - new_start);
  skim_str_puts(out, "()");
  *io = close + 1;
  return true;
}

static bool try_copy_async_arrow_head(skim_str_t *out, const char *src, size_t len, size_t *io) {
  size_t i = *io;
  if (!skim_word_at(src, len, i, "async")) return false;
  size_t after_async = skim_skip_ws(src, len, i + 5);
  if (after_async + 1 < len && src[after_async] == '=' && src[after_async + 1] == '>') {
    skim_str_puts(out, "(async)");
    *io = i + 5;
    return true;
  }
  size_t param_start = 0, param_end = 0;
  size_t after_param = skim_parse_identifier(src, len, i + 5, &param_start, &param_end);
  if (param_start == param_end) return false;
  size_t arrow = skim_skip_ws(src, len, after_param);
  if (arrow + 1 >= len || src[arrow] != '=' || src[arrow + 1] != '>') return false;
  skim_str_puts(out, "async(");
  skim_str_putn(out, src + param_start, param_end - param_start);
  skim_str_puts(out, ")");
  *io = after_param;
  return true;
}

bool skim_transform_try_at(skim_str_t *out, const char *src, size_t len, size_t *io) {
  size_t i = *io;
  if (i > 0 && src[i - 1] == '.' && src[i] != '<') return false;
  switch (src[i]) {
  case '@':
    if (skim_ts_class_try(out, src, len, io)) return true;
    return skim_ts_annotations_try(out, src, len, io);
  case ':':
  case '?':
  case '!':
  case '<':
    return skim_ts_annotations_try(out, src, len, io);
  case 'a':
    if (
      skim_word_at(src, len, i, "as") || skim_word_at(src, len, i, "abstract") || skim_word_at(src, len, i, "accessor")
    ) {
      return skim_ts_annotations_try(out, src, len, io);
    }
    break;
  case 'c':
    if (skim_word_at(src, len, i, "class")) return skim_ts_class_try(out, src, len, io);
    if (skim_word_at(src, len, i, "const")) return skim_ts_enum_try(out, src, len, io);
    break;
  case 'd':
    if (skim_word_at(src, len, i, "declare")) return skim_ts_annotations_try(out, src, len, io);
    break;
  case 'e':
    if (skim_word_at(src, len, i, "enum")) return skim_ts_enum_try(out, src, len, io);
    if (skim_word_at(src, len, i, "export")) {
      size_t j = skim_skip_ws_comments(src, len, i + 6);
      if (skim_word_at(src, len, j, "declare")) return skim_ts_annotations_try(out, src, len, io);
      if (skim_ts_class_try(out, src, len, io)) return true;
      if (skim_ts_namespace_try(out, src, len, io)) return true;
      if (skim_ts_enum_try(out, src, len, io)) return true;
      return skim_ts_module_try(out, src, len, io);
    }
    break;
  case 'f':
    if (skim_word_at(src, len, i, "function")) return skim_ts_annotations_try(out, src, len, io);
    break;
  case 'i':
    if (skim_word_at(src, len, i, "import")) return skim_ts_module_try(out, src, len, io);
    if (skim_word_at(src, len, i, "interface") || skim_word_at(src, len, i, "implements")) {
      return skim_ts_annotations_try(out, src, len, io);
    }
    break;
  case 'm':
    if (skim_word_at(src, len, i, "module")) return skim_ts_namespace_try(out, src, len, io);
    break;
  case 'n':
    if (skim_word_at(src, len, i, "namespace")) return skim_ts_namespace_try(out, src, len, io);
    break;
  case 'p':
    if (
      skim_word_at(src, len, i, "public") || skim_word_at(src, len, i, "private") ||
      skim_word_at(src, len, i, "protected")
    ) {
      return skim_ts_annotations_try(out, src, len, io);
    }
    break;
  case 'r':
    if (skim_word_at(src, len, i, "readonly")) return skim_ts_annotations_try(out, src, len, io);
    break;
  case 's':
    if (skim_word_at(src, len, i, "satisfies")) return skim_ts_annotations_try(out, src, len, io);
    break;
  case 't':
    if (skim_word_at(src, len, i, "type") || skim_word_at(src, len, i, "this"))
      return skim_ts_annotations_try(out, src, len, io);
    break;
  }

  if (
    (skim_options.remove_class_fields_without_initializer || skim_options.allow_declare_fields_false) &&
    skim_is_id_start(src[i]) && skim_ts_annotations_try(out, src, len, io)
  ) {
    return true;
  }
  if (next_non_ws_is_member_access(src, len, i) && skim_ts_enum_ref_try(out, src, len, io)) return true;
  return false;
}

static void
transform_range_impl(const char *src, size_t len, size_t start, size_t end, skim_str_t *out, bool allow_ts) {
  for (size_t i = start; i < end;) {
    if (src[i] == '\'' || src[i] == '"' || src[i] == '`') {
      if (src[i] == '`') trim_tagged_template_space(out);
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
    if (src[i] == '/' && slash_starts_regex(src, start, i)) {
      i = copy_regex_literal(out, src, len, i);
      continue;
    }
    if (legacy_octal_literal_at(src, len, i)) {
      i = copy_legacy_octal_literal(out, src, len, i);
      continue;
    }
    if (based_number_literal_at(src, len, i)) {
      i = copy_based_number_literal(out, src, len, i);
      continue;
    }
    if (decimal_number_literal_at(src, len, i)) {
      i = copy_decimal_number_literal(out, src, len, i);
      continue;
    }
    if (src[i] == '(' && try_copy_parenthesized_argless_new(out, src, len, &i)) continue;
    if (skim_word_at(src, len, i, "new") && try_copy_argless_new(out, src, len, &i)) continue;
    if (skim_word_at(src, len, i, "async") && try_copy_async_arrow_head(out, src, len, &i)) continue;
    if (try_copy_shorthand_property(out, src, len, start, &i)) continue;
    if (allow_ts && skim_transform_try_at(out, src, len, &i)) continue;
    if (src[i] == ',' && trailing_parameter_comma_at(src, len, i)) {
      i++;
      continue;
    }
    if (skim_emit_copy_plain_identifier(out, src, end, &i)) continue;
    if (skim_emit_copy_plain_span(out, src, end, &i)) continue;
    skim_str_putc(out, src[i++]);
  }
}

void skim_transform_range(const char *src, size_t len, size_t start, size_t end, skim_str_t *out) {
  transform_range_impl(src, len, start, end, out, true);
}

void skim_transform_js_range(const char *src, size_t len, size_t start, size_t end, skim_str_t *out) {
  transform_range_impl(src, len, start, end, out, false);
}
