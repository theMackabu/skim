#include "skim_internal.h"

#include <ctype.h>
#include <string.h>

static bool range_has_newline(const char *src, size_t start, size_t end) {
  for (size_t i = start; i < end; i++) {
    if (src[i] == '\n' || src[i] == '\r') return true;
  }
  return false;
}

static size_t skip_type_tail_impl(const char *src, size_t len, size_t i, bool stop_before_arrow) {
  int paren = 0, bracket = 0, brace = 0, angle = 0;
  bool seen_type_token = false;
  bool after_arrow = false;
  while (i < len) {
    char c = src[i];
    if ((c == '\n' || c == '\r') && paren == 0 && bracket == 0 && brace == 0 && angle == 0 && seen_type_token) break;
    if (isspace((unsigned char)c)) {
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
      while (i < len && src[i] != '\n')
        i++;
      continue;
    }
    if (i + 1 < len && c == '/' && src[i + 1] == '*') {
      i += 2;
      while (i + 1 < len && !(src[i] == '*' && src[i + 1] == '/'))
        i++;
      if (i + 1 < len) i += 2;
      continue;
    }
    if (
      paren == 0 && bracket == 0 && brace == 0 && angle == 0 &&
      (skim_word_at(src, len, i, "of") || skim_word_at(src, len, i, "in"))
    )
      break;
    if (paren == 0 && bracket == 0 && brace == 0 && angle == 0 && seen_type_token && skim_word_at(src, len, i, "as"))
      break;
    if (c == '(') paren++;
    else if (c == ')' && paren-- <= 0) break;
    else if (c == '[') bracket++;
    else if (c == ']' && bracket-- <= 0) break;
    else if (c == '{') {
      if (paren == 0 && bracket == 0 && angle == 0 && brace == 0 && seen_type_token && !after_arrow) {
        size_t p = i;
        while (p > 0 && isspace((unsigned char)src[p - 1]))
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
    if (!isspace((unsigned char)c)) after_arrow = false;
    i++;
  }
  return i;
}

static size_t skip_type_tail(const char *src, size_t len, size_t i) {
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
    if (i + 1 < len && c == '/' && src[i + 1] == '/') {
      while (i < len && src[i] != '\n')
        i++;
      continue;
    }
    if (i + 1 < len && c == '/' && src[i + 1] == '*') {
      i += 2;
      while (i + 1 < len && !(src[i] == '*' && src[i + 1] == '/'))
        i++;
      if (i + 1 < len) i += 2;
      continue;
    }
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

static size_t skip_return_type_tail(const char *src, size_t len, size_t i) {
  size_t end = skip_type_tail_impl(src, len, i, true);
  return find_top_level_arrow(src, len, i, end);
}

static bool comma_looks_like_param_separator(const char *src, size_t comma);

static char prev_non_ws_char(const char *src, size_t i) {
  while (i > 0 && isspace((unsigned char)src[i - 1]))
    i--;
  return i > 0 ? src[i - 1] : '\0';
}

static char prev_non_ws_same_line_char(const char *src, size_t i) {
  while (i > 0 && (src[i - 1] == ' ' || src[i - 1] == '\t'))
    i--;
  return i > 0 ? src[i - 1] : '\0';
}

static bool this_looks_like_bare_parameter(const char *src, size_t len, size_t i) {
  size_t before = i;
  while (before > 0 && (src[before - 1] == ' ' || src[before - 1] == '\t'))
    before--;
  if (before == 0 || (src[before - 1] != '(' && src[before - 1] != ',')) return false;

  int paren = 0, bracket = 0, brace = 0;
  size_t open = i;
  while (open > 0) {
    char c = src[--open];
    if (c == ')') paren++;
    else if (c == ']') bracket++;
    else if (c == '}') brace++;
    else if (c == '(') {
      if (paren == 0 && bracket == 0 && brace == 0) break;
      if (paren > 0) paren--;
    } else if (c == '[' && bracket > 0) {
      bracket--;
    } else if (c == '{' && brace > 0) {
      brace--;
    }
  }
  if (src[open] != '(') return false;

  size_t close = skim_skip_balanced(src, len, open, '(', ')');
  if (close <= open || close > len) return false;
  size_t after = skim_skip_ws_comments(src, len, close);
  return after < len &&
         (src[after] == '{' || src[after] == ':' || (src[after] == '=' && after + 1 < len && src[after + 1] == '>'));
}

static bool colon_looks_like_ternary(const char *src, size_t i) {
  int paren = 0, bracket = 0, brace = 0;
  for (size_t k = i; k > 0;) {
    char c = src[--k];
    if (c == ')') paren++;
    else if (c == '(' && paren > 0) paren--;
    else if (c == '(' && paren == 0 && bracket == 0 && brace == 0) return false;
    else if (c == ']') bracket++;
    else if (c == '[' && bracket > 0) bracket--;
    else if (c == '}') brace++;
    else if (c == '{') {
      if (brace == 0 && paren == 0 && bracket == 0) return false;
      if (brace > 0) brace--;
    } else if (c == '?' && paren == 0 && bracket == 0 && brace == 0) {
      if (k + 1 == i && k > 0 && skim_is_id_part(src[k - 1])) return false;
      return true;
    } else if ((c == ';' || c == ':') && paren == 0 && bracket == 0 && brace == 0) {
      return false;
    }
  }
  return false;
}

static bool colon_looks_like_case_label(const char *src, size_t i) {
  size_t k = i;
  while (k > 0 && src[k - 1] != '\n' && src[k - 1] != '\r' && src[k - 1] != ';')
    k--;
  size_t line = skim_skip_ws(src, i, k);
  if (!skim_word_at(src, i, line, "case") && !skim_word_at(src, i, line, "default")) return false;
  int paren = 0, bracket = 0, brace = 0;
  for (size_t p = line; p < i; p++) {
    char c = src[p];
    if (c == '\'' || c == '"' || c == '`') {
      p = skim_skip_string_raw(src, i, p) - 1;
      continue;
    }
    if (c == '(') paren++;
    else if (c == ')' && paren > 0) paren--;
    else if (c == '[') bracket++;
    else if (c == ']' && bracket > 0) bracket--;
    else if (c == '{') brace++;
    else if (c == '}' && brace > 0) brace--;
  }
  return paren == 0 && bracket == 0 && brace == 0;
}

static bool colon_looks_like_statement_label(const char *src, size_t len, size_t i) {
  size_t k = i;
  while (k > 0 && (src[k - 1] == ' ' || src[k - 1] == '\t'))
    k--;
  size_t start = k;
  while (start > 0 && skim_is_id_part(src[start - 1]))
    start--;
  if (start == k) return false;
  size_t line = start;
  while (line > 0 && (src[line - 1] == ' ' || src[line - 1] == '\t'))
    line--;
  if (
    line > 0 && src[line - 1] != '\n' && src[line - 1] != '\r' && src[line - 1] != '{' && src[line - 1] != '}' &&
    src[line - 1] != ':'
  )
    return false;
  size_t before_line = line;
  while (before_line > 0 && isspace((unsigned char)src[before_line - 1]))
    before_line--;
  if (before_line > 0 && (src[before_line - 1] == '(' || src[before_line - 1] == ',')) return false;
  size_t next = skim_skip_ws(src, len, i + 1);
  if (
    next >= len || src[next] == '{' || skim_word_at(src, len, next, "while") || skim_word_at(src, len, next, "for") ||
    skim_word_at(src, len, next, "if") || skim_word_at(src, len, next, "switch") ||
    skim_word_at(src, len, next, "do") || skim_word_at(src, len, next, "try") || skim_word_at(src, len, next, "var") ||
    skim_word_at(src, len, next, "let") || skim_word_at(src, len, next, "const") ||
    skim_word_at(src, len, next, "typeof") || skim_word_at(src, len, next, "break") ||
    skim_word_at(src, len, next, "continue")
  )
    return true;
  if (next < len && skim_is_id_start(src[next])) {
    size_t name_start = next++;
    while (next < len && skim_is_id_part(src[next]))
      next++;
    next = skim_skip_ws(src, len, next);
    if (next < len && src[next] == ':' && next > name_start) return true;
  }
  int paren = 0, bracket = 0, brace = 0;
  for (size_t p = start; p > 0;) {
    char c = src[--p];
    if (c == ')') paren++;
    else if (c == ']') bracket++;
    else if (c == '}') brace++;
    else if (c == '(') {
      if (paren == 0 && bracket == 0 && brace == 0) return false;
      paren--;
    } else if (c == '[' && bracket > 0) {
      bracket--;
    } else if (c == '{') {
      if (brace > 0) brace--;
      else if (paren == 0 && bracket == 0) break;
    } else if ((c == ';' || c == '{' || c == '}') && paren == 0 && bracket == 0 && brace == 0) {
      break;
    }
  }
  if (
    skim_word_at(src, len, next, "while") || skim_word_at(src, len, next, "for") ||
    skim_word_at(src, len, next, "if") || skim_word_at(src, len, next, "switch") ||
    skim_word_at(src, len, next, "do") || skim_word_at(src, len, next, "try")
  )
    return true;
  if (next < len && skim_is_id_start(src[next])) {
    size_t name_start = next++;
    while (next < len && skim_is_id_part(src[next]))
      next++;
    next = skim_skip_ws(src, len, next);
    if (next < len && src[next] == ':' && next > name_start) return true;
  }
  return next >= len || src[next] == '{' || skim_word_at(src, len, next, "while") ||
         skim_word_at(src, len, next, "for") || skim_word_at(src, len, next, "if") ||
         skim_word_at(src, len, next, "switch") || skim_word_at(src, len, next, "do") ||
         skim_word_at(src, len, next, "try") || skim_word_at(src, len, next, "var") ||
         skim_word_at(src, len, next, "let") || skim_word_at(src, len, next, "const") ||
         skim_word_at(src, len, next, "typeof") || skim_word_at(src, len, next, "break") ||
         skim_word_at(src, len, next, "continue");
}

static bool word_before_pos(const char *src, size_t i, const char *word) {
  while (i > 0 && isspace((unsigned char)src[i - 1]))
    i--;
  size_t end = i;
  while (i > 0 && skim_is_id_part(src[i - 1]))
    i--;
  size_t n = strlen(word);
  return end - i == n && memcmp(src + i, word, n) == 0;
}

static bool colon_looks_like_value_object_property(const char *src, size_t i) {
  if (prev_non_ws_char(src, i) == ')') return false;
  int paren = 0, bracket = 0, brace = 0;
  for (size_t k = i; k > 0;) {
    char c = src[--k];
    if (c == ')') paren++;
    else if (c == '(') {
      if (paren > 0) paren--;
      else if (bracket == 0 && brace == 0) return false;
    } else if (c == ']') bracket++;
    else if (c == '[') {
      if (bracket > 0) bracket--;
    } else if (c == '}') brace++;
    else if (c == '{') {
      if (brace > 0) {
        brace--;
      } else if (paren == 0 && bracket == 0) {
        size_t before = k;
        while (before > 0 && isspace((unsigned char)src[before - 1]))
          before--;
        char prev = before > 0 ? src[before - 1] : '\0';
        return prev == '=' || prev == '(' || prev == ',' || prev == '[' || prev == ':' ||
               word_before_pos(src, before, "return") || word_before_pos(src, before, "yield");
      }
    } else if (c == ';' && paren == 0 && bracket == 0 && brace == 0) {
      return false;
    }
  }
  return false;
}

static bool comma_is_in_var_decl(const char *src, size_t comma) {
  for (size_t k = comma; k > 0;) {
    char c = src[--k];
    if (c == ';' || c == '{' || c == '}' || c == '\n' || c == '\r') return false;
    if (skim_is_id_part(c)) {
      size_t end = k + 1;
      while (k > 0 && skim_is_id_part(src[k - 1]))
        k--;
      if (
        (end - k == 3 && memcmp(src + k, "var", 3) == 0) || (end - k == 3 && memcmp(src + k, "let", 3) == 0) ||
        (end - k == 5 && memcmp(src + k, "const", 5) == 0)
      ) {
        return true;
      }
    }
  }
  return false;
}

static bool colon_is_in_var_decl(const char *src, size_t colon) {
  size_t line = colon;
  while (line > 0 && src[line - 1] != '\n' && src[line - 1] != '\r' && src[line - 1] != ';')
    line--;
  for (size_t p = line; p < colon; p++) {
    if (
      skim_word_at(src, colon, p, "var") || skim_word_at(src, colon, p, "let") || skim_word_at(src, colon, p, "const")
    ) {
      return true;
    }
  }
  for (size_t k = colon; k > 0;) {
    char c = src[--k];
    if (c == ';' || c == '{' || c == '}' || c == '\n' || c == '\r') return false;
    if (skim_is_id_part(c)) {
      size_t end = k + 1;
      while (k > 0 && skim_is_id_part(src[k - 1]))
        k--;
      if (
        (end - k == 3 && memcmp(src + k, "var", 3) == 0) || (end - k == 3 && memcmp(src + k, "let", 3) == 0) ||
        (end - k == 5 && memcmp(src + k, "const", 5) == 0)
      ) {
        return true;
      }
    }
  }
  return false;
}

static bool prev_allows_type_colon(const char *src, size_t len, size_t i) {
  while (i > 0 && isspace((unsigned char)src[i - 1]))
    i--;
  if (i > 0 && (src[i - 1] == '?' || src[i - 1] == '!')) {
    i--;
    while (i > 0 && isspace((unsigned char)src[i - 1]))
      i--;
  }
  if (i == 0) return false;
  char c = src[i - 1];
  if (c == ']') {
    size_t bracket = i - 1;
    int depth = 1;
    while (bracket > 0 && depth > 0) {
      bracket--;
      if (src[bracket] == ']') depth++;
      else if (src[bracket] == '[') depth--;
    }
    size_t before_bracket = bracket;
    while (before_bracket > 0 && isspace((unsigned char)src[before_bracket - 1]))
      before_bracket--;
    if (
      before_bracket > 0 &&
      (src[before_bracket - 1] == '(' ||
       (src[before_bracket - 1] == ',' && comma_looks_like_param_separator(src, before_bracket - 1)))
    ) {
      return true;
    }
    if (before_bracket > 0 && (src[before_bracket - 1] == '{' || src[before_bracket - 1] == ',')) {
      size_t after_type = skip_type_tail(src, len, i + 1);
      if (after_type >= len || src[after_type] != ';') return false;
    }
  }
  if (skim_is_id_part(c)) {
    size_t ident_start = i;
    while (ident_start > 0 && skim_is_id_part(src[ident_start - 1]))
      ident_start--;
    size_t before_ident = ident_start;
    while (before_ident > 0 && isspace((unsigned char)src[before_ident - 1]))
      before_ident--;
    if (before_ident > 0 && src[before_ident - 1] == ',' && comma_looks_like_param_separator(src, before_ident - 1)) {
      return true;
    }
    if (before_ident > 0 && src[before_ident - 1] == ',' && comma_is_in_var_decl(src, before_ident - 1)) {
      return true;
    }
    if (before_ident > 0 && (src[before_ident - 1] == '{' || src[before_ident - 1] == ',')) {
      size_t after_type = skip_type_tail(src, len, i + 1);
      if (after_type >= len || src[after_type] != ';') return false;
    }
  }
  if (c == '}') {
    size_t after_type = skim_skip_ws(src, len, skip_type_tail(src, len, i + 1));
    return after_type >= len || src[after_type] == ')' || src[after_type] == ',' || src[after_type] == '=' ||
           src[after_type] == '{';
  }
  return skim_is_id_part(c) || c == ')' || c == ']';
}

static bool next_looks_like_type(const char *src, size_t len, size_t i) {
  i = skim_skip_ws(src, len, i);
  if (i >= len) return false;
  if (skim_is_id_start(src[i])) return true;
  if (isdigit((unsigned char)src[i]) || src[i] == '\'' || src[i] == '"' || src[i] == '`') return true;
  return src[i] == '{' || src[i] == '[' || src[i] == '(' || src[i] == '<' || src[i] == '-' || src[i] == '|' ||
         src[i] == '&';
}

static size_t skip_angle_type_list(const char *src, size_t len, size_t i);

static bool type_alias_at(const char *src, size_t len, size_t i) {
  if (!skim_word_at(src, len, i, "type")) return false;
  size_t name_start = 0, name_end = 0;
  size_t j = skim_parse_identifier(src, len, i + 4, &name_start, &name_end);
  if (name_start == name_end) return false;
  if (range_has_newline(src, i + 4, name_start)) return false;
  j = skim_skip_ws(src, len, j);
  if (j < len && src[j] == '<') j = skip_angle_type_list(src, len, j);
  j = skim_skip_ws(src, len, j);
  return j < len && src[j] == '=';
}

static size_t skip_type_declaration_statement(const char *src, size_t len, size_t i) {
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
    if (i + 1 < len && c == '/' && src[i + 1] == '/') {
      i += 2;
      while (i < len && src[i] != '\n')
        i++;
      continue;
    }
    if (i + 1 < len && c == '/' && src[i + 1] == '*') {
      i += 2;
      while (i + 1 < len && !(src[i] == '*' && src[i + 1] == '/'))
        i++;
      if (i + 1 < len) i += 2;
      continue;
    }
    if (isspace((unsigned char)c)) {
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
    if (!isspace((unsigned char)c)) {
      seen_type_token = true;
      last_sig = c;
    }
    i++;
  }
  return i;
}

static size_t skip_declare_function_statement(const char *src, size_t len, size_t i) {
  int paren = 0, bracket = 0, brace = 0, angle = 0;
  bool saw_params = false;
  while (i < len) {
    char c = src[i];
    if (c == '\'' || c == '"' || c == '`') {
      i = skim_skip_string_raw(src, len, i);
      continue;
    }
    if (i + 1 < len && c == '/' && src[i + 1] == '/') {
      while (i < len && src[i] != '\n')
        i++;
      continue;
    }
    if (i + 1 < len && c == '/' && src[i + 1] == '*') {
      i += 2;
      while (i + 1 < len && !(src[i] == '*' && src[i + 1] == '/'))
        i++;
      if (i + 1 < len) i += 2;
      continue;
    }
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

static size_t find_interface_body_or_semicolon(const char *src, size_t len, size_t i) {
  int paren = 0, bracket = 0, angle = 0;
  while (i < len) {
    char c = src[i];
    if (c == '\'' || c == '"' || c == '`') {
      i = skim_skip_string_raw(src, len, i);
      continue;
    }
    if (i + 1 < len && c == '/' && src[i + 1] == '/') {
      while (i < len && src[i] != '\n')
        i++;
      continue;
    }
    if (i + 1 < len && c == '/' && src[i + 1] == '*') {
      i += 2;
      while (i + 1 < len && !(src[i] == '*' && src[i + 1] == '/'))
        i++;
      if (i + 1 < len) i += 2;
      continue;
    }
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

static size_t skip_angle_type_list(const char *src, size_t len, size_t i) {
  int depth = 0;
  int paren = 0, bracket = 0, brace = 0;
  while (i < len) {
    if (src[i] == '\'' || src[i] == '"' || src[i] == '`') {
      i = skim_skip_string_raw(src, len, i);
      continue;
    }
    if (depth > 0 && paren == 0 && bracket == 0 && brace == 0 && src[i] == ';') return len + 1;
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
  return depth == 0 ? i : len + 1;
}

static bool prev_is_identifierish(const char *src, size_t i) {
  while (i > 0 && isspace((unsigned char)src[i - 1]))
    i--;
  return i > 0 && (skim_is_id_part(src[i - 1]) || src[i - 1] == ')' || src[i - 1] == ']');
}

static bool prev_word_before(const char *src, size_t i, const char *word) {
  while (i > 0 && isspace((unsigned char)src[i - 1]))
    i--;
  size_t end = i;
  while (i > 0 && skim_is_id_part(src[i - 1]))
    i--;
  size_t len = strlen(word);
  return end - i == len && memcmp(src + i, word, len) == 0;
}

static bool colon_after_function_like_params(const char *src, size_t i) {
  int depth = 0;
  size_t open = i;
  while (open > 0) {
    char c = src[--open];
    if (c == ')') depth++;
    else if (c == '(') {
      if (depth == 0) break;
      depth--;
    }
  }
  if (open == 0 && src[open] != '(') return false;
  size_t before = open;
  while (before > 0 && isspace((unsigned char)src[before - 1]))
    before--;
  if (before == 0) return false;
  if (!skim_is_id_part(src[before - 1])) return false;
  size_t name_end = before;
  while (before > 0 && skim_is_id_part(src[before - 1]))
    before--;
  size_t prev = before;
  while (prev > 0 && isspace((unsigned char)src[prev - 1]))
    prev--;
  if (prev > 0 && skim_is_id_part(src[prev - 1])) {
    size_t word_end = prev;
    while (prev > 0 && skim_is_id_part(src[prev - 1]))
      prev--;
    if (word_end - prev == 8 && memcmp(src + prev, "function", 8) == 0) return true;
  }
  if (name_end > before) {
    char p = prev > 0 ? src[prev - 1] : '\0';
    return p == '{' || p == '}' || p == ';' || p == '\n' || p == '\r';
  }
  return false;
}

static bool comma_looks_like_param_separator(const char *src, size_t comma) {
  int paren = 0, bracket = 0, brace = 0;
  for (size_t k = comma; k > 0;) {
    char c = src[--k];
    if (c == ')') paren++;
    else if (c == '(') {
      if (paren == 0 && bracket == 0 && brace == 0) return true;
      if (paren > 0) paren--;
    } else if (c == ']') bracket++;
    else if (c == '[') {
      if (bracket > 0) bracket--;
    } else if (c == '}') brace++;
    else if (c == '{') {
      if (brace == 0 && paren == 0 && bracket == 0) return false;
      if (brace > 0) brace--;
    } else if (c == ';' && paren == 0 && bracket == 0 && brace == 0) {
      return false;
    }
  }
  return false;
}

static bool angle_is_probably_type_list(const char *src, size_t len, size_t i, size_t *end_out) {
  if (src[i] != '<' || !prev_is_identifierish(src, i)) return false;
  if (i + 1 < len && src[i + 1] == '<') return false;
  if (i + 1 < len && src[i + 1] == '=') return false;
  size_t first = skim_skip_ws(src, len, i + 1);
  if (first >= len) return false;
  size_t end = skip_angle_type_list(src, len, i);
  if (end > len || end == i) return false;
  int paren = 0, bracket = 0, brace = 0, angle = 0;
  for (size_t p = i + 1; p + 1 < end; p++) {
    char c = src[p];
    if (c == '\'' || c == '"' || c == '`') {
      p = skim_skip_string_raw(src, end, p) - 1;
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
    else if (
      paren == 0 && bracket == 0 && brace == 0 && angle == 0 &&
      ((c == '|' && src[p + 1] == '|') || (c == '&' && src[p + 1] == '&'))
    ) {
      return false;
    }
  }
  size_t j = skim_skip_ws(src, len, end);
  if (j >= len || src[j] == ';' || src[j] == '\n' || src[j] == '\r') {
    *end_out = end;
    return true;
  }
  if (src[j] == '(' || src[j] == '{' || src[j] == '`' || skim_word_at(src, len, j, "extends")) {
    *end_out = end;
    return true;
  }
  return false;
}

static bool angle_is_probably_type_assertion(const char *src, size_t len, size_t i, size_t *end_out) {
  if (src[i] != '<') return false;
  if (i + 1 < len && src[i + 1] == '<') return false;
  if (
    prev_is_identifierish(src, i) && !prev_word_before(src, i, "return") && !prev_word_before(src, i, "throw") &&
    !prev_word_before(src, i, "yield")
  )
    return false;
  size_t end = skip_angle_type_list(src, len, i);
  if (end >= len || end == i) return false;
  size_t j = skim_skip_ws(src, len, end);
  if (j >= len) return false;
  if (
    skim_is_id_start(src[j]) || isdigit((unsigned char)src[j]) || src[j] == '<' || src[j] == '(' || src[j] == '[' ||
    src[j] == '{' || src[j] == '+' || src[j] == '-' || src[j] == '~' || src[j] == '!' || src[j] == '\'' ||
    src[j] == '"' || src[j] == '`'
  ) {
    *end_out = end;
    return true;
  }
  return false;
}

static bool try_remove_uninitialized_class_field(const char *src, size_t len, size_t i, size_t *end_out) {
  if (!skim_options.remove_class_fields_without_initializer && !skim_options.allow_declare_fields_false) return false;
  if (i > 0 && src[i - 1] == '#') return false;
  size_t prev = i;
  while (prev > 0 && (src[prev - 1] == ' ' || src[prev - 1] == '\t'))
    prev--;
  if (prev > 0 && src[prev - 1] != '\n' && src[prev - 1] != '\r' && src[prev - 1] != '{' && src[prev - 1] != ';')
    return false;
  size_t j = i;
  if (src[j] == '@') {
    while (j < len && src[j] != '\n' && src[j] != '\r')
      j++;
    j = skim_skip_ws(src, len, j);
  }
  if (skim_word_at(src, len, j, "static")) j = skim_skip_ws(src, len, j + 6);
  if (skim_word_at(src, len, j, "accessor")) {
    size_t end = skim_skip_statement_like(src, len, j);
    if (end > j) {
      *end_out = end;
      return true;
    }
  }
  if (src[j] == '#') return false;
  if (j < len && src[j] == '[') {
    size_t close = skim_skip_balanced(src, len, j, '[', ']');
    size_t after = skim_skip_ws(src, len, close);
    if (after < len && src[after] == '(') return false;
    j = after;
  } else {
    size_t name_start = 0, name_end = 0;
    j = skim_parse_identifier(src, len, j, &name_start, &name_end);
    if (name_start == name_end) return false;
    j = skim_skip_ws(src, len, j);
    if (j < len && src[j] == '(') return false;
  }
  if (j < len && (src[j] == '?' || src[j] == '!')) j = skim_skip_ws(src, len, j + 1);
  if (j < len && src[j] == ':') j = skim_skip_ws(src, len, skip_type_tail(src, len, j + 1));
  if (j < len && src[j] == '=') return false;
  if (j < len && src[j] == ';') {
    *end_out = j + 1;
    return true;
  }
  return false;
}

static bool try_remove_function_overload(const char *src, size_t len, size_t i, size_t *end_out) {
  if (!skim_word_at(src, len, i, "function")) return false;
  size_t after_function = skim_skip_ws_comments(src, len, i + 8);
  if (after_function >= len || (!skim_is_id_start(src[after_function]) && src[after_function] != '*')) return false;
  char prev = prev_non_ws_same_line_char(src, i);
  if (prev != '\0' && prev != ';' && prev != '{' && prev != '}' && prev != '\n' && prev != '\r') return false;
  int paren = 0, bracket = 0, brace = 0, angle = 0;
  for (size_t j = i + 8; j < len; j++) {
    char c = src[j];
    if (c == '\'' || c == '"' || c == '`') {
      j = skim_skip_string_raw(src, len, j) - 1;
      continue;
    }
    if (c == '(') paren++;
    else if (c == ')' && paren > 0) paren--;
    else if (c == '[') bracket++;
    else if (c == ']' && bracket > 0) bracket--;
    else if (c == '<') angle++;
    else if (c == '>' && angle > 0) angle--;
    else if (c == ':' && paren == 0 && bracket == 0 && brace == 0 && angle == 0) {
    } else if ((c == '\n' || c == '\r') && paren == 0 && bracket == 0 && brace == 0 && angle == 0) {
      size_t next = skim_skip_ws_comments(src, len, j + 1);
      if (skim_word_at(src, len, next, "function")) {
        *end_out = next;
        return true;
      }
    } else if (c == '{' && paren == 0 && bracket == 0 && brace == 0 && angle == 0) {
      if (prev_non_ws_same_line_char(src, j) == ':') {
        size_t close = skim_skip_balanced(src, len, j, '{', '}');
        size_t next = skim_skip_ws_comments(src, len, close);
        if (next < len && src[next] == ';') {
          *end_out = next + 1;
          return true;
        }
      }
      return false;
    } else if (c == ';' && paren == 0 && bracket == 0 && brace == 0 && angle == 0) {
      *end_out = j + 1;
      return true;
    } else if (c == '{') brace++;
    else if (c == '}' && brace > 0) brace--;
  }
  return false;
}

bool skim_ts_annotations_try(skim_str_t *out, const char *src, size_t len, size_t *io) {
  size_t i = *io;
  size_t erase_start = i;
  bool export_declare = false;
  if (skim_word_at(src, len, i, "export")) {
    size_t j = skim_skip_ws_comments(src, len, i + 6);
    if (skim_word_at(src, len, j, "declare")) {
      export_declare = true;
      i = j;
    }
  }

  if (
    (skim_options.remove_class_fields_without_initializer || skim_options.allow_declare_fields_false) && src[i] == '@'
  ) {
    size_t end = i;
    while (end < len && src[end] != '\n' && src[end] != '\r')
      end++;
    skim_emit_preserved_newlines(out, src, i, end);
    *io = end;
    return true;
  }

  size_t field_end = 0;
  if (try_remove_uninitialized_class_field(src, len, i, &field_end)) {
    skim_emit_preserved_newlines(out, src, i, field_end);
    *io = field_end;
    return true;
  }

  size_t overload_end = 0;
  if (try_remove_function_overload(src, len, i, &overload_end)) {
    skim_emit_preserved_newlines(out, src, i, overload_end);
    *io = overload_end;
    return true;
  }

  if (skim_word_at(src, len, i, "implements")) {
    size_t end = i + 10;
    end = skim_skip_ws_comments(src, len, end);
    if (end < len && (src[end] == ';' || src[end] == '=')) return false;
    while (end < len && src[end] != '{')
      end++;
    *io = end;
    return true;
  }

  if (skim_word_at(src, len, i, "abstract")) {
    size_t j = skim_skip_ws(src, len, i + 8);
    if (skim_word_at(src, len, j, "class")) {
      if (range_has_newline(src, i + 8, j)) return false;
      *io = i + 8;
      return true;
    }
    size_t name_start = 0, name_end = 0;
    j = skim_parse_identifier(src, len, j, &name_start, &name_end);
    j = skim_skip_ws(src, len, j);
    if (name_start != name_end && j < len && src[j] == '(') {
      size_t params_end = skim_skip_balanced(src, len, j, '(', ')');
      size_t after = skim_skip_ws(src, len, params_end);
      if (after < len && src[after] == ':') after = skim_skip_ws(src, len, skip_type_tail(src, len, after + 1));
      if (after < len && src[after] == ';') {
        skim_emit_preserved_newlines(out, src, i, after + 1);
        *io = after + 1;
        return true;
      }
    }
  }

  static const char *modifiers[] = {"public", "private", "protected", "readonly", "override"};
  for (size_t m = 0; m < sizeof(modifiers) / sizeof(modifiers[0]); m++) {
    if (skim_word_at(src, len, i, modifiers[m])) {
      char prev = prev_non_ws_same_line_char(src, i);
      if (
        prev != '(' && prev != ',' && prev != '{' && prev != ';' && prev != '\n' && prev != '\r' &&
        !prev_word_before(src, i, "public") && !prev_word_before(src, i, "private") &&
        !prev_word_before(src, i, "protected") && !prev_word_before(src, i, "readonly") &&
        !prev_word_before(src, i, "override")
      )
        return false;
      *io = i + strlen(modifiers[m]);
      return true;
    }
  }

  if (skim_word_at(src, len, i, "interface")) {
    char prev = prev_non_ws_same_line_char(src, i);
    if (prev != '\0' && prev != ';' && prev != '{' && prev != '}' && prev != '\n' && prev != '\r') return false;
    size_t after_interface = skim_skip_ws_comments(src, len, i + 9);
    if (range_has_newline(src, i + 9, after_interface)) return false;
    if (after_interface < len && (src[after_interface] == ';' || src[after_interface] == '=')) return false;
    size_t body = find_interface_body_or_semicolon(src, len, i + 9);
    size_t end = body;
    if (body < len && src[body] == '{') end = skim_skip_balanced(src, len, body, '{', '}');
    else if (body < len && src[body] == ';') end = body + 1;
    if (end < len && src[end] == ';') end++;
    skim_emit_preserved_newlines(out, src, i, end);
    *io = end;
    return true;
  }

  if (skim_word_at(src, len, i, "this")) {
    size_t j = skim_skip_ws(src, len, i + 4);
    if (j < len && src[j] == ':') {
      size_t end = skim_skip_ws(src, len, skip_type_tail(src, len, j + 1));
      if (end < len && src[end] == ',') end++;
      *io = end;
      return true;
    }
    if (j < len && src[j] == ',' && this_looks_like_bare_parameter(src, len, i)) {
      *io = j + 1;
      return true;
    }
  }

  bool is_declare_keyword = false;
  if (skim_word_at(src, len, i, "declare") && !prev_word_before(src, i, "function")) {
    size_t after_declare = skim_skip_ws_comments(src, len, i + 7);
    is_declare_keyword =
      !range_has_newline(src, i + 7, after_declare) &&
      (skim_word_at(src, len, after_declare, "class") || skim_word_at(src, len, after_declare, "enum") ||
       skim_word_at(src, len, after_declare, "namespace") || skim_word_at(src, len, after_declare, "module") ||
       skim_word_at(src, len, after_declare, "interface") || skim_word_at(src, len, after_declare, "global") ||
       skim_word_at(src, len, after_declare, "function") || skim_word_at(src, len, after_declare, "async") ||
       skim_word_at(src, len, after_declare, "var") || skim_word_at(src, len, after_declare, "let") ||
       skim_word_at(src, len, after_declare, "const") || skim_word_at(src, len, after_declare, "type"));
  }
  bool is_type_alias = type_alias_at(src, len, i);
  if (export_declare || is_type_alias || is_declare_keyword) {
    size_t end = is_type_alias ? skip_type_declaration_statement(src, len, i) : skim_skip_statement_like(src, len, i);
    if (export_declare || is_declare_keyword) {
      size_t after_declare = skim_skip_ws_comments(src, len, i + 7);
      if (skim_word_at(src, len, after_declare, "function")) end = skip_declare_function_statement(src, len, i);
      else if (
        skim_word_at(src, len, after_declare, "var") || skim_word_at(src, len, after_declare, "let") ||
        skim_word_at(src, len, after_declare, "const")
      )
        end = skip_type_declaration_statement(src, len, i);
      bool body_decl =
        skim_word_at(src, len, after_declare, "class") || skim_word_at(src, len, after_declare, "enum") ||
        skim_word_at(src, len, after_declare, "namespace") || skim_word_at(src, len, after_declare, "module") ||
        skim_word_at(src, len, after_declare, "global") || skim_word_at(src, len, after_declare, "interface");
      size_t body = after_declare;
      while (body < len && src[body] != '{' && src[body] != ';') {
        if (src[body] == '\'' || src[body] == '"' || src[body] == '`') body = skim_skip_string_raw(src, len, body);
        else body++;
      }
      if (body_decl && body < len && src[body] == '{') {
        end = skim_skip_balanced(src, len, body, '{', '}');
        if (end < len && src[end] == ';') end++;
      }
    }
    skim_emit_preserved_newlines(out, src, erase_start, end);
    *io = end;
    return true;
  }

  if (
    src[i] == ':' && !colon_looks_like_ternary(src, i) && !colon_looks_like_case_label(src, i) &&
    !colon_looks_like_statement_label(src, len, i) && !colon_looks_like_value_object_property(src, i) &&
    prev_allows_type_colon(src, len, i) && next_looks_like_type(src, len, i + 1)
  ) {
    size_t after_colon = skim_skip_ws(src, len, i + 1);
    if (skim_word_at(src, len, after_colon, "function")) return false;
    size_t end =
      prev_non_ws_char(src, i) == ')' ? skip_return_type_tail(src, len, i + 1) : skip_type_tail(src, len, i + 1);
    if (
      after_colon < len && src[after_colon] == '{' && prev_non_ws_char(src, i) != ')' && colon_is_in_var_decl(src, i)
    ) {
      end = skim_skip_balanced(src, len, after_colon, '{', '}');
      for (;;) {
        size_t next = skim_skip_ws(src, len, end);
        if (next + 1 < len && src[next] == '[' && src[next + 1] == ']') {
          end = next + 2;
          continue;
        }
        if (next < len && (src[next] == '|' || src[next] == '&')) {
          end = skip_type_tail(src, len, next);
          continue;
        }
        break;
      }
    }
    if (prev_non_ws_char(src, i) == ')' && !colon_after_function_like_params(src, i)) {
      size_t type_start = skim_skip_ws(src, len, i + 1);
      if (type_start < len && src[type_start] == '(') {
        size_t after_params = skim_skip_ws(src, len, skim_skip_balanced(src, len, type_start, '(', ')'));
        if (after_params + 1 < len && src[after_params] == '=' && src[after_params + 1] == '>') {
          end = skip_return_type_tail(src, len, after_params + 2);
        }
      }
    }
    if (end + 1 < len && src[end] == '=' && src[end + 1] == '>') {
      size_t full = skip_type_tail(src, len, i + 1);
      size_t after_full = skim_skip_ws(src, len, full);
      if (after_full < len && src[after_full] == '{' && colon_after_function_like_params(src, i)) end = full;
    }
    if (skim_word_at(src, len, end, "of") || skim_word_at(src, len, end, "in")) skim_str_putc(out, ' ');
    *io = end;
    return true;
  }

  if (
    src[i] == '?' && i + 1 < len &&
    (src[i + 1] == ':' || src[i + 1] == ',' || src[i + 1] == ')' || src[i + 1] == ';' || src[i + 1] == '(')
  ) {
    *io = src[i + 1] == ':' ? skip_type_tail(src, len, i + 2) : i + 1;
    return true;
  }

  if (src[i] == '!' && i + 1 < len && src[i + 1] != '=') {
    char prev = prev_non_ws_same_line_char(src, i);
    if (!(skim_is_id_part(prev) || prev == ')' || prev == ']')) return false;
    if (
      prev_word_before(src, i, "return") || prev_word_before(src, i, "throw") || prev_word_before(src, i, "yield") ||
      prev_word_before(src, i, "case") || prev_word_before(src, i, "delete") || prev_word_before(src, i, "void") ||
      prev_word_before(src, i, "typeof")
    )
      return false;
    size_t end = i + 1;
    while (end < len && src[end] == '!' && (end + 1 >= len || src[end + 1] != '='))
      end++;
    *io = end;
    return true;
  }

  if (src[i] == '<') {
    size_t end = i;
    if (angle_is_probably_type_list(src, len, i, &end) || angle_is_probably_type_assertion(src, len, i, &end)) {
      *io = end;
      return true;
    }
  }

  if (
    skim_word_at(src, len, i, "as") && prev_non_ws_same_line_char(src, i) != '\n' &&
    prev_non_ws_same_line_char(src, i) != '\r' && prev_non_ws_char(src, i) != '=' && prev_non_ws_char(src, i) != '(' &&
    prev_non_ws_char(src, i) != '[' && prev_non_ws_char(src, i) != '{' && prev_non_ws_char(src, i) != ',' &&
    prev_non_ws_char(src, i) != ';' && prev_non_ws_char(src, i) != ':' && prev_non_ws_char(src, i) != '*' &&
    !prev_word_before(src, i, "var") && !prev_word_before(src, i, "let") && !prev_word_before(src, i, "const") &&
    !prev_word_before(src, i, "function")
  ) {
    *io = skip_type_tail(src, len, i + 2);
    return true;
  }

  if (skim_word_at(src, len, i, "satisfies")) {
    *io = skip_type_tail(src, len, i + 9);
    return true;
  }

  return false;
}
