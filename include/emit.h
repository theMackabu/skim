#ifndef SKIM_EMIT_H
#define SKIM_EMIT_H

#include "chars.h"
#include "str.h"

void skim_transform_range(const char *src, size_t len, size_t start, size_t end, skim_str_t *out);

void skim_emit_preserved_newlines(skim_str_t *out, const char *src, size_t from, size_t to);
size_t skim_copy_string(skim_str_t *out, const char *src, size_t len, size_t i);
size_t skim_copy_line_comment(skim_str_t *out, const char *src, size_t len, size_t i);
size_t skim_copy_block_comment(skim_str_t *out, const char *src, size_t len, size_t i);

bool skim_legacy_octal_literal_at(const char *src, size_t len, size_t i);
size_t skim_copy_legacy_octal_literal(skim_str_t *out, const char *src, size_t len, size_t i);
bool skim_decimal_number_literal_at(const char *src, size_t len, size_t i);
size_t skim_copy_decimal_number_literal(skim_str_t *out, const char *src, size_t len, size_t i, bool in_expression);
bool skim_based_number_literal_at(const char *src, size_t len, size_t i);
size_t skim_copy_based_number_literal(skim_str_t *out, const char *src, size_t len, size_t i, bool in_expression);

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

#endif
