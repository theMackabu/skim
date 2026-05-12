#include "skim_internal.h"

static bool scan_skip_trivia_or_string(const char *src, size_t len, size_t *io) {
  size_t i = *io;
  if (src[i] == '\'' || src[i] == '"' || src[i] == '`') {
    *io = skim_skip_string_raw(src, len, i);
    return true;
  }
  if (i + 1 < len && src[i] == '/' && src[i + 1] == '/') {
    i += 2;
    while (i < len && src[i] != '\n')
      i++;
    *io = i;
    return true;
  }
  if (i + 1 < len && src[i] == '/' && src[i + 1] == '*') {
    i += 2;
    while (i + 1 < len && !(src[i] == '*' && src[i + 1] == '/'))
      i++;
    if (i + 1 < len) i += 2;
    *io = i;
    return true;
  }
  return false;
}

bool skim_has_module_syntax(const char *src, size_t len) {
  for (size_t i = 0; i < len;) {
    if (scan_skip_trivia_or_string(src, len, &i)) continue;
    if (skim_word_at(src, len, i, "import") || skim_word_at(src, len, i, "export")) return true;
    i++;
  }
  return false;
}

bool skim_has_erasable_import_syntax(const char *src, size_t len) {
  for (size_t i = 0; i < len;) {
    if (scan_skip_trivia_or_string(src, len, &i)) continue;
    if (skim_word_at(src, len, i, "import")) {
      size_t j = skim_skip_ws_comments(src, len, i + 6);
      if (j < len && skim_is_id_start(src[j])) {
        size_t start = 0, end = 0;
        j = skim_parse_identifier(src, len, j, &start, &end);
        j = skim_skip_ws_comments(src, len, j);
        if (j < len && src[j] == '=') {
          i++;
          continue;
        }
      }
      return true;
    }
    i++;
  }
  return false;
}

bool skim_has_export_declare(const char *src, size_t len) {
  for (size_t i = 0; i < len;) {
    if (scan_skip_trivia_or_string(src, len, &i)) continue;
    if (skim_word_at(src, len, i, "export")) {
      size_t j = skim_skip_ws_comments(src, len, i + 6);
      if (skim_word_at(src, len, j, "declare")) return true;
    }
    i++;
  }
  return false;
}

bool skim_has_type_only_module_syntax(const char *src, size_t len) {
  for (size_t i = 0; i < len;) {
    if (scan_skip_trivia_or_string(src, len, &i)) continue;
    if (skim_word_at(src, len, i, "import")) {
      size_t j = skim_skip_ws_comments(src, len, i + 6);
      if (skim_word_at(src, len, j, "type")) return true;
    }
    if (skim_word_at(src, len, i, "export")) {
      size_t j = skim_skip_ws_comments(src, len, i + 6);
      if (skim_word_at(src, len, j, "type")) return true;
    }
    i++;
  }
  return false;
}
