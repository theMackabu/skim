#include "emit.h"
#include "skip.h"
#include "str.h"

#include <string.h>

void skim_emit_preserved_newlines(skim_str_t *out, const char *src, size_t from, size_t to) {
  while (from < to) {
    const char *newline = memchr(src + from, '\n', to - from);
    if (!newline) return;
    skim_str_putc(out, '\n');
    from = (size_t)(newline - src) + 1;
  }
}

size_t skim_copy_string(skim_str_t *out, const char *src, size_t len, size_t i) {
  char quote = src[i++];
  skim_str_putc(out, quote);
  while (i < len) {
    size_t run = i;
    while (run < len && src[run] != quote && src[run] != '\\' && src[run] != '$' && src[run] != '\0')
      run++;
    if (run > i) {
      skim_str_putn(out, src + i, run - i);
      i = run;
      if (i >= len) break;
    }
    char c = src[i++];
    if (c == '\0') {
      skim_str_puts(out, "\\0");
      continue;
    }
    if (c == '\\' && i < len && quote != '`') {
      if (src[i] == '\n' || src[i] == '\r') {
        if (src[i] == '\r' && i + 1 < len && src[i + 1] == '\n') i += 2;
        else i++;
        while (i < len && (src[i] == ' ' || src[i] == '\t'))
          i++;
        continue;
      }
      if (src[i] == 'u' && i + 2 < len && src[i + 1] == '{') {
        size_t j = i + 2;
        unsigned value = 0;
        bool ok = false;
        while (j < len && src[j] != '}') {
          int digit = -1;
          if (src[j] >= '0' && src[j] <= '9') digit = src[j] - '0';
          else if (src[j] >= 'a' && src[j] <= 'f') digit = src[j] - 'a' + 10;
          else if (src[j] >= 'A' && src[j] <= 'F') digit = src[j] - 'A' + 10;
          else break;
          value = value * 16u + (unsigned)digit;
          ok = true;
          j++;
        }
        if (ok && j < len && src[j] == '}' && value == 0) {
          skim_str_puts(out, "\\0");
          i = j + 1;
          continue;
        }
      }
      if (
        src[i] == 'u' && i + 4 < len && src[i + 1] == '0' && src[i + 2] == '0' && src[i + 3] == '0' && src[i + 4] == '0'
      ) {
        skim_str_puts(out, "\\0");
        i += 5;
        continue;
      }
      if (src[i] == 'x' && i + 2 < len && src[i + 1] == '0' && src[i + 2] == '0') {
        skim_str_puts(out, "\\0");
        i += 3;
        continue;
      }
      skim_str_putc(out, c);
      skim_str_putc(out, src[i++]);
      continue;
    }
    skim_str_putc(out, c);
    if (quote == '`' && c == '$' && i < len && src[i] == '{') {
      skim_str_putc(out, src[i++]);
      size_t expr_start = i;
      size_t expr_end = skim_skip_balanced(src, len, i - 1, '{', '}');
      if (expr_end == 0 || expr_end <= expr_start) continue;
      skim_transform_range(src, len, expr_start, expr_end - 1, out);
      skim_str_putc(out, '}');
      i = expr_end;
      continue;
    }
    if (c == quote) break;
  }
  return i;
}

size_t skim_copy_line_comment(skim_str_t *out, const char *src, size_t len, size_t i) {
  if (i >= len) return i;
  const char *newline = memchr(src + i, '\n', len - i);
  size_t end = newline ? (size_t)(newline - src) + 1 : len;
  skim_str_putn(out, src + i, end - i);
  return end;
}

size_t skim_copy_block_comment(skim_str_t *out, const char *src, size_t len, size_t i) {
  skim_str_putn(out, src + i, 2);
  i += 2;
  if (i >= len) return i;
  size_t close = skim_find_block_comment_close(src, len, i);
  size_t end = close < len ? close + 2 : len;
  skim_str_putn(out, src + i, end - i);
  return end;
}
