#include "chars.h"
#include "emit.h"
#include "run.h"
#include "skip.h"
#include "str.h"
#include "transform.h"
#include "type_syntax.h"
#include "typescript.h"

#include <string.h>

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

static bool type_alias_at(const char *src, size_t len, size_t i) {
  if (!skim_word_at(src, len, i, "type")) return false;
  size_t name_start = 0, name_end = 0;
  size_t j = skim_parse_identifier(src, len, i + 4, &name_start, &name_end);
  if (name_start == name_end) return false;
  if (skim_range_has_newline(src, i + 4, name_start)) return false;
  j = skim_skip_ws(src, len, j);
  if (j < len && src[j] == '<') j = skim_ts_skip_angle_type_list(src, len, j, true);
  j = skim_skip_ws(src, len, j);
  return j < len && src[j] == '=';
}

static bool prev_is_identifierish(const char *src, size_t i) {
  while (i > 0 && skim_is_space(src[i - 1]))
    i--;
  return i > 0 && (skim_is_id_part(src[i - 1]) || src[i - 1] == ')' || src[i - 1] == ']');
}

static bool angle_is_probably_type_list(const char *src, size_t len, size_t i, size_t *end_out) {
  if (src[i] != '<' || !prev_is_identifierish(src, i)) return false;
  if (i + 1 < len && src[i + 1] == '<') return false;
  if (i + 1 < len && src[i + 1] == '=') return false;
  size_t first = skim_skip_ws(src, len, i + 1);
  if (first >= len) return false;
  size_t end = skim_ts_skip_angle_type_list(src, len, i, true);
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
    prev_is_identifierish(src, i) && !skim_word_before(src, i, "return") && !skim_word_before(src, i, "throw") &&
    !skim_word_before(src, i, "yield")
  )
    return false;
  size_t end = skim_ts_skip_angle_type_list(src, len, i, true);
  if (end >= len || end == i) return false;
  size_t j = skim_skip_ws(src, len, end);
  if (j >= len) return false;
  if (
    skim_is_id_start(src[j]) || skim_is_digit(src[j]) || src[j] == '<' || src[j] == '(' || src[j] == '[' ||
    src[j] == '{' || src[j] == '+' || src[j] == '-' || src[j] == '~' || src[j] == '!' || src[j] == '\'' ||
    src[j] == '"' || src[j] == '`'
  ) {
    *end_out = end;
    return true;
  }
  return false;
}

static bool try_remove_uninitialized_class_field(const char *src, size_t len, size_t i, size_t *end_out) {
  const skim_options_t *options = &skim_run->options;
  if (!options->remove_class_fields_without_initializer && !options->allow_declare_fields_false) return false;
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
  if (j >= len || src[j] == '#') return false;
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
  if (j < len && src[j] == ':') j = skim_skip_ws(src, len, skim_ts_skip_type_tail(src, len, j + 1));
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
    else if ((c == '\n' || c == '\r') && paren == 0 && bracket == 0 && brace == 0 && angle == 0) {
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
    (skim_run->options.remove_class_fields_without_initializer || skim_run->options.allow_declare_fields_false) &&
    src[i] == '@'
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
      if (skim_range_has_newline(src, i + 8, j)) return false;
      *io = i + 8;
      return true;
    }
    size_t name_start = 0, name_end = 0;
    j = skim_parse_identifier(src, len, j, &name_start, &name_end);
    j = skim_skip_ws(src, len, j);
    if (name_start != name_end && j < len && src[j] == '(') {
      size_t params_end = skim_skip_balanced(src, len, j, '(', ')');
      size_t after = skim_skip_ws(src, len, params_end);
      if (after < len && src[after] == ':') after = skim_skip_ws(src, len, skim_ts_skip_type_tail(src, len, after + 1));
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
      size_t after_mod = skim_skip_ws(src, len, i + strlen(modifiers[m]));
      if (
        after_mod < len &&
        (src[after_mod] == ':' || src[after_mod] == ',' || src[after_mod] == '}' ||
         src[after_mod] == '(' || src[after_mod] == '=')
      )
        return false;
      char prev = prev_non_ws_same_line_char(src, i);
      if (
        prev != '(' && prev != ',' && prev != '{' && prev != ';' && prev != '\n' && prev != '\r' &&
        !skim_word_before(src, i, "public") && !skim_word_before(src, i, "private") &&
        !skim_word_before(src, i, "protected") && !skim_word_before(src, i, "readonly") &&
        !skim_word_before(src, i, "override")
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
    if (skim_range_has_newline(src, i + 9, after_interface)) return false;
    if (after_interface < len && (src[after_interface] == ';' || src[after_interface] == '=')) return false;
    size_t body = skim_ts_find_interface_body_or_semicolon(src, len, i + 9);
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
      size_t end = skim_skip_ws(src, len, skim_ts_skip_type_tail(src, len, j + 1));
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
  if (skim_word_at(src, len, i, "declare") && !skim_word_before(src, i, "function")) {
    size_t after_declare = skim_skip_ws_comments(src, len, i + 7);
    static const char declare_words[] =
      "class\0enum\0namespace\0module\0interface\0global\0function\0async\0var\0let\0const\0type\0";
    is_declare_keyword =
      !skim_range_has_newline(src, i + 7, after_declare) && skim_word_any(src, len, after_declare, declare_words);
  }
  bool is_type_alias = type_alias_at(src, len, i);
  if (export_declare || is_type_alias || is_declare_keyword) {
    size_t end = is_type_alias ? skim_skip_type_alias(src, len, i) : skim_skip_statement_like(src, len, i);
    if (export_declare || is_declare_keyword) {
      size_t after_declare = skim_skip_ws_comments(src, len, i + 7);
      if (skim_word_at(src, len, after_declare, "type")) end = skim_skip_type_alias(src, len, after_declare);
      else if (skim_word_at(src, len, after_declare, "function")) end = skim_ts_skip_declare_function_statement(src, len, i);
      else if (skim_word_any(src, len, after_declare, "var\0let\0const\0"))
        end = skim_ts_skip_type_declaration_statement(src, len, i);
      bool body_decl = skim_word_any(src, len, after_declare, "class\0enum\0namespace\0module\0global\0interface\0");
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
    src[i] == ':' && skim_ts_colon_starts_type_annotation(src, len, i)
  ) {
    size_t after_colon = skim_skip_ws(src, len, i + 1);
    if (skim_word_at(src, len, after_colon, "function")) return false;
    size_t end =
      skim_prev_non_ws(src, i) == ')' ? skim_ts_skip_return_type_tail(src, len, i + 1) : skim_ts_skip_type_tail(src, len, i + 1);
    if (
      after_colon < len && src[after_colon] == '{' && skim_prev_non_ws(src, i) != ')' && skim_ts_colon_is_in_var_decl(src, i)
    ) {
      end = skim_skip_balanced(src, len, after_colon, '{', '}');
      for (;;) {
        size_t next = skim_skip_ws(src, len, end);
        if (next + 1 < len && src[next] == '[' && src[next + 1] == ']') {
          end = next + 2;
          continue;
        }
        if (next < len && (src[next] == '|' || src[next] == '&')) {
          end = skim_ts_skip_type_tail(src, len, next);
          continue;
        }
        break;
      }
    }
    if (
      after_colon < len && src[after_colon] == '(' && skim_prev_non_ws(src, i) == ')' &&
      !skim_ts_colon_after_function_like_params(src, i)
    ) {
      size_t after_params = skim_skip_ws(src, len, skim_skip_balanced(src, len, after_colon, '(', ')'));
      if (after_params + 1 < len && src[after_params] == '=' && src[after_params + 1] == '>') {
        end = skim_ts_skip_return_type_tail(src, len, after_params + 2);
      }
    }
    if (end + 1 < len && src[end] == '=' && src[end + 1] == '>') {
      size_t full = skim_ts_skip_type_tail(src, len, i + 1);
      size_t after_full = skim_skip_ws(src, len, full);
      if (after_full < len && src[after_full] == '{' && skim_ts_colon_after_function_like_params(src, i)) end = full;
    }
    if (skim_word_any(src, len, end, "of\0in\0")) skim_str_putc(out, ' ');
    *io = end;
    return true;
  }

  if (
    src[i] == '?' && i + 1 < len &&
    (src[i + 1] == ':' || src[i + 1] == ',' || src[i + 1] == ')' || src[i + 1] == ';' || src[i + 1] == '(')
  ) {
    *io = src[i + 1] == ':' ? skim_ts_skip_type_tail(src, len, i + 2) : i + 1;
    return true;
  }

  if (src[i] == '!' && i + 1 < len && src[i + 1] != '=') {
    char prev = prev_non_ws_same_line_char(src, i);
    if (!(skim_is_id_part(prev) || prev == ')' || prev == ']')) return false;
    if (
      skim_word_before(src, i, "return") || skim_word_before(src, i, "throw") || skim_word_before(src, i, "yield") ||
      skim_word_before(src, i, "case") || skim_word_before(src, i, "delete") || skim_word_before(src, i, "void") ||
      skim_word_before(src, i, "typeof")
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
    prev_non_ws_same_line_char(src, i) != '\r' && skim_prev_non_ws(src, i) != '=' && skim_prev_non_ws(src, i) != '(' &&
    skim_prev_non_ws(src, i) != '[' && skim_prev_non_ws(src, i) != '{' && skim_prev_non_ws(src, i) != ',' &&
    skim_prev_non_ws(src, i) != ';' && skim_prev_non_ws(src, i) != ':' && skim_prev_non_ws(src, i) != '*' &&
    !skim_word_before(src, i, "var") && !skim_word_before(src, i, "let") && !skim_word_before(src, i, "const") &&
    !skim_word_before(src, i, "function")
  ) {
    *io = skim_ts_skip_type_tail(src, len, i + 2);
    return true;
  }

  if (skim_word_at(src, len, i, "satisfies")) {
    *io = skim_ts_skip_type_tail(src, len, i + 9);
    return true;
  }

  return false;
}
