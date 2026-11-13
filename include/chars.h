#ifndef SKIM_CHARS_H
#define SKIM_CHARS_H

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

enum skim_cc_bit {
#define SKIM_CHAR_FLAG(name) SKIM_CC_BIT_##name,
#include "char_flag_list.h"
};

enum skim_cc: unsigned short {
#define SKIM_CHAR_FLAG(name) SKIM_CC_##name = 1 << SKIM_CC_BIT_##name,
#include "char_flag_list.h"
};

bool skim_word_at_slow(const char *src, size_t len, size_t i, const char *word, size_t n);
bool skim_word_any(const char *src, size_t len, size_t i, const char *words);
bool skim_word_before(const char *src, size_t pos, const char *word);

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
  return skim_word_at_slow(src, len, i, word, strlen(word));
}

static inline bool skim_range_has_newline(const char *src, size_t start, size_t end) {
  for (size_t i = start; i < end; i++)
    if (src[i] == '\n' || src[i] == '\r') return true;
  return false;
}

#endif
