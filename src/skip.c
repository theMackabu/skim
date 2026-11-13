#include "chars.h"
#include "skip.h"

#include <string.h>

bool skim_has_effective_code(const char *src, size_t len) {
  for (size_t i = 0; i < len;) {
    if (skim_is_space(src[i])) {
      i++;
      continue;
    }
    if (skim_skip_comment(src, len, &i)) continue;
    return true;
  }
  return false;
}

size_t skim_skip_ws_comments(const char *src, size_t len, size_t i) {
  for (;;) {
    i = skim_skip_ws(src, len, i);
    if (skim_skip_comment(src, len, &i)) continue;
    return i;
  }
}

size_t skim_skip_type_alias(const char *src, size_t len, size_t i) {
  int paren = 0, bracket = 0, brace = 0, angle = 0;
  bool seen_type_token = false;
  char last_sig = '\0';
  while (i < len) {
    char c = src[i];
    if (skim_is_space(c) || (c == '/' && i + 1 < len && (src[i + 1] == '/' || src[i + 1] == '*'))) {
      size_t next = skim_skip_ws_comments(src, len, i);
      bool newline = skim_range_has_newline(src, i, next);
      if (newline && paren == 0 && bracket == 0 && brace == 0 && angle == 0 && seen_type_token) {
        bool needs_type =
          last_sig == '=' || last_sig == '|' || last_sig == '&' || last_sig == '?' || last_sig == ':' || last_sig == ',';
        bool continues = next < len && (src[next] == '|' || src[next] == '&' || src[next] == '?' || src[next] == ':');
        if (!needs_type && !continues) return i;
      }
      if (next > i) {
        i = next;
        continue;
      }
    }
    if (c == '\'' || c == '"' || c == '`') {
      seen_type_token = true;
      last_sig = c;
      i = skim_skip_string_raw(src, len, i);
      continue;
    }
    if (c == '(') paren++;
    else if (c == ')') {
      if (paren == 0) return i;
      paren--;
    }
    else if (c == '[') bracket++;
    else if (c == ']') {
      if (bracket == 0) return i;
      bracket--;
    }
    else if (c == '{') brace++;
    else if (c == '}') {
      if (brace == 0) return i;
      brace--;
    }
    else if (c == '<') angle++;
    else if (c == '>' && angle > 0 && (i == 0 || src[i - 1] != '=')) angle--;
    else if (c == ';' && paren == 0 && bracket == 0 && brace == 0 && angle == 0) return i + 1;
    if (!skim_is_space(c)) {
      seen_type_token = true;
      last_sig = c;
    }
    i++;
  }
  return i;
}

bool skim_slash_starts_regex(const char *src, size_t start, size_t i) {
  bool saw_newline = false;
  for (;;) {
    while (i > start && skim_is_space(src[i - 1])) {
      if (src[i - 1] == '\n' || src[i - 1] == '\r') saw_newline = true;
      i--;
    }
    if (i < start + 2 || src[i - 2] != '*' || src[i - 1] != '/') break;
    size_t comment = i - 2;
    while (comment > start && !(src[comment - 1] == '/' && src[comment] == '*'))
      comment--;
    if (comment == start) break;
    i = comment - 1;
  }
  if (i == start) return true;
  if (saw_newline) return true;
  size_t word_start = i;
  while (word_start > start && skim_is_id_part(src[word_start - 1]))
    word_start--;
  if (word_start < i && (word_start == start || src[word_start - 1] != '.')) {
    static const char *words[] = {
      "return", "throw", "yield", "await", "case", "delete", "void", "typeof", "in", "instanceof"
    };
    for (size_t k = 0; k < sizeof(words) / sizeof(words[0]); k++) {
      size_t n = strlen(words[k]);
      if (i - word_start == n && memcmp(src + word_start, words[k], n) == 0) return true;
    }
  }
  char c = src[i - 1];
  if ((c == '-' || c == '+') && i >= 2 && src[i - 2] == c) return false;
  return c == '(' || c == '[' || c == '{' || c == ',' || c == ';' || c == ':' || c == '=' || c == '!' || c == '?' ||
         c == '&' || c == '|' || c == '+' || c == '-' || c == '*' || c == '~' || c == '^' || c == '<' || c == '>';
}

size_t skim_skip_regex_literal(const char *src, size_t len, size_t i) {
  bool in_class = false;
  i++;
  while (i < len) {
    char c = src[i++];
    if (c == '\\' && i < len) {
      i++;
      continue;
    }
    if (c == '[') in_class = true;
    else if (c == ']') in_class = false;
    else if (c == '/' && !in_class) break;
    else if (c == '\n' || c == '\r') break;
  }
  while (i < len && skim_is_id_part(src[i]))
    i++;
  return i;
}

size_t skim_skip_string_raw(const char *src, size_t len, size_t i) {
  char quote = src[i++];
  for (;;) {
    while (i < len && !skim_char_has(src[i], SKIM_CC_STRING_STOP))
      i++;
    if (i >= len) break;
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
  size_t start = i;
  int depth = 0;
  while (i < len) {
    if (!skim_char_has(src[i], SKIM_CC_BALANCED)) {
      i++;
      continue;
    }
    if (src[i] == '\'' || src[i] == '"' || src[i] == '`') {
      i = skim_skip_string_raw(src, len, i);
      continue;
    }
    if (skim_skip_comment(src, len, &i)) continue;
    if (src[i] == '/' && skim_slash_starts_regex(src, start, i)) {
      i = skim_skip_regex_literal(src, len, i);
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
      i = skim_skip_line_comment(src, len, i);
      return i;
    }
    if (i + 1 < len && src[i] == '/' && src[i + 1] == '*') {
      i = skim_skip_block_comment(src, len, i + 2);
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

