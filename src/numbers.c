#include "chars.h"
#include "emit.h"
#include "skip.h"
#include "str.h"

#include <stdio.h>
#include <string.h>

bool skim_legacy_octal_literal_at(const char *src, size_t len, size_t i) {
  if (i + 1 >= len || src[i] != '0' || !skim_is_digit(src[i + 1])) return false;
  if (i > 0 && (skim_is_id_part(src[i - 1]) || src[i - 1] == '.')) return false;
  return true;
}

size_t skim_copy_legacy_octal_literal(skim_str_t *out, const char *src, size_t len, size_t i) {
  size_t j = i;
  unsigned long long value = 0;
  bool octal = true;
  while (j < len && skim_is_digit(src[j])) {
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

bool skim_decimal_number_literal_at(const char *src, size_t len, size_t i) {
  if (i >= len || !skim_is_digit(src[i])) return false;
  if (i > 0 && (skim_is_id_part(src[i - 1]) || src[i - 1] == '.')) return false;
  return true;
}

static size_t count_digits_stripped(const char *src, size_t start, size_t end) {
  size_t count = 0;
  for (size_t i = start; i < end; i++) {
    if (skim_is_digit(src[i])) count++;
  }
  return count;
}

static void put_digits_stripped(skim_str_t *out, const char *src, size_t start, size_t end) {
  for (size_t i = start; i < end; i++) {
    if (skim_is_digit(src[i])) skim_str_putc(out, src[i]);
  }
}

static void put_n_digits_stripped(skim_str_t *out, const char *src, size_t start, size_t end, size_t n) {
  for (size_t i = start; i < end && n > 0; i++) {
    if (skim_is_digit(src[i])) {
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
    if (!skim_is_digit(src[i])) continue;
    saw_digit = true;
    if (src[i] != '0') return false;
  }
  return saw_digit;
}

static bool stripped_digits_all_zero(const char *src, size_t start, size_t end) {
  for (size_t i = start; i < end; i++) {
    if (skim_is_digit(src[i]) && src[i] != '0') return false;
  }
  return true;
}

static long long parse_exponent_digits(const char *src, size_t start, size_t end, bool negative) {
  long long value = 0;
  for (size_t i = start; i < end; i++) {
    if (!skim_is_digit(src[i])) continue;
    if (value < 1000000000ll) value = value * 10 + (long long)(src[i] - '0');
  }
  return negative ? -value : value;
}

static void
maybe_put_number_member_separator(skim_str_t *out, const char *src, size_t len, size_t i, bool in_expression) {
  if (in_expression && i + 1 < len && src[i] == '.' && skim_is_id_start(src[i + 1])) skim_str_putc(out, ' ');
}

static void put_js_scientific_number(skim_str_t *out, long double value) {
  char buf[128];
  snprintf(buf, sizeof(buf), "%.15Le", value);
  char digits[32];
  size_t digit_count = 0;
  size_t e = 0;
  for (; buf[e] && buf[e] != 'e' && buf[e] != 'E'; e++) {
    if (skim_is_digit(buf[e])) digits[digit_count++] = buf[e];
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
      if (skim_is_digit(buf[e])) exp_value = exp_value * 10 + (buf[e] - '0');
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

size_t skim_copy_decimal_number_literal(skim_str_t *out, const char *src, size_t len, size_t i, bool in_expression) {
  size_t j = i;
  while (j < len && (skim_is_digit(src[j]) || src[j] == '_'))
    j++;
  size_t int_end = j;
  if (j < len && src[j] == 'n') {
    put_digits_stripped(out, src, i, j);
    skim_str_putc(out, 'n');
    return j + 1;
  }
  if (in_expression && j < len && src[j] == '.' && j + 1 < len && src[j + 1] == '.') {
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
    while (frac_end < len && (skim_is_digit(src[frac_end]) || src[frac_end] == '_'))
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
    while (exp < len && (skim_is_digit(src[exp]) || src[exp] == '_'))
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
        maybe_put_number_member_separator(out, src, len, j, in_expression);
        return j;
      }
      put_digits_stripped(out, src, i, has_fraction ? frac_end : int_end);
      if (exponent != 0) {
        char buf[48];
        snprintf(buf, sizeof(buf), "e%lld", exponent);
        skim_str_puts(out, buf);
      }
      maybe_put_number_member_separator(out, src, len, j, in_expression);
      return j;
    }
    if (stripped_digits_all_zero(src, frac_start, frac_end)) {
      put_digits_stripped(out, src, i, frac_start - 1);
      maybe_put_number_member_separator(out, src, len, j, in_expression);
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
    maybe_put_number_member_separator(out, src, len, j, in_expression);
    return j;
  }

  size_t int_len = count_digits_stripped(src, i, j);
  if (in_expression && int_len >= 12 && int_len < 20) {
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
        maybe_put_number_member_separator(out, src, len, j, in_expression);
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
      maybe_put_number_member_separator(out, src, len, j, in_expression);
      return j;
    }
  }

  put_digits_stripped(out, src, i, j);
  maybe_put_number_member_separator(out, src, len, j, in_expression);
  return j;
}

bool skim_based_number_literal_at(const char *src, size_t len, size_t i) {
  if (i + 2 >= len || src[i] != '0') return false;
  if (i > 0 && (skim_is_id_part(src[i - 1]) || src[i - 1] == '.')) return false;
  return src[i + 1] == 'b' || src[i + 1] == 'B' || src[i + 1] == 'o' || src[i + 1] == 'O' || src[i + 1] == 'x' ||
         src[i + 1] == 'X';
}

size_t skim_copy_based_number_literal(skim_str_t *out, const char *src, size_t len, size_t i, bool in_expression) {
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
    maybe_put_number_member_separator(out, src, len, j, in_expression);
    return j;
  }
  char buf[32];
  snprintf(buf, sizeof(buf), "%llu", value);
  skim_str_puts(out, buf);
  if (bigint) {
    skim_str_putc(out, 'n');
    j++;
  }
  maybe_put_number_member_separator(out, src, len, j, in_expression);
  return j;
}
