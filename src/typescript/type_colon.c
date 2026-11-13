#include "chars.h"
#include "run.h"
#include "scan_cache.h"
#include "skip.h"
#include "type_syntax.h"
#include "typescript.h"

#include <string.h>

bool skim_colon_looks_like_ternary(const char *src, size_t colon) {
  int paren = 0, bracket = 0, brace = 0;
  for (size_t k = colon; k > 0;) {
    char c = src[--k];
    if (!skim_char_has(c, SKIM_CC_BACK_STOP)) continue;
    if ((c == ')' || c == ']' || c == '}') && skim_back_skip_block(src, &k)) continue;
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
      if (k + 1 == colon && k > 0 && skim_is_id_part(src[k - 1])) return false;
      return true;
    } else if ((c == ';' || c == ':') && paren == 0 && bracket == 0 && brace == 0) {
      return false;
    }
  }
  return false;
}

bool skim_colon_looks_like_case_label(const char *src, size_t colon) {
  size_t k = colon;
  while (k > 0 && src[k - 1] != '\n' && src[k - 1] != '\r' && src[k - 1] != ';')
    k--;
  size_t line = skim_skip_ws(src, colon, k);
  if (!skim_word_any(src, colon, line, "case\0default\0")) return false;
  int paren = 0, bracket = 0, brace = 0;
  for (size_t p = line; p < colon; p++) {
    char c = src[p];
    if (c == '\'' || c == '"' || c == '`') {
      p = skim_skip_string_raw(src, colon, p) - 1;
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
  while (before_line > 0 && skim_is_space(src[before_line - 1]))
    before_line--;
  if (before_line > 0 && (src[before_line - 1] == '(' || src[before_line - 1] == ',')) return false;
  size_t next = skim_skip_ws(src, len, i + 1);
  if (
    next >= len || src[next] == '{' ||
    skim_word_any(src, len, next, "while\0for\0if\0switch\0do\0try\0var\0let\0const\0typeof\0break\0continue\0")
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
    if (!skim_char_has(c, SKIM_CC_BACK_STOP)) continue;
    if (((c == ')' && paren >= 0) || c == ']' || c == '}') && skim_back_skip_block(src, &p)) continue;
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
  if (skim_word_any(src, len, next, "while\0for\0if\0switch\0do\0try\0")) return true;
  if (next < len && skim_is_id_start(src[next])) {
    size_t name_start = next++;
    while (next < len && skim_is_id_part(src[next]))
      next++;
    next = skim_skip_ws(src, len, next);
    if (next < len && src[next] == ':' && next > name_start) return true;
  }
  return next >= len || src[next] == '{' ||
         skim_word_any(src, len, next, "while\0for\0if\0switch\0do\0try\0var\0let\0const\0typeof\0break\0continue\0");
}

void skim_ts_annotations_memo_invalidate(void) {
  skim_object_scan_memo_t *memo = &skim_run->ts.object_memo;
  memo->src = NULL;
}

static bool value_object_scan(const char *src, size_t i, bool memo_active) {
  skim_object_scan_memo_t *memo = &skim_run->ts.object_memo;
  int paren = 0, bracket = 0, brace = 0;
  for (size_t k = i; k > 0;) {
    char c = src[--k];
    if (!skim_char_has(c, SKIM_CC_BACK_STOP)) continue;
    if ((c == ')' || c == ']' || c == '}') && skim_back_skip_block(src, &k)) continue;
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
        while (before > 0 && skim_is_space(src[before - 1]))
          before--;
        char prev = before > 0 ? src[before - 1] : '\0';
        return prev == '=' || prev == '(' || prev == ',' || prev == '[' || prev == ':' ||
               skim_word_before(src, before, "return") || skim_word_before(src, before, "yield");
      }
    } else if (c == ';' && paren == 0 && bracket == 0 && brace == 0) {
      return false;
    } else if (c == ':' && memo_active && paren == 0 && bracket == 0 && brace == 0) {
      size_t slot = k % SKIM_OBJECT_SCAN_MEMO_SIZE;
      if (memo->pos_plus_one[slot] == k + 1) return memo->result[slot];
    }
  }
  return false;
}

static bool colon_looks_like_value_object_property(const char *src, size_t i) {
  skim_object_scan_memo_t *memo = &skim_run->ts.object_memo;
  if (prev_non_ws_char(src, i) == ')') return false;
  if (!skim_scan_cache_owns(src)) return value_object_scan(src, i, false);
  if (memo->src != src) {
    memset(memo->pos_plus_one, 0, sizeof(memo->pos_plus_one));
    memo->src = src;
  }
  bool result = value_object_scan(src, i, true);
  size_t slot = i % SKIM_OBJECT_SCAN_MEMO_SIZE;
  memo->pos_plus_one[slot] = i + 1;
  memo->result[slot] = result;
  return result;
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

bool skim_ts_colon_is_in_var_decl(const char *src, size_t colon) {
  size_t line = colon;
  while (line > 0 && src[line - 1] != '\n' && src[line - 1] != '\r' && src[line - 1] != ';')
    line--;
  for (size_t p = line; p < colon; p++) {
    if (skim_word_any(src, colon, p, "var\0let\0const\0")) { return true; }
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

static bool comma_looks_like_param_separator(const char *src, size_t comma) {
  int paren = 0, bracket = 0, brace = 0;
  for (size_t k = comma; k > 0;) {
    char c = src[--k];
    if (!skim_char_has(c, SKIM_CC_BACK_STOP)) continue;
    if ((c == ')' || c == ']' || c == '}') && skim_back_skip_block(src, &k)) continue;
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

static bool prev_allows_type_colon(const char *src, size_t len, size_t i) {
  while (i > 0 && skim_is_space(src[i - 1]))
    i--;
  if (i > 0 && (src[i - 1] == '?' || src[i - 1] == '!')) {
    i--;
    while (i > 0 && skim_is_space(src[i - 1]))
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
    while (before_bracket > 0 && skim_is_space(src[before_bracket - 1]))
      before_bracket--;
    if (
      before_bracket > 0 &&
      (src[before_bracket - 1] == '(' ||
       (src[before_bracket - 1] == ',' && comma_looks_like_param_separator(src, before_bracket - 1)))
    ) {
      return true;
    }
    if (before_bracket > 0 && (src[before_bracket - 1] == '{' || src[before_bracket - 1] == ',')) {
      size_t after_type = skim_ts_skip_type_tail(src, len, i + 1);
      if (after_type >= len || src[after_type] != ';') return false;
    }
  }
  if (skim_is_id_part(c)) {
    size_t ident_start = i;
    while (ident_start > 0 && skim_is_id_part(src[ident_start - 1]))
      ident_start--;
    size_t before_ident = ident_start;
    while (before_ident > 0 && skim_is_space(src[before_ident - 1]))
      before_ident--;
    if (before_ident > 0 && src[before_ident - 1] == ',' && comma_looks_like_param_separator(src, before_ident - 1)) {
      return true;
    }
    if (before_ident > 0 && src[before_ident - 1] == ',' && comma_is_in_var_decl(src, before_ident - 1)) {
      return true;
    }
    if (before_ident > 0 && (src[before_ident - 1] == '{' || src[before_ident - 1] == ',')) {
      size_t after_type = skim_ts_skip_type_tail(src, len, i + 1);
      if (after_type >= len || src[after_type] != ';') return false;
    }
  }
  if (c == '}') {
    size_t after_type = skim_skip_ws(src, len, skim_ts_skip_type_tail(src, len, i + 1));
    return after_type >= len || src[after_type] == ')' || src[after_type] == ',' || src[after_type] == '=' ||
           src[after_type] == '{';
  }
  return skim_is_id_part(c) || c == ')' || c == ']';
}

static bool next_looks_like_type(const char *src, size_t len, size_t i) {
  i = skim_skip_ws(src, len, i);
  if (i >= len) return false;
  if (skim_is_id_start(src[i])) return true;
  if (skim_is_digit(src[i]) || src[i] == '\'' || src[i] == '"' || src[i] == '`') return true;
  return src[i] == '{' || src[i] == '[' || src[i] == '(' || src[i] == '<' || src[i] == '-' || src[i] == '|' ||
         src[i] == '&';
}

bool skim_ts_colon_after_function_like_params(const char *src, size_t i) {
  int depth = 0;
  size_t open = i;
  while (open > 0) {
    char c = src[--open];
    if (!skim_char_has(c, SKIM_CC_BRACKET)) continue;
    if ((c == ')' || c == ']' || c == '}') && skim_back_skip_block(src, &open)) continue;
    if (c == ')') depth++;
    else if (c == '(') {
      if (depth == 0) break;
      depth--;
    }
  }
  if (open == 0 && src[open] != '(') return false;
  size_t before = open;
  while (before > 0 && skim_is_space(src[before - 1]))
    before--;
  if (before == 0) return false;
  if (!skim_is_id_part(src[before - 1])) return false;
  size_t name_end = before;
  while (before > 0 && skim_is_id_part(src[before - 1]))
    before--;
  size_t prev = before;
  while (prev > 0 && skim_is_space(src[prev - 1]))
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

bool skim_ts_colon_starts_type_annotation(const char *src, size_t len, size_t i) {
  return !skim_colon_looks_like_ternary(src, i) && !skim_colon_looks_like_case_label(src, i) &&
         !colon_looks_like_statement_label(src, len, i) && !colon_looks_like_value_object_property(src, i) &&
         prev_allows_type_colon(src, len, i) && next_looks_like_type(src, len, i + 1);
}
