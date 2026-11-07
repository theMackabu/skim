#include "chars.h"
#include "skip.h"
#include "transform.h"

#include <string.h>

static inline bool scan_skip_trivia_or_string(const char *src, size_t len, size_t *io) {
  size_t i = *io;
  if (src[i] == '\'' || src[i] == '"' || src[i] == '`') {
    *io = skim_skip_string_raw(src, len, i);
    return true;
  }
  return skim_skip_comment(src, len, io);
}

static inline size_t scan_next_candidate(const char *src, size_t len, size_t i) {
  while (i < len && !skim_char_has(src[i], SKIM_CC_MARKER))
    i++;
  return i;
}

bool skim_source_has_module_word(const char *src, size_t len) {
  for (size_t i = 0; i < len; i++) {
    const char *p = memchr(src + i, 'p', len - i);
    if (!p) return false;
    i = (size_t)(p - src);
    if (i < 2 || len - i < 4 || memcmp(src + i + 1, "ort", 3) != 0) continue;
    if ((src[i - 2] == 'i' && src[i - 1] == 'm') || (src[i - 2] == 'e' && src[i - 1] == 'x')) return true;
  }
  return false;
}

bool skim_has_module_syntax(const char *src, size_t len) {
  for (size_t i = scan_next_candidate(src, len, 0); i < len; i = scan_next_candidate(src, len, i)) {
    if (scan_skip_trivia_or_string(src, len, &i)) continue;
    if (skim_word_at(src, len, i, "import") || skim_word_at(src, len, i, "export")) return true;
    i++;
  }
  return false;
}

bool skim_source_needs_module_marker(const char *src, size_t len) {
  size_t depth = 0;
  for (size_t i = 0; i < len;) {
    while (i < len && !skim_char_has(src[i], SKIM_CC_MARKER | SKIM_CC_BRACKET))
      i++;
    if (i >= len) break;
    if (scan_skip_trivia_or_string(src, len, &i)) continue;
    if (src[i] == '/' && skim_slash_starts_regex(src, 0, i)) {
      i = skim_skip_regex_literal(src, len, i);
      continue;
    }
    if (src[i] == '{') depth++;
    else if (src[i] == '}' && depth > 0) depth--;
    if (depth == 0 && skim_word_at(src, len, i, "import")) {
      size_t j = skim_skip_ws_comments(src, len, i + 6);
      if (skim_word_at(src, len, j, "type")) return true;
      if (j >= len || !skim_is_id_start(src[j])) return true;
      size_t start = 0, end = 0;
      j = skim_skip_ws_comments(src, len, skim_parse_identifier(src, len, j, &start, &end));
      if (j >= len || src[j] != '=') return true;
    } else if (depth == 0 && skim_word_at(src, len, i, "export")) {
      size_t j = skim_skip_ws_comments(src, len, i + 6);
      if (
        j < len && src[j] != '=' && !skim_word_at(src, len, j, "as") && !skim_word_at(src, len, j, "namespace") &&
        !skim_word_at(src, len, j, "module")
      )
        return true;
    }
    i++;
  }
  return false;
}
