#ifndef SKIM_CHARS_H
#define SKIM_CHARS_H

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

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

#endif
