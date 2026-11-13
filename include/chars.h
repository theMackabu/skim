#ifndef SKIM_CHARS_H
#define SKIM_CHARS_H

#include <stdbool.h>
#include <stdint.h>

enum skim_cc_bit {
#define SKIM_CHAR_FLAG(name) SKIM_CC_BIT_##name,
#include "char_flag_list.h"
};

enum skim_cc : uint8_t {
#define SKIM_CHAR_FLAG(name) SKIM_CC_##name = 1 << SKIM_CC_BIT_##name,
#include "char_flag_list.h"
};

extern const uint8_t skim_char_class[256];

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

#endif
