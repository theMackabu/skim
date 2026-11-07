#ifndef SKIM_INTERNAL_H
#define SKIM_INTERNAL_H

#include "skim.h"

#include <stdint.h>
#include <string.h>

typedef struct {
  char *data;
  size_t len;
  size_t cap;
} skim_str_t;

typedef struct {
  size_t *decl_buckets;
  size_t *known_member_buckets;
} skim_transform_state_t;

typedef struct {
  uint32_t close;
  uint32_t open;
} skim_bracket_pair_t;

typedef struct {
  const char *src;
  size_t len;
  skim_bracket_pair_t *pairs;
  uint32_t *pair_buckets;
  bool pairs_built;
} skim_scan_cache_t;

extern skim_options_t skim_options;
extern skim_source_mode_t skim_source_mode;
extern skim_transform_state_t *skim_state;
extern size_t skim_ts_enum_inline_member_count;

#define SKIM_BUCKET_COUNT 4096u
#define SKIM_DECL_VALUE 1u
#define SKIM_DECL_ENUM 2u
#define SKIM_DECL_NAMESPACE 4u

void skim_scan_cache_enter(skim_scan_cache_t *saved, const char *src, size_t len);
void skim_scan_cache_leave(const skim_scan_cache_t *saved);
bool skim_scan_cache_owns(const char *src);
bool skim_back_skip_block(const char *src, size_t *k);
bool skim_colon_looks_like_ternary(const char *src, size_t colon);
bool skim_colon_looks_like_case_label(const char *src, size_t colon);

void skim_decl_reset(void);
void skim_transform_state_free(skim_transform_state_t *state);
void skim_decl_remember(const char *src, const char *name_start, const char *name_end, size_t pos, unsigned flags);
bool skim_decl_seen_before(const char *src, const char *name, size_t pos, unsigned flags);

void skim_die(const char *msg);
void skim_str_grow(skim_str_t *s, size_t extra);

static inline void skim_str_reserve(skim_str_t *s, size_t extra) {
  if (s->len + extra + 1 > s->cap) skim_str_grow(s, extra);
}

static inline void skim_str_putc(skim_str_t *s, char c) {
  skim_str_reserve(s, 1);
  s->data[s->len++] = c;
  s->data[s->len] = '\0';
}

static inline void skim_str_putn(skim_str_t *s, const char *p, size_t n) {
  skim_str_reserve(s, n);
  memcpy(s->data + s->len, p, n);
  s->len += n;
  s->data[s->len] = '\0';
}

static inline void skim_str_puts(skim_str_t *s, const char *p) {
  skim_str_putn(s, p, strlen(p));
}

char *skim_slice_dup(const char *src, size_t start, size_t end);
bool skim_has_effective_code(const char *src, size_t len);
bool skim_has_module_syntax(const char *src, size_t len);
bool skim_source_has_module_word(const char *src, size_t len);
bool skim_source_needs_module_marker(const char *src, size_t len);

#define SKIM_CC_ID_START 1u
#define SKIM_CC_ID_PART 2u
#define SKIM_CC_SPACE 4u
#define SKIM_CC_DIGIT 8u
#define SKIM_CC_EMIT_DISPATCH 16u
#define SKIM_CC_CLEANUP_SPECIAL 32u
#define SKIM_CC_BRACKET 64u
#define SKIM_CC_MARKER 128u
#define SKIM_CC_BACK_STOP 256u
#define SKIM_CC_BALANCED 512u
#define SKIM_CC_ITEM_END 1024u
#define SKIM_CC_DECL_START 2048u
#define SKIM_CC_ANGLE 4096u

extern const unsigned short skim_char_class[256];

static inline bool skim_char_has(char c, unsigned flags) {
  return skim_char_class[(unsigned char)c] & flags;
}

static inline bool skim_is_id_start(char c) {
  return skim_char_class[(unsigned char)c] & SKIM_CC_ID_START;
}

static inline bool skim_is_id_part(char c) {
  return skim_char_class[(unsigned char)c] & SKIM_CC_ID_PART;
}

static inline bool skim_is_space(char c) {
  return skim_char_class[(unsigned char)c] & SKIM_CC_SPACE;
}

static inline bool skim_is_digit(char c) {
  return skim_char_class[(unsigned char)c] & SKIM_CC_DIGIT;
}

static inline bool skim_word_at(const char *src, size_t len, size_t i, const char *word) {
  if (i >= len || src[i] != word[0]) return false;
  size_t n = strlen(word);
  if (i + n > len || memcmp(src + i, word, n) != 0) return false;
  if (i > 0 && skim_is_id_part(src[i - 1])) return false;
  return i + n == len || !skim_is_id_part(src[i + n]);
}

static inline bool skim_word_before(const char *src, size_t pos, const char *word) {
  while (pos > 0 && skim_is_space(src[pos - 1]))
    pos--;
  size_t end = pos;
  while (pos > 0 && skim_is_id_part(src[pos - 1]))
    pos--;
  size_t n = strlen(word);
  return end - pos == n && memcmp(src + pos, word, n) == 0;
}

static inline bool skim_range_has_newline(const char *src, size_t start, size_t end) {
  for (size_t i = start; i < end; i++) {
    if (src[i] == '\n' || src[i] == '\r') return true;
  }
  return false;
}

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

size_t skim_scan_cache_len(void);
bool skim_slash_starts_regex(const char *src, size_t start, size_t i);
size_t skim_skip_ws_comments_slow(const char *src, size_t len, size_t i);

static inline size_t skim_skip_ws(const char *src, size_t len, size_t i) {
  while (i < len && skim_is_space(src[i]))
    i++;
  return i;
}

static inline size_t skim_skip_ws_comments(const char *src, size_t len, size_t i) {
  if (i >= len || (!skim_is_space(src[i]) && src[i] != '/')) return i;
  return skim_skip_ws_comments_slow(src, len, i);
}

size_t skim_skip_regex_literal(const char *src, size_t len, size_t i);
size_t skim_skip_string_raw(const char *src, size_t len, size_t i);
size_t skim_skip_balanced(const char *src, size_t len, size_t i, char open, char close);
size_t skim_skip_type_alias(const char *src, size_t len, size_t i);
size_t skim_skip_statement_like(const char *src, size_t len, size_t i);
size_t skim_parse_identifier(const char *src, size_t len, size_t i, size_t *start, size_t *end);

void skim_emit_preserved_newlines(skim_str_t *out, const char *src, size_t from, size_t to);
bool skim_decimal_number_literal_at(const char *src, size_t len, size_t i);
bool skim_based_number_literal_at(const char *src, size_t len, size_t i);

size_t skim_copy_decimal_number_literal(skim_str_t *out, const char *src, size_t len, size_t i, bool in_expression);
size_t skim_copy_based_number_literal(skim_str_t *out, const char *src, size_t len, size_t i, bool in_expression);
size_t skim_copy_string(skim_str_t *out, const char *src, size_t len, size_t i);
size_t skim_copy_line_comment(skim_str_t *out, const char *src, size_t len, size_t i);
size_t skim_copy_block_comment(skim_str_t *out, const char *src, size_t len, size_t i);

static inline bool skim_emit_copy_plain_identifier(skim_str_t *out, const char *src, size_t end, size_t *io) {
  size_t i = *io;
  if (i >= end || !skim_is_id_start(src[i])) return false;
  skim_str_reserve(out, end - i);
  char *dst = out->data + out->len;
  size_t j = i;
  do {
    *dst++ = src[j++];
  } while (j < end && skim_is_id_part(src[j]));
  *dst = '\0';
  out->len += j - i;
  *io = j;
  return true;
}

static inline bool skim_emit_copy_plain_span(skim_str_t *out, const char *src, size_t end, size_t *io) {
  size_t i = *io;
  if (i >= end || skim_char_has(src[i], SKIM_CC_EMIT_DISPATCH)) return false;
  skim_str_reserve(out, end - i);
  char *dst = out->data + out->len;
  size_t j = i;
  do {
    *dst++ = src[j++];
  } while (j < end && !skim_char_has(src[j], SKIM_CC_EMIT_DISPATCH));
  *dst = '\0';
  out->len += j - i;
  *io = j;
  return true;
}

char *skim_transform_typescript(const char *src, size_t len);
void skim_transform_typescript_into(const char *src, size_t len, skim_str_t *out, skim_str_t *scratch);
void skim_transform_range(const char *src, size_t len, size_t start, size_t end, skim_str_t *out);
bool skim_transform_try_at(skim_str_t *out, const char *src, size_t len, size_t *io);

void skim_ts_annotations_memo_invalidate(void);
bool skim_ts_annotations_try(skim_str_t *out, const char *src, size_t len, size_t *io);
void skim_ts_module_index_invalidate(void);
bool skim_ts_module_try(skim_str_t *out, const char *src, size_t len, size_t *io);
bool skim_ts_enum_try(skim_str_t *out, const char *src, size_t len, size_t *io);
bool skim_ts_enum_ref_try(skim_str_t *out, const char *src, size_t len, size_t *io);

void skim_ts_enum_reset(void);
void skim_ts_enum_scope_invalidate(void);
void skim_ts_enum_suppress_push(void);
void skim_ts_enum_suppress_pop(void);

void skim_ts_namespace_reset(void);
void skim_ts_namespace_decl_index_invalidate(void);
bool skim_ts_namespace_try(skim_str_t *out, const char *src, size_t len, size_t *io);
bool skim_ts_class_try(skim_str_t *out, const char *src, size_t len, size_t *io);

#endif
