#include "chars.h"

enum {
#define SKIM_CHAR_GROUP(name, flags) CC_##name = flags,
#include "char_group_list.h"
};

const unsigned short skim_char_class[256] = {
#define SKIM_CHAR_CLASS(lo, hi, cls) [lo ... hi] = cls,
#include "char_class_list.h"
};

bool skim_word_at_slow(const char *src, size_t len, size_t i, const char *word, size_t n) {
  if (i + n > len || memcmp(src + i, word, n) != 0) return false;
  if (i > 0 && skim_is_id_part(src[i - 1])) return false;
  return i + n == len || !skim_is_id_part(src[i + n]);
}

bool skim_word_any(const char *src, size_t len, size_t i, const char *words) {
  if (i >= len || !skim_is_id_start(src[i])) return false;
  if (i > 0 && skim_is_id_part(src[i - 1])) return false;
  size_t n = 1;
  while (i + n < len && skim_is_id_part(src[i + n]))
    n++;
  for (; *words; words += strlen(words) + 1) {
    if (*words == src[i] && strlen(words) == n && memcmp(src + i, words, n) == 0) return true;
  }
  return false;
}

bool skim_word_before(const char *src, size_t pos, const char *word) {
  while (pos > 0 && skim_is_space(src[pos - 1]))
    pos--;
  size_t end = pos;
  while (pos > 0 && skim_is_id_part(src[pos - 1]))
    pos--;
  size_t n = strlen(word);
  return end - pos == n && memcmp(src + pos, word, n) == 0;
}
