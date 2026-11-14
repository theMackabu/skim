#include "chars.h"
#include "print_internal.h"

void skim_print_number(skim_printer_t *pr, uint32_t token) {
  const char *src = pr->src;
  size_t start = pr->t[token].start;
  size_t end = pr->t[token].end;
  size_t mark = pr->out->len;
  size_t stop = 0;
  if (src[start] == '.') {
    skim_str_putn(pr->out, src + start, end - start);
    return;
  }
  if (skim_legacy_octal_literal_at(src, pr->len, start))
    stop = skim_copy_legacy_octal_literal(pr->out, src, pr->len, start);
  else if (skim_based_number_literal_at(src, pr->len, start))
    stop = skim_copy_based_number_literal(pr->out, src, pr->len, start, true);
  else stop = skim_copy_decimal_number_literal(pr->out, src, pr->len, start, true);
  if (stop == end) return;
  pr->out->len = mark;
  skim_str_putn(pr->out, src + start, end - start);
}

static int hex_digit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static size_t null_escape_end(const char *src, size_t i, size_t end) {
  if (src[i] == 'x' && i + 2 < end && src[i + 1] == '0' && src[i + 2] == '0') return i + 3;
  if (src[i] != 'u') return 0;
  if (i + 4 < end && memcmp(src + i + 1, "0000", 4) == 0) return i + 5;
  if (i + 2 >= end || src[i + 1] != '{') return 0;
  size_t j = i + 2;
  while (j < end && hex_digit(src[j]) == 0)
    j++;
  return j > i + 2 && j < end && src[j] == '}' ? j + 1 : 0;
}

void skim_print_string(skim_printer_t *pr, uint32_t token) {
  const char *src = pr->src;
  size_t i = pr->t[token].start;
  size_t end = pr->t[token].end;
  skim_str_t *out = pr->out;
  while (i < end) {
    char c = src[i++];
    if (c == '\0') {
      skim_str_puts(out, "\\0");
      continue;
    }
    if (c != '\\' || i >= end) {
      skim_str_putc(out, c);
      continue;
    }
    if (src[i] == '\n' || src[i] == '\r') {
      i += src[i] == '\r' && i + 1 < end && src[i + 1] == '\n' ? 2 : 1;
      continue;
    }
    size_t null_end = null_escape_end(src, i, end);
    if (null_end && !(null_end < end && skim_is_digit(src[null_end]))) {
      skim_str_puts(out, "\\0");
      i = null_end;
      continue;
    }
    skim_str_putc(out, c);
    skim_str_putc(out, src[i++]);
  }
}
