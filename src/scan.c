#include "skim_internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void skim_die(const char *msg) {
  fprintf(stderr, "%s\n", msg);
  exit(1);
}

void skim_str_reserve(skim_str_t *s, size_t extra) {
  size_t need = s->len + extra + 1;
  if (need <= s->cap) return;
  size_t cap = s->cap ? s->cap : 4096;
  while (cap < need)
    cap *= 2;
  char *next = realloc(s->data, cap);
  if (!next) skim_die("out of memory");
  s->data = next;
  s->cap = cap;
}

void skim_str_putc(skim_str_t *s, char c) {
  skim_str_reserve(s, 1);
  s->data[s->len++] = c;
  s->data[s->len] = '\0';
}

void skim_str_putn(skim_str_t *s, const char *p, size_t n) {
  skim_str_reserve(s, n);
  memcpy(s->data + s->len, p, n);
  s->len += n;
  s->data[s->len] = '\0';
}

void skim_str_puts(skim_str_t *s, const char *p) {
  skim_str_putn(s, p, strlen(p));
}

char *skim_slice_dup(const char *src, size_t start, size_t end) {
  char *s = malloc(end - start + 1);
  if (!s) skim_die("out of memory");
  memcpy(s, src + start, end - start);
  s[end - start] = '\0';
  return s;
}

bool skim_has_effective_code(const char *src, size_t len) {
  for (size_t i = 0; i < len;) {
    if (isspace((unsigned char)src[i])) {
      i++;
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
    return true;
  }
  return false;
}

bool skim_is_id_start(char c) {
  return isalpha((unsigned char)c) || c == '_' || c == '$';
}

bool skim_is_id_part(char c) {
  return isalnum((unsigned char)c) || c == '_' || c == '$';
}

bool skim_word_at(const char *src, size_t len, size_t i, const char *word) {
  size_t n = strlen(word);
  if (i + n > len || memcmp(src + i, word, n) != 0) return false;
  if (i > 0 && skim_is_id_part(src[i - 1])) return false;
  if (i + n < len && skim_is_id_part(src[i + n])) return false;
  return true;
}

size_t skim_skip_ws(const char *src, size_t len, size_t i) {
  while (i < len && isspace((unsigned char)src[i]))
    i++;
  return i;
}

size_t skim_skip_ws_comments(const char *src, size_t len, size_t i) {
  for (;;) {
    i = skim_skip_ws(src, len, i);
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
    return i;
  }
}

size_t skim_skip_string_raw(const char *src, size_t len, size_t i) {
  char quote = src[i++];
  while (i < len) {
    char c = src[i++];
    if (c == '\\' && i < len && quote != '`') {
      i++;
      continue;
    }
    if (quote == '`' && c == '$' && i < len && src[i] == '{') {
      i = skim_skip_balanced(src, len, i, '{', '}');
      continue;
    }
    if (c == quote) break;
  }
  return i;
}

size_t skim_skip_balanced(const char *src, size_t len, size_t i, char open, char close) {
  int depth = 0;
  while (i < len) {
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
    if (src[i] == open) depth++;
    if (src[i] == close) {
      depth--;
      if (depth == 0) return i + 1;
    }
    i++;
  }
  return i;
}

size_t skim_skip_statement_like(const char *src, size_t len, size_t i) {
  while (i < len) {
    if (src[i] == '\'' || src[i] == '"' || src[i] == '`') {
      i = skim_skip_string_raw(src, len, i);
      continue;
    }
    if (i + 1 < len && src[i] == '/' && src[i + 1] == '/') {
      while (i < len && src[i] != '\n')
        i++;
      return i;
    }
    if (i + 1 < len && src[i] == '/' && src[i + 1] == '*') {
      i += 2;
      while (i + 1 < len && !(src[i] == '*' && src[i + 1] == '/'))
        i++;
      if (i + 1 < len) i += 2;
      continue;
    }
    if (src[i] == '{') {
      i = skim_skip_balanced(src, len, i, '{', '}');
      continue;
    }
    if (src[i] == ';') return i + 1;
    i++;
  }
  return i;
}

size_t skim_parse_identifier(const char *src, size_t len, size_t i, size_t *start, size_t *end) {
  i = skim_skip_ws_comments(src, len, i);
  if (i >= len || !skim_is_id_start(src[i])) return i;
  *start = i;
  i++;
  while (i < len && skim_is_id_part(src[i]))
    i++;
  *end = i;
  return i;
}

void skim_emit_preserved_newlines(skim_str_t *out, const char *src, size_t from, size_t to) {
  for (size_t i = from; i < to; i++) {
    if (src[i] == '\n') skim_str_putc(out, '\n');
  }
}

size_t skim_copy_string(skim_str_t *out, const char *src, size_t len, size_t i) {
  char quote = src[i++];
  skim_str_putc(out, quote);
  while (i < len) {
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
  while (i < len) {
    char c = src[i++];
    skim_str_putc(out, c);
    if (c == '\n') break;
  }
  return i;
}

size_t skim_copy_block_comment(skim_str_t *out, const char *src, size_t len, size_t i) {
  skim_str_putn(out, src + i, 2);
  i += 2;
  while (i < len) {
    if (i + 1 < len && src[i] == '*' && src[i + 1] == '/') {
      skim_str_putn(out, src + i, 2);
      return i + 2;
    }
    skim_str_putc(out, src[i++]);
  }
  return i;
}
