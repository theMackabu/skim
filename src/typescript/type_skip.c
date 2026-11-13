#include "chars.h"
#include "skip.h"
#include "type_syntax.h"

static size_t skip_type_tail_impl(const char *src, size_t len, size_t i, bool stop_before_arrow) {
  int paren = 0, bracket = 0, brace = 0, angle = 0;
  bool seen_type_token = false;
  bool after_arrow = false;
  while (i < len) {
    char c = src[i];
    if ((c == '\n' || c == '\r') && paren == 0 && bracket == 0 && brace == 0 && angle == 0 && seen_type_token) break;
    if (skim_is_space(c)) {
      i++;
      continue;
    }
    if (c == '\'' || c == '"' || c == '`') {
      seen_type_token = true;
      i = skim_skip_string_raw(src, len, i);
      continue;
    }
    if (i + 1 < len && c == '/' && src[i + 1] == '/') {
      if (paren == 0 && bracket == 0 && brace == 0 && angle == 0) break;
      i += 2;
      i = skim_skip_line_comment(src, len, i);
      continue;
    }
    if (i + 1 < len && c == '/' && src[i + 1] == '*') {
      i = skim_skip_block_comment(src, len, i + 2);
      continue;
    }
    if (paren == 0 && bracket == 0 && brace == 0 && angle == 0 && skim_word_any(src, len, i, "of\0in\0")) break;
    if (paren == 0 && bracket == 0 && brace == 0 && angle == 0 && seen_type_token && skim_word_at(src, len, i, "as"))
      break;
    if (c == '(') paren++;
    else if (c == ')' && paren-- <= 0) break;
    else if (c == '[') bracket++;
    else if (c == ']' && bracket-- <= 0) break;
    else if (c == '{') {
      if (paren == 0 && bracket == 0 && angle == 0 && brace == 0 && seen_type_token && !after_arrow) {
        size_t p = i;
        while (p > 0 && skim_is_space(src[p - 1]))
          p--;
        char prev = p > 0 ? src[p - 1] : '\0';
        if (prev != '|' && prev != '&' && prev != ',' && prev != '(' && prev != '<') break;
      }
      brace++;
    } else if (c == '}' && brace-- <= 0) break;
    else if (c == '<') angle++;
    else if (c == '>' && angle > 0) angle--;
    else if (c == '=' && i + 1 < len && src[i + 1] == '>') {
      if (stop_before_arrow && paren == 0 && bracket == 0 && brace == 0 && angle == 0) break;
      i += 2;
      seen_type_token = true;
      after_arrow = true;
      continue;
    } else if (paren == 0 && bracket == 0 && brace == 0 && angle == 0) {
      if ((c == '|' && i + 1 < len && src[i + 1] == '|') || (c == '&' && i + 1 < len && src[i + 1] == '&')) break;
      if (c == ',' || c == ';' || c == '=' || c == '\n') break;
    }
    seen_type_token = true;
    after_arrow = false;
    i++;
  }
  return i;
}

size_t skim_ts_skip_type_tail(const char *src, size_t len, size_t i) {
  return skip_type_tail_impl(src, len, i, false);
}

static size_t find_top_level_arrow(const char *src, size_t len, size_t i, size_t end) {
  int paren = 0, bracket = 0, brace = 0, angle = 0;
  while (i + 1 < len && i < end) {
    char c = src[i];
    if (c == '\'' || c == '"' || c == '`') {
      i = skim_skip_string_raw(src, len, i);
      continue;
    }
    if (skim_skip_comment(src, len, &i)) continue;
    if (c == '=' && src[i + 1] == '>') {
      if (paren == 0 && bracket == 0 && brace == 0 && angle == 0) return i;
      i += 2;
      continue;
    }
    if (c == '(') paren++;
    else if (c == ')' && paren > 0) paren--;
    else if (c == '[') bracket++;
    else if (c == ']' && bracket > 0) bracket--;
    else if (c == '{') brace++;
    else if (c == '}' && brace > 0) brace--;
    else if (c == '<') angle++;
    else if (c == '>' && angle > 0) angle--;
    i++;
  }
  return end;
}

size_t skim_ts_skip_return_type_tail(const char *src, size_t len, size_t i) {
  size_t end = skip_type_tail_impl(src, len, i, true);
  return find_top_level_arrow(src, len, i, end);
}

size_t skim_ts_skip_type_declaration_statement(const char *src, size_t len, size_t i) {
  int paren = 0, bracket = 0, brace = 0, angle = 0;
  bool seen_type_token = false;
  char last_sig = '\0';
  while (i < len) {
    char c = src[i];
    if ((c == '\n' || c == '\r') && paren == 0 && bracket == 0 && brace == 0 && angle == 0 && seen_type_token) {
      size_t next = skim_skip_ws(src, len, i + 1);
      if (
        last_sig == '=' || last_sig == '|' || last_sig == '&' || last_sig == '?' || last_sig == ':' || last_sig == ','
      ) {
        i++;
        continue;
      }
      if (
        next < len && (src[next] == '|' || src[next] == '&' || src[next] == '{' || src[next] == '?' || src[next] == ':')
      ) {
        i = next;
        continue;
      }
      return i;
    }
    if (c == '\'' || c == '"' || c == '`') {
      seen_type_token = true;
      i = skim_skip_string_raw(src, len, i);
      continue;
    }
    if (skim_skip_comment(src, len, &i)) continue;
    if (skim_is_space(c)) {
      i++;
      continue;
    }
    if (c == '(') paren++;
    else if (c == ')' && paren-- <= 0) return i;
    else if (c == '[') bracket++;
    else if (c == ']' && bracket-- <= 0) return i;
    else if (c == '{') brace++;
    else if (c == '}' && brace-- <= 0) return i;
    else if (c == '<') angle++;
    else if (c == '>' && angle > 0) angle--;
    else if (c == ';' && paren == 0 && bracket == 0 && brace == 0 && angle == 0) return i + 1;
    seen_type_token = true;
    last_sig = c;
    i++;
  }
  return i;
}

size_t skim_ts_skip_declare_function_statement(const char *src, size_t len, size_t i) {
  int paren = 0, bracket = 0, brace = 0, angle = 0;
  bool saw_params = false;
  while (i < len) {
    char c = src[i];
    if (c == '\'' || c == '"' || c == '`') {
      i = skim_skip_string_raw(src, len, i);
      continue;
    }
    if (skim_skip_comment(src, len, &i)) continue;
    if ((c == '\n' || c == '\r') && saw_params && paren == 0 && bracket == 0 && brace == 0 && angle == 0) return i;
    if (c == '=' && i + 1 < len && src[i + 1] == '>') {
      i += 2;
      continue;
    }
    if (c == '(') {
      paren++;
      saw_params = true;
    } else if (c == ')' && paren > 0) paren--;
    else if (c == '[') bracket++;
    else if (c == ']' && bracket > 0) bracket--;
    else if (c == '{') brace++;
    else if (c == '}' && brace > 0) brace--;
    else if (c == '<') angle++;
    else if (c == '>' && angle > 0) angle--;
    else if (c == ';' && paren == 0 && bracket == 0 && brace == 0 && angle == 0) return i + 1;
    i++;
  }
  return i;
}

size_t skim_ts_find_interface_body_or_semicolon(const char *src, size_t len, size_t i) {
  int paren = 0, bracket = 0, angle = 0;
  while (i < len) {
    char c = src[i];
    if (c == '\'' || c == '"' || c == '`') {
      i = skim_skip_string_raw(src, len, i);
      continue;
    }
    if (skim_skip_comment(src, len, &i)) continue;
    if (angle > 0 && c == '{') {
      i = skim_skip_balanced(src, len, i, '{', '}');
      continue;
    }
    if (c == '(') paren++;
    else if (c == ')' && paren > 0) paren--;
    else if (c == '[') bracket++;
    else if (c == ']' && bracket > 0) bracket--;
    else if (c == '<') angle++;
    else if (c == '>' && angle > 0) angle--;
    else if ((c == '{' || c == ';') && paren == 0 && bracket == 0 && angle == 0) return i;
    i++;
  }
  return i;
}

size_t skim_ts_skip_angle_type_list(const char *src, size_t len, size_t i, bool strict) {
  int depth = 0;
  int paren = 0, bracket = 0, brace = 0;
  while (i < len) {
    if (!skim_char_has(src[i], SKIM_CC_ANGLE)) {
      i++;
      continue;
    }
    if (src[i] == '\'' || src[i] == '"' || src[i] == '`') {
      i = skim_skip_string_raw(src, len, i);
      continue;
    }
    if (strict && depth > 0 && paren == 0 && bracket == 0 && brace == 0 && src[i] == ';') return len + 1;
    if (src[i] == '(') paren++;
    else if (src[i] == ')' && paren > 0) paren--;
    else if (src[i] == '[') bracket++;
    else if (src[i] == ']' && bracket > 0) bracket--;
    else if (src[i] == '{') brace++;
    else if (src[i] == '}' && brace > 0) brace--;
    else if (src[i] == '=' && i + 1 < len && src[i + 1] == '>') i++;
    else if (src[i] == '<') depth++;
    else if (src[i] == '>' && depth > 0) {
      if (depth > 1) {
        depth--;
      } else if (paren == 0 && bracket == 0 && brace == 0) {
        depth--;
        return i + 1;
      }
    }
    i++;
  }
  return !strict || depth == 0 ? i : len + 1;
}
