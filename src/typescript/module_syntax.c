#include "chars.h"
#include "module_internal.h"
#include "skip.h"
#include "str.h"

size_t skim_module_import_statement_end(const char *src, size_t len, size_t i) {
  int paren = 0, bracket = 0, brace = 0;
  bool next_string_is_module_specifier = false;
  bool saw_module_specifier = false;
  while (i < len) {
    char c = src[i];
    if (c == '\'' || c == '"' || c == '`') {
      i = skim_skip_string_raw(src, len, i);
      if (next_string_is_module_specifier) {
        saw_module_specifier = true;
        next_string_is_module_specifier = false;
      }
      continue;
    }
    if (i + 1 < len && c == '/' && src[i + 1] == '/') {
      if (paren == 0 && bracket == 0 && brace == 0 && saw_module_specifier) return i;
      i += 2;
      while (i < len && src[i] != '\n' && src[i] != '\r')
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
    if (paren == 0 && bracket == 0 && brace == 0) {
      if (skim_word_at(src, len, i, "import")) {
        size_t next = skim_skip_ws_comments(src, len, i + 6);
        if (next < len && (src[next] == '\'' || src[next] == '"' || src[next] == '`')) {
          next_string_is_module_specifier = true;
        }
        i += 6;
        continue;
      }
      if (skim_word_at(src, len, i, "from")) {
        next_string_is_module_specifier = true;
        i += 4;
        continue;
      }
    }
    if (c == '(') paren++;
    else if (c == ')' && paren > 0) paren--;
    else if (c == '[') bracket++;
    else if (c == ']' && bracket > 0) bracket--;
    else if (c == '{') brace++;
    else if (c == '}' && brace > 0) brace--;
    else if (c == ';' && paren == 0 && bracket == 0 && brace == 0) return i + 1;
    else if (
      (c == '\n' || c == '\r') && paren == 0 && bracket == 0 && brace == 0 && saw_module_specifier
    )
      return i;
    i++;
  }
  return i;
}

bool skim_module_statement_keyword_can_start_here(const char *src, size_t pos) {
  size_t line = pos;
  while (line > 0 && src[line - 1] != '\n' && src[line - 1] != '\r')
    line--;
  size_t first = line;
  while (first < pos && (src[first] == ' ' || src[first] == '\t'))
    first++;
  if (first == pos) return true;

  char prev = prev_non_ws(src, pos);
  return prev == '\0' || prev == ';' || prev == '{' || prev == '}';
}

bool skim_module_type_statement_keyword_at(const char *src, size_t len, size_t pos) {
  if (!skim_module_statement_keyword_can_start_here(src, pos) && !word_before_is(src, pos, "export")) return false;
  if (skim_word_at(src, len, pos, "type")) {
    size_t name_start = 0, name_end = 0;
    size_t after_name = skim_parse_identifier(src, len, skim_skip_ws_comments(src, len, pos + 4), &name_start, &name_end);
    size_t eq = skim_skip_ws_comments(src, len, after_name);
    return name_start != name_end && eq < len && src[eq] == '=';
  }
  if (skim_word_at(src, len, pos, "interface")) {
    size_t name_start = 0, name_end = 0;
    skim_parse_identifier(src, len, skim_skip_ws_comments(src, len, pos + 9), &name_start, &name_end);
    return name_start != name_end;
  }
  return false;
}

bool
skim_module_local_name_span(const char *src, size_t start, size_t end, size_t *name_start, size_t *name_end) {
  start = trim_left(src, start, end);
  end = trim_right(src, start, end);
  if (skim_word_at(src, end, start, "type")) start = trim_left(src, start + 4, end);
  *name_start = *name_end = 0;
  for (size_t as_pos = start; as_pos < end; as_pos++) {
    if (skim_word_at(src, end, as_pos, "as")) {
      skim_parse_identifier(src, end, as_pos + 2, name_start, name_end);
      if (*name_start != *name_end) return true;
    }
  }
  skim_parse_identifier(src, end, start, name_start, name_end);
  return *name_start != *name_end;
}

char *skim_module_local_name_dup(const char *src, size_t start, size_t end) {
  size_t name_start = 0, name_end = 0;
  if (!skim_module_local_name_span(src, start, end, &name_start, &name_end)) return NULL;
  return skim_slice_dup(src, name_start, name_end);
}

bool skim_module_specifier_is_explicit_type(const char *src, size_t start, size_t end) {
  start = trim_left(src, start, end);
  return skim_word_at(src, end, start, "type");
}

size_t skim_module_export_type_statement_end(const char *src, size_t len, size_t i) {
  size_t type = i;
  if (skim_word_at(src, len, type, "export")) type = skim_skip_ws_comments(src, len, type + 6);
  if (skim_word_at(src, len, type, "type")) {
    size_t name = skim_skip_ws_comments(src, len, type + 4);
    if (name < len && skim_is_id_start(src[name])) return skim_skip_type_alias(src, len, type);
  }
  int paren = 0, bracket = 0, brace = 0, angle = 0;
  bool seen_token = false;
  char last_sig = '\0';
  while (i < len) {
    char c = src[i];
    if ((c == '\n' || c == '\r') && paren == 0 && bracket == 0 && brace == 0 && angle == 0 && seen_token) {
      size_t next = skim_skip_ws_comments(src, len, i + 1);
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
      seen_token = true;
      i = skim_skip_string_raw(src, len, i);
      continue;
    }
    if (i + 1 < len && c == '/' && src[i + 1] == '/') {
      while (i < len && src[i] != '\n' && src[i] != '\r')
        i++;
      continue;
    }
    if (i + 1 < len && c == '/' && src[i + 1] == '*') {
      i = skim_skip_block_comment(src, len, i + 2);
      continue;
    }
    if (skim_is_space(c)) {
      i++;
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
    else if (c == ';' && paren == 0 && bracket == 0 && brace == 0 && angle == 0) return i + 1;
    seen_token = true;
    last_sig = c;
    i++;
  }
  return i;
}
