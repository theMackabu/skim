#ifndef SKIM_SKIP_H
#define SKIM_SKIP_H

#include "chars.h"

bool skim_has_effective_code(const char *src, size_t len);
bool skim_slash_starts_regex(const char *src, size_t start, size_t i);

size_t skim_skip_ws_comments(const char *src, size_t len, size_t i);
size_t skim_skip_regex_literal(const char *src, size_t len, size_t i);
size_t skim_skip_string_raw(const char *src, size_t len, size_t i);
size_t skim_skip_balanced(const char *src, size_t len, size_t i, char open, char close);
size_t skim_skip_type_alias(const char *src, size_t len, size_t i);
size_t skim_skip_statement_like(const char *src, size_t len, size_t i);
size_t skim_parse_identifier(const char *src, size_t len, size_t i, size_t *start, size_t *end);

static inline size_t skim_skip_line_comment(const char *src, size_t len, size_t i) {
  if (i >= len) return i;
  const char *newline = memchr(src + i, '\n', len - i);
  return newline ? (size_t)(newline - src) : len;
}

static inline size_t skim_find_block_comment_close(const char *src, size_t len, size_t i) {
  while (i + 1 < len) {
    const char *star = memchr(src + i, '*', len - 1 - i);
    if (!star) return len;
    i = (size_t)(star - src);
    if (src[i + 1] == '/') return i;
    i++;
  }
  return len;
}

static inline size_t skim_skip_block_comment(const char *src, size_t len, size_t i) {
  size_t close = skim_find_block_comment_close(src, len, i);
  if (close < len) return close + 2;
  return i + 1 < len ? len - 1 : i;
}

static inline bool skim_skip_comment(const char *src, size_t len, size_t *io) {
  size_t i = *io;
  if (i + 1 >= len || src[i] != '/') return false;
  if (src[i + 1] == '/') {
    *io = skim_skip_line_comment(src, len, i + 2);
    return true;
  }
  if (src[i + 1] == '*') {
    *io = skim_skip_block_comment(src, len, i + 2);
    return true;
  }
  return false;
}

static inline size_t skim_skip_ws(const char *src, size_t len, size_t i) {
  while (i < len && skim_is_space(src[i])) i++;
  return i;
}

static inline char skim_prev_non_ws(const char *src, size_t i) {
  while (i > 0 && skim_is_space(src[i - 1]))
    i--;
  return i > 0 ? src[i - 1] : '\0';
}

static inline size_t skim_skip_ws_comments_fast(const char *src, size_t len, size_t i) {
  if (i >= len || (!skim_is_space(src[i]) && src[i] != '/')) return i;
  return skim_skip_ws_comments(src, len, i);
}

#endif
