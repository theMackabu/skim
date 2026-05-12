#include "syntax.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static void
push_item(skim_ast_program_t *program, skim_ast_item_kind_t kind, size_t start, size_t content_start, size_t end) {
  if (end <= start) return;
  if (program->item_count == program->item_cap) {
    size_t cap = program->item_cap ? program->item_cap * 2 : 128;
    skim_ast_item_t *items = realloc(program->items, sizeof(*items) * cap);
    if (!items) skim_die("out of memory");
    program->items = items;
    program->item_cap = cap;
  }
  program->items[program->item_count++] = (skim_ast_item_t){
    .kind = kind,
    .start = start,
    .content_start = content_start,
    .end = end,
  };
}

static bool range_has_newline(const char *src, size_t start, size_t end) {
  for (size_t i = start; i < end; i++) {
    if (src[i] == '\n' || src[i] == '\r') return true;
  }
  return false;
}

static bool declaration_name_on_same_line(const char *src, size_t len, size_t kw_end) {
  skim_syntax_token_t next = skim_syntax_token_at(src, len, kw_end, true);
  return next.kind != SKIM_TOKEN_EOF && !range_has_newline(src, kw_end, next.start);
}

static bool module_name_is_in_operator(const char *src, size_t len, size_t kw_end) {
  skim_syntax_token_t next = skim_syntax_token_at(src, len, kw_end, true);
  return next.kind == SKIM_TOKEN_IDENTIFIER && next.end - next.start == 2 && memcmp(src + next.start, "in", 2) == 0;
}

static skim_ast_item_kind_t classify_item(const char *src, size_t len, skim_syntax_token_t token) {
  if (token.kind != SKIM_TOKEN_KEYWORD) return SKIM_AST_ITEM_TEXT;
  switch (token.keyword) {
  case SKIM_KEYWORD_IMPORT:
    return SKIM_AST_ITEM_IMPORT;
  case SKIM_KEYWORD_EXPORT:
    return SKIM_AST_ITEM_EXPORT;
  case SKIM_KEYWORD_INTERFACE:
    return declaration_name_on_same_line(src, len, token.end) ? SKIM_AST_ITEM_INTERFACE : SKIM_AST_ITEM_TEXT;
  case SKIM_KEYWORD_TYPE:
    return declaration_name_on_same_line(src, len, token.end) ? SKIM_AST_ITEM_TYPE_ALIAS : SKIM_AST_ITEM_TEXT;
  case SKIM_KEYWORD_DECLARE:
    return declaration_name_on_same_line(src, len, token.end) ? SKIM_AST_ITEM_DECLARE : SKIM_AST_ITEM_TEXT;
  case SKIM_KEYWORD_ENUM:
    return SKIM_AST_ITEM_ENUM;
  case SKIM_KEYWORD_NAMESPACE:
    return declaration_name_on_same_line(src, len, token.end) ? SKIM_AST_ITEM_NAMESPACE : SKIM_AST_ITEM_TEXT;
  case SKIM_KEYWORD_MODULE:
    return declaration_name_on_same_line(src, len, token.end) && !module_name_is_in_operator(src, len, token.end)
             ? SKIM_AST_ITEM_NAMESPACE
             : SKIM_AST_ITEM_TEXT;
  case SKIM_KEYWORD_CLASS:
    return SKIM_AST_ITEM_CLASS;
  case SKIM_KEYWORD_CONST: {
    skim_syntax_token_t next = skim_syntax_token_at(src, len, token.end, true);
    return next.keyword == SKIM_KEYWORD_ENUM ? SKIM_AST_ITEM_ENUM : SKIM_AST_ITEM_TEXT;
  }
  default:
    return SKIM_AST_ITEM_TEXT;
  }
}

static void record_decl(const char *src, size_t len, size_t i) {
  if (skim_word_at(src, len, i, "export")) i = skim_skip_ws_comments(src, len, i + 6);

  unsigned flags = 0;
  size_t name_after = i;
  if (skim_word_at(src, len, i, "const")) {
    size_t j = skim_skip_ws_comments(src, len, i + 5);
    if (skim_word_at(src, len, j, "enum")) {
      flags = SKIM_DECL_VALUE | SKIM_DECL_ENUM;
      name_after = j + 4;
    } else {
      flags = SKIM_DECL_VALUE;
      name_after = i + 5;
    }
  } else if (skim_word_at(src, len, i, "enum")) {
    flags = SKIM_DECL_VALUE | SKIM_DECL_ENUM;
    name_after = i + 4;
  } else if (skim_word_at(src, len, i, "namespace")) {
    flags = SKIM_DECL_VALUE | SKIM_DECL_NAMESPACE;
    name_after = i + 9;
  } else if (skim_word_at(src, len, i, "module")) {
    flags = SKIM_DECL_VALUE | SKIM_DECL_NAMESPACE;
    name_after = i + 6;
  } else if (skim_word_at(src, len, i, "var")) {
    flags = SKIM_DECL_VALUE;
    name_after = i + 3;
  } else if (skim_word_at(src, len, i, "let")) {
    flags = SKIM_DECL_VALUE;
    name_after = i + 3;
  } else if (skim_word_at(src, len, i, "function")) {
    flags = SKIM_DECL_VALUE;
    name_after = i + 8;
  } else if (skim_word_at(src, len, i, "class")) {
    flags = SKIM_DECL_VALUE;
    name_after = i + 5;
  }

  if (!flags) return;
  size_t name_start = 0, name_end = 0;
  skim_parse_identifier(src, len, name_after, &name_start, &name_end);
  if (name_start != name_end) skim_decl_remember(src, src + name_start, src + name_end, i, flags);
}

static bool item_has_own_braced_body(const char *src, size_t len, size_t i) {
  if (skim_word_at(src, len, i, "export")) i = skim_skip_ws_comments(src, len, i + 6);
  if (skim_word_at(src, len, i, "const")) i = skim_skip_ws_comments(src, len, i + 5);
  if (skim_word_at(src, len, i, "interface") && !declaration_name_on_same_line(src, len, i + 9)) return false;
  if (skim_word_at(src, len, i, "namespace") && !declaration_name_on_same_line(src, len, i + 9)) return false;
  if (
    skim_word_at(src, len, i, "module") &&
    (!declaration_name_on_same_line(src, len, i + 6) || module_name_is_in_operator(src, len, i + 6))
  )
    return false;
  return skim_word_at(src, len, i, "enum") || skim_word_at(src, len, i, "namespace") ||
         skim_word_at(src, len, i, "module") || skim_word_at(src, len, i, "class") ||
         skim_word_at(src, len, i, "interface");
}

static size_t braced_item_end(const char *src, size_t len, size_t i) {
  int paren = 0, bracket = 0, angle = 0;
  while (i < len) {
    if (src[i] == '\'' || src[i] == '"' || src[i] == '`') {
      i = skim_skip_string_raw(src, len, i);
      continue;
    }
    if (src[i] == '(') paren++;
    else if (src[i] == ')' && paren > 0) paren--;
    else if (src[i] == '[') bracket++;
    else if (src[i] == ']' && bracket > 0) bracket--;
    else if (src[i] == '<') angle++;
    else if (src[i] == '>' && angle > 0) angle--;
    else if ((src[i] == '{' || src[i] == ';') && paren == 0 && bracket == 0 && angle == 0) break;
    i++;
  }
  if (i < len && src[i] == '{') {
    i = skim_skip_balanced(src, len, i, '{', '}');
    if (i < len && src[i] == ';') i++;
    return i;
  }
  if (i < len && src[i] == ';') return i + 1;
  return i;
}

static size_t skip_ts_type_tail_for_item(const char *src, size_t len, size_t i) {
  int paren = 0, bracket = 0, brace = 0, angle = 0;
  bool seen_type_token = false;
  while (i < len) {
    char c = src[i];
    if ((c == '\n' || c == '\r') && paren == 0 && bracket == 0 && brace == 0 && angle == 0 && seen_type_token) break;
    if (c == '\'' || c == '"' || c == '`') {
      seen_type_token = true;
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
    if (isspace((unsigned char)c)) {
      i++;
      continue;
    }
    if (c == '(') paren++;
    else if (c == ')' && paren-- <= 0) break;
    else if (c == '[') bracket++;
    else if (c == ']' && bracket-- <= 0) break;
    else if (c == '{') brace++;
    else if (c == '}' && brace-- <= 0) break;
    else if (c == '<') angle++;
    else if (c == '>' && angle > 0) angle--;
    else if (c == '=' && i + 1 < len && src[i + 1] == '>') {
      i += 2;
      continue;
    } else if (paren == 0 && bracket == 0 && brace == 0 && angle == 0) {
      if (c == '=' || c == ',' || c == ';' || c == '\n' || c == '\r') break;
    }
    seen_type_token = true;
    i++;
  }
  return i;
}

static bool next_starts_asi_item(const char *src, size_t len, size_t i) {
  i = skim_skip_ws_comments(src, len, i);
  if (i >= len) return true;
  if (src[i] == '.' || src[i] == ',' || src[i] == ';' || src[i] == ')' || src[i] == ']' || src[i] == '}') return false;
  if (
    src[i] == '+' || src[i] == '-' || src[i] == '*' || src[i] == '/' || src[i] == '%' || src[i] == '<' ||
    src[i] == '>' || src[i] == '=' || src[i] == '&' || src[i] == '|' || src[i] == '?' || src[i] == ':'
  )
    return false;
  return true;
}

static bool prev_allows_asi_item(const char *src, size_t i) {
  size_t line = i;
  while (line > 0 && src[line - 1] != '\n' && src[line - 1] != '\r')
    line--;
  size_t comment = i;
  for (size_t k = line; k + 1 < i; k++) {
    if (src[k] == '/' && src[k + 1] == '/') {
      comment = k;
      break;
    }
  }
  i = comment;
  while (i > 0 && isspace((unsigned char)src[i - 1]))
    i--;
  if (i == 0) return false;
  size_t word_end = i;
  while (i > 0 && skim_is_id_part(src[i - 1]))
    i--;
  if (word_end > i) {
    if (
      (word_end - i == 3 && memcmp(src + i, "var", 3) == 0) || (word_end - i == 3 && memcmp(src + i, "let", 3) == 0) ||
      (word_end - i == 5 && memcmp(src + i, "const", 5) == 0)
    ) {
      return false;
    }
    i = word_end;
  }
  char prev = src[i - 1];
  if (
    prev == '(' || prev == '[' || prev == '{' || prev == ':' || prev == ',' || prev == '.' || prev == '?' ||
    prev == '=' || prev == '+' || prev == '-' || prev == '*' || prev == '/' || prev == '%' || prev == '<' ||
    prev == '>' || prev == '&' || prev == '|' || prev == '!' || prev == '~' || prev == '^'
  )
    return false;
  return true;
}

static bool item_ends_in_line_comment(const char *src, size_t start, size_t end) {
  while (end > start && (src[end - 1] == '\n' || src[end - 1] == '\r'))
    end--;
  size_t line = end;
  while (line > start && src[line - 1] != '\n' && src[line - 1] != '\r')
    line--;
  for (size_t i = line; i + 1 < end; i++) {
    if (src[i] == '/' && src[i + 1] == '/') return true;
  }
  return false;
}

static bool out_ends_in_line_comment(const skim_str_t *out) {
  size_t end = out->len;
  while (end > 0 && (out->data[end - 1] == '\n' || out->data[end - 1] == '\r'))
    end--;
  size_t line = end;
  while (line > 0 && out->data[line - 1] != '\n' && out->data[line - 1] != '\r')
    line--;
  for (size_t i = line; i + 1 < end; i++) {
    if (out->data[i] == '/' && out->data[i + 1] == '/') return true;
  }
  return false;
}

static size_t variable_item_end(const char *src, size_t len, size_t i) {
  int paren = 0, bracket = 0, brace = 0;
  while (i < len) {
    char c = src[i];
    if (c == '\'' || c == '"' || c == '`') {
      i = skim_skip_string_raw(src, len, i);
      continue;
    }
    if (i + 1 < len && c == '/' && src[i + 1] == '/') {
      size_t comment = i;
      while (i < len && src[i] != '\n')
        i++;
      if (
        paren == 0 && bracket == 0 && brace == 0 && prev_allows_asi_item(src, comment) &&
        next_starts_asi_item(src, len, i + 1)
      )
        return i;
      continue;
    }
    if (i + 1 < len && c == '/' && src[i + 1] == '*') {
      i += 2;
      while (i + 1 < len && !(src[i] == '*' && src[i + 1] == '/'))
        i++;
      if (i + 1 < len) i += 2;
      continue;
    }
    if (c == ':' && paren == 0 && bracket == 0 && brace == 0) {
      i = skip_ts_type_tail_for_item(src, len, i + 1);
      continue;
    }
    if (c == '(') paren++;
    else if (c == ')' && paren > 0) paren--;
    else if (c == '[') bracket++;
    else if (c == ']' && bracket > 0) bracket--;
    else if (c == '{') {
      i = skim_skip_balanced(src, len, i, '{', '}');
      continue;
    } else if (c == ';' && paren == 0 && bracket == 0 && brace == 0) {
      return i + 1;
    } else if ((c == '\n' || c == '\r') && paren == 0 && bracket == 0 && brace == 0 && prev_allows_asi_item(src, i)) {
      size_t next = skim_skip_ws_comments(src, len, i + 1);
      if (
        next >= len || skim_word_at(src, len, next, "var") || skim_word_at(src, len, next, "let") ||
        skim_word_at(src, len, next, "const") || skim_word_at(src, len, next, "function") ||
        skim_word_at(src, len, next, "class") || skim_word_at(src, len, next, "interface") ||
        skim_word_at(src, len, next, "type") || skim_word_at(src, len, next, "enum") ||
        skim_word_at(src, len, next, "namespace") || skim_word_at(src, len, next, "module") ||
        skim_word_at(src, len, next, "export") || skim_word_at(src, len, next, "import") ||
        next_starts_asi_item(src, len, next)
      ) {
        return i;
      }
    }
    i++;
  }
  return i;
}

static bool item_is_variable_decl(const char *src, size_t len, size_t i) {
  if (skim_word_at(src, len, i, "export")) i = skim_skip_ws_comments(src, len, i + 6);
  if (skim_word_at(src, len, i, "declare")) i = skim_skip_ws_comments(src, len, i + 7);
  return skim_word_at(src, len, i, "var") || skim_word_at(src, len, i, "let") || skim_word_at(src, len, i, "const");
}

static size_t type_alias_item_end(const char *src, size_t len, size_t i) {
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
      if (next < len && (src[next] == '|' || src[next] == '&' || src[next] == '?' || src[next] == ':')) {
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
    if (i + 1 < len && src[i] == '/' && src[i + 1] == '/') {
      while (i < len && src[i] != '\n')
        i++;
      continue;
    }
    if (i + 1 < len && src[i] == '/' && src[i + 1] == '*') {
      i += 2;
      while (i + 1 < len && !(src[i] == '*' && src[i + 1] == '/'))
        i++;
      if (i + 1 < len) i += 2;
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
    if (!isspace((unsigned char)c)) {
      seen_type_token = true;
      last_sig = c;
    }
    i++;
  }
  return i;
}

static size_t text_item_end(const char *src, size_t len, size_t i) {
  bool starts_decorator = i < len && src[i] == '@';
  bool starts_export = skim_word_at(src, len, i, "export");
  int paren = 0, bracket = 0, brace = 0;
  while (i < len) {
    char c = src[i];
    if (c == '\'' || c == '"' || c == '`') {
      i = skim_skip_string_raw(src, len, i);
      continue;
    }
    if (i + 1 < len && c == '/' && src[i + 1] == '/') {
      size_t comment = i;
      while (i < len && src[i] != '\n')
        i++;
      if (starts_decorator || starts_export) {
        size_t next = skim_skip_ws_comments(src, len, i + 1);
        if (
          next < len &&
          (src[next] == '@' || skim_word_at(src, len, next, "export") || skim_word_at(src, len, next, "class"))
        ) {
          continue;
        }
      }
      if (
        paren == 0 && bracket == 0 && brace == 0 && prev_allows_asi_item(src, comment) &&
        next_starts_asi_item(src, len, i + 1)
      )
        return i;
      continue;
    }
    if (i + 1 < len && c == '/' && src[i + 1] == '*') {
      i += 2;
      while (i + 1 < len && !(src[i] == '*' && src[i + 1] == '/'))
        i++;
      if (i + 1 < len) i += 2;
      continue;
    }
    if (c == '(') paren++;
    else if (c == ')' && paren > 0) paren--;
    else if (c == '[') bracket++;
    else if (c == ']' && bracket > 0) bracket--;
    else if (c == '{') {
      i = skim_skip_balanced(src, len, i, '{', '}');
      continue;
    } else if (c == ';' && paren == 0 && bracket == 0 && brace == 0) {
      return i + 1;
    } else if ((c == '\n' || c == '\r') && paren == 0 && bracket == 0 && brace == 0 && prev_allows_asi_item(src, i)) {
      if (starts_decorator || starts_export) {
        size_t next = skim_skip_ws_comments(src, len, i + 1);
        if (
          next < len &&
          (src[next] == '@' || skim_word_at(src, len, next, "export") || skim_word_at(src, len, next, "class"))
        ) {
          i++;
          continue;
        }
      }
      if (next_starts_asi_item(src, len, i + 1)) return i;
    }
    i++;
  }
  return i;
}

static char out_prev_non_ws(const skim_str_t *out) {
  size_t i = out->len;
  while (i > 0 && isspace((unsigned char)out->data[i - 1]))
    i--;
  return i > 0 ? out->data[i - 1] : '\0';
}

static bool item_starts_statement_expression(const char *src, size_t len, size_t i) {
  i = skim_skip_ws_comments(src, len, i);
  if (i >= len) return false;
  if (
    src[i] == '(' || src[i] == '[' || src[i] == '\'' || src[i] == '"' || src[i] == '`' || src[i] == '+' ||
    src[i] == '-' || src[i] == '!' || src[i] == '~'
  )
    return true;
  if (
    skim_word_at(src, len, i, "new") || skim_word_at(src, len, i, "this") || skim_word_at(src, len, i, "super") ||
    skim_word_at(src, len, i, "await") || skim_word_at(src, len, i, "yield")
  )
    return true;
  if (!skim_is_id_start(src[i])) return false;
  size_t start = 0, end = 0;
  size_t after = skim_parse_identifier(src, len, i, &start, &end);
  if (start == end) return false;
  after = skim_skip_ws_comments(src, len, after);
  return after < len && (src[after] == '.' || src[after] == '[' || src[after] == '(' || src[after] == '`' ||
                         (src[after] == '?' && after + 1 < len && src[after + 1] == '.'));
}

bool skim_syntax_item_needs_statement_semicolon(
  const skim_ast_item_t *item,
  const char *src,
  size_t len,
  const skim_str_t *out
) {
  if (item->kind != SKIM_AST_ITEM_TEXT) return false;
  if (item_ends_in_line_comment(src, item->start, item->end)) return false;
  if (out_ends_in_line_comment(out)) return false;
  char prev = out_prev_non_ws(out);
  if (prev == '\0' || prev == ';' || prev == '{') return false;
  size_t i = skim_skip_ws_comments(src, len, item->content_start);
  if (i >= len) return false;
  if (
    skim_word_at(src, len, i, "function") || skim_word_at(src, len, i, "class") || skim_word_at(src, len, i, "if") ||
    skim_word_at(src, len, i, "for") || skim_word_at(src, len, i, "while") || skim_word_at(src, len, i, "switch") ||
    skim_word_at(src, len, i, "try") || skim_word_at(src, len, i, "do")
  )
    return false;
  return item_is_variable_decl(src, len, i) || item_starts_statement_expression(src, len, i);
}

void skim_parse_program(skim_ast_program_t *program, const char *src, size_t len) {
  *program = (skim_ast_program_t){.src = src, .len = len};
  for (size_t i = 0; i < len;) {
    size_t start = i;
    skim_syntax_token_t first = skim_syntax_token_at(src, len, i, true);
    size_t content_start = first.start;
    if (first.kind == SKIM_TOKEN_EOF) {
      push_item(program, SKIM_AST_ITEM_TEXT, start, content_start, len);
      break;
    }

    skim_ast_item_kind_t kind = classify_item(src, len, first);
    record_decl(src, len, content_start);
    size_t end = item_has_own_braced_body(src, len, content_start)
      ? braced_item_end(src, len, content_start)
      : (item_is_variable_decl(src, len, content_start)
          ? variable_item_end(src, len, content_start)
          : (kind == SKIM_AST_ITEM_TYPE_ALIAS ? type_alias_item_end(src, len, content_start)
            : kind == SKIM_AST_ITEM_DECLARE ? skim_skip_statement_like(src, len, content_start)
            : text_item_end(src, len, content_start)));
  if (end <= content_start) end = content_start + 1;
    push_item(program, kind, start, content_start, end);
    i = end;
  }
}
