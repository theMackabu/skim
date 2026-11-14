#include "parse_internal.h"

static void semicolon(parser_t *p) {
  if (kind(p) == ';') p->i++;
}

static void leave(parser_t *p, uint32_t open) {
  uint32_t after = after_pair(p, open);
  if (p->i < after) p->i = after;
}

void skim_parse_statements(parser_t *p, uint32_t end) {
  while (p->i < end) {
    uint32_t before = p->i;
    skim_parse_statement(p);
    if (p->i == before) p->i++;
  }
}

void skim_parse_block(parser_t *p) {
  uint32_t open = p->i;
  scope_mark_t scope = skim_scope_push(p, open, false);
  p->i++;
  skim_parse_statements(p, close_of(p, open));
  leave(p, open);
  skim_scope_pop(p, scope);
}

static void embedded_statement(parser_t *p) {
  uint32_t first = p->i;
  skim_parse_statement(p);
  bool removed = p->i > first && p->t[first].flags & SKIM_TF_STRIP && p->t[p->i - 1].flags & SKIM_TF_STRIP;
  if (removed) flag(p, first, SKIM_TF_EMPTY);
}

static void parenthesized(parser_t *p) {
  if (kind(p) != '(') return;
  uint32_t open = p->i;
  p->i++;
  skim_parse_expression(p);
  leave(p, open);
}

void skim_parse_variables(parser_t *p) {
  skim_decl_kind_t decl = word(p) == SKIM_KW_VAR ? SKIM_D_VAR : SKIM_D_LET;
  if (word(p) == SKIM_KW_AWAIT) p->i++;
  p->i++;
  for (;;) {
    skim_parse_binding(p, decl);
    if (kind(p) == '!') {
      strip(p, p->i, p->i + 1);
      p->i++;
    }
    if (kind(p) == ':') {
      uint32_t end = skim_type_end(p, p->i + 1);
      strip(p, p->i, end);
      p->i = end;
    }
    if (kind(p) == '=') {
      p->i++;
      skim_parse_assignment(p, false);
    }
    if (kind(p) != ',') return;
    p->i++;
  }
}

static bool variable_keyword(const parser_t *p) {
  switch (word(p)) {
  case SKIM_KW_VAR:
  case SKIM_KW_CONST:
    return true;
  case SKIM_KW_LET: {
    uint8_t next = kind_at(p, p->i + 1);
    return next == SKIM_T_WORD || next == '[' || next == '{';
  }
  case SKIM_KW_USING:
    return kind_at(p, p->i + 1) == SKIM_T_WORD && !newline_at(p, p->i + 1) &&
           !skim_keyword_attrs[word_at(p, p->i + 1)] && word_at(p, p->i + 1) != SKIM_KW_OF;
  case SKIM_KW_AWAIT:
    return word_at(p, p->i + 1) == SKIM_KW_USING && kind_at(p, p->i + 2) == SKIM_T_WORD && !newline_at(p, p->i + 2);
  default:
    return false;
  }
}

static void for_statement(parser_t *p) {
  scope_mark_t scope = skim_scope_push(p, p->i, false);
  p->i++;
  if (word(p) == SKIM_KW_AWAIT) p->i++;
  if (kind(p) == '(') {
    uint32_t open = p->i;
    p->i++;
    if (variable_keyword(p)) skim_parse_variables(p);
    else if (kind(p) != ';') skim_parse_expression(p);
    if (kind(p) == ';') {
      p->i++;
      skim_parse_expression(p);
      semicolon(p);
      skim_parse_expression(p);
    } else if (word(p) == SKIM_KW_OF || word(p) == SKIM_KW_IN) {
      p->i++;
      skim_parse_expression(p);
    }
    leave(p, open);
  }
  embedded_statement(p);
  skim_scope_pop(p, scope);
}

static void switch_statement(parser_t *p) {
  p->i++;
  parenthesized(p);
  if (kind(p) != '{') return;
  uint32_t open = p->i;
  uint32_t close = close_of(p, open);
  p->i++;
  while (p->i < close) {
    uint32_t before = p->i;
    if (word(p) == SKIM_KW_CASE) {
      p->i++;
      skim_parse_expression(p);
      if (kind(p) == ':') p->i++;
    } else if (word(p) == SKIM_KW_DEFAULT && kind_at(p, p->i + 1) == ':') {
      p->i += 2;
    } else {
      skim_parse_statement(p);
    }
    if (p->i == before) p->i++;
  }
  leave(p, open);
}

static void try_statement(parser_t *p) {
  p->i++;
  if (kind(p) == '{') skim_parse_block(p);
  if (word(p) == SKIM_KW_CATCH) {
    scope_mark_t scope = skim_scope_push(p, p->i, false);
    p->i++;
    if (kind(p) == '(') {
      uint32_t open = p->i;
      p->i++;
      skim_parse_binding(p, SKIM_D_LET);
      if (kind(p) == ':') strip(p, p->i, close_of(p, open));
      leave(p, open);
    }
    if (kind(p) == '{') skim_parse_block(p);
    skim_scope_pop(p, scope);
  }
  if (word(p) == SKIM_KW_FINALLY) {
    p->i++;
    if (kind(p) == '{') skim_parse_block(p);
  }
}

static void function_declaration(parser_t *p, uint32_t start) {
  p->i++;
  if (kind(p) == '*') p->i++;
  if (kind(p) == SKIM_T_WORD) skim_declare(p, p->i++, SKIM_D_FUNCTION);
  if (skim_parse_function_tail(p)) return;
  semicolon(p);
  strip(p, start, p->i);
  p->runtime = true;
}

static bool type_alias(parser_t *p) {
  uint32_t start = p->i;
  uint32_t name = start + 1;
  if (kind_at(p, name) != SKIM_T_WORD || newline_at(p, name)) return false;
  uint32_t equals = name + 1;
  if (kind_at(p, equals) == '<') equals = skim_angle_end(p, equals);
  if (!equals || kind_at(p, equals) != '=') return false;
  uint32_t end = skim_type_end(p, equals + 1);
  if (kind_at(p, end) == ';') end++;
  skim_declare(p, name, SKIM_D_TYPE);
  strip(p, start, end);
  p->i = end;
  return true;
}

static bool interface_declaration(parser_t *p) {
  uint32_t start = p->i;
  uint32_t body = start + 1;
  if (kind_at(p, body) != SKIM_T_WORD || newline_at(p, body)) return false;
  while (body < p->count && kind_at(p, body) != '{' && kind_at(p, body) != ';') {
    uint32_t angle = kind_at(p, body) == '<' ? skim_angle_end(p, body) : 0;
    body = angle ? angle : body + 1;
  }
  if (kind_at(p, body) != '{') return false;
  uint32_t end = after_pair(p, body);
  skim_declare(p, start + 1, SKIM_D_TYPE);
  strip(p, start, end);
  p->i = end;
  return true;
}

static bool declare_follows(const parser_t *p) {
  uint32_t next = p->i + 1;
  if (newline_at(p, next)) return false;
  switch (word_at(p, next)) {
  case SKIM_KW_CONST:
  case SKIM_KW_LET:
  case SKIM_KW_VAR:
  case SKIM_KW_FUNCTION:
  case SKIM_KW_ASYNC:
  case SKIM_KW_CLASS:
  case SKIM_KW_ABSTRACT:
  case SKIM_KW_ENUM:
  case SKIM_KW_NAMESPACE:
  case SKIM_KW_MODULE:
  case SKIM_KW_GLOBAL:
  case SKIM_KW_TYPE:
  case SKIM_KW_INTERFACE:
  case SKIM_KW_IMPORT:
    return true;
  default:
    return false;
  }
}

static uint32_t ambient_name(const parser_t *p, uint32_t keyword) {
  uint32_t name = keyword + 1;
  switch (word_at(p, keyword)) {
  case SKIM_KW_ABSTRACT:
  case SKIM_KW_ASYNC:
    name++;
    break;
  case SKIM_KW_CONST:
    if (word_at(p, name) == SKIM_KW_ENUM) name++;
    break;
  case SKIM_KW_LET:
  case SKIM_KW_VAR:
  case SKIM_KW_FUNCTION:
  case SKIM_KW_CLASS:
  case SKIM_KW_ENUM:
    break;
  default:
    return 0;
  }
  if (kind_at(p, name) == '*') name++;
  return kind_at(p, name) == SKIM_T_WORD ? name : 0;
}

static void ambient_statement(parser_t *p, uint32_t start) {
  p->dry++;
  p->i++;
  skim_parse_statement(p);
  p->dry--;
  strip(p, start, p->i);
}

static bool namespace_follows(const parser_t *p) {
  uint32_t name = p->i + 1;
  if (newline_at(p, name)) return false;
  if (kind_at(p, name) == SKIM_T_STRING) return word(p) == SKIM_KW_MODULE;
  if (kind_at(p, name) != SKIM_T_WORD || skim_keyword_attrs[p->t[name].word] & SKIM_KW_INFIX) return false;
  uint8_t next = kind_at(p, name + 1);
  return next == '{' || (next == '.' && kind_at(p, name + 2) == SKIM_T_WORD);
}

static bool word_statement(parser_t *p) {
  uint32_t start = p->i;
  uint32_t next = start + 1;
  switch (p->t[start].word) {
  case SKIM_KW_VAR:
  case SKIM_KW_LET:
  case SKIM_KW_USING:
  case SKIM_KW_AWAIT:
  case SKIM_KW_CONST:
    if (word(p) == SKIM_KW_CONST && word_at(p, next) == SKIM_KW_ENUM) {
      skim_parse_enum(p, start, SKIM_NF_CONST);
      return true;
    }
    if (!variable_keyword(p)) return false;
    skim_parse_variables(p);
    semicolon(p);
    return true;
  case SKIM_KW_FUNCTION:
    function_declaration(p, start);
    return true;
  case SKIM_KW_ASYNC:
    if (word_at(p, next) != SKIM_KW_FUNCTION || newline_at(p, next)) return false;
    p->i++;
    function_declaration(p, start);
    return true;
  case SKIM_KW_CLASS:
    skim_parse_class(p, 0);
    return true;
  case SKIM_KW_ABSTRACT:
    if (word_at(p, next) != SKIM_KW_CLASS || newline_at(p, next)) return false;
    strip(p, start, next);
    p->i++;
    skim_parse_class(p, 0);
    return true;
  case SKIM_KW_ENUM:
    if (kind_at(p, next) != SKIM_T_WORD || newline_at(p, next)) return false;
    skim_parse_enum(p, start, 0);
    return true;
  case SKIM_KW_NAMESPACE:
  case SKIM_KW_MODULE:
    if (!namespace_follows(p)) return false;
    skim_parse_namespace(p, start, 0);
    return true;
  case SKIM_KW_GLOBAL:
    if (kind_at(p, next) != '{' || newline_at(p, next)) return false;
    p->dry++;
    p->i++;
    skim_parse_block(p);
    p->dry--;
    strip(p, start, p->i);
    p->runtime = true;
    return true;
  case SKIM_KW_TYPE:
    return type_alias(p);
  case SKIM_KW_INTERFACE:
    return interface_declaration(p);
  case SKIM_KW_DECLARE:
    if (!declare_follows(p)) return false;
    ambient_statement(p, start);
    if (ambient_name(p, next)) skim_declare(p, ambient_name(p, next), SKIM_D_AMBIENT);
    switch (word_at(p, next)) {
    case SKIM_KW_NAMESPACE:
    case SKIM_KW_MODULE:
    case SKIM_KW_TYPE:
    case SKIM_KW_INTERFACE:
    case SKIM_KW_IMPORT:
      break;
    default:
      p->runtime = true;
      break;
    }
    return true;
  case SKIM_KW_IMPORT:
    if (kind_at(p, next) == '(' || kind_at(p, next) == '.') return false;
    skim_parse_import(p);
    return true;
  case SKIM_KW_EXPORT:
    skim_parse_export(p);
    return true;
  case SKIM_KW_IF:
    p->i++;
    parenthesized(p);
    embedded_statement(p);
    if (word(p) == SKIM_KW_ELSE) {
      p->i++;
      embedded_statement(p);
    }
    return true;
  case SKIM_KW_FOR:
    for_statement(p);
    return true;
  case SKIM_KW_WHILE:
  case SKIM_KW_WITH:
    p->i++;
    parenthesized(p);
    embedded_statement(p);
    return true;
  case SKIM_KW_DO:
    p->i++;
    embedded_statement(p);
    if (word(p) == SKIM_KW_WHILE) {
      p->i++;
      parenthesized(p);
    }
    semicolon(p);
    return true;
  case SKIM_KW_SWITCH:
    switch_statement(p);
    return true;
  case SKIM_KW_TRY:
    try_statement(p);
    return true;
  case SKIM_KW_RETURN:
  case SKIM_KW_THROW:
    p->i++;
    if (!newline_at(p, p->i)) skim_parse_expression(p);
    semicolon(p);
    return true;
  case SKIM_KW_BREAK:
  case SKIM_KW_CONTINUE:
    p->i++;
    if (kind(p) == SKIM_T_WORD && !newline_at(p, p->i)) p->i++;
    semicolon(p);
    return true;
  case SKIM_KW_DEBUGGER:
    p->i++;
    semicolon(p);
    return true;
  default:
    return false;
  }
}

void skim_parse_statement(parser_t *p) {
  if (++p->depth > SKIM_PARSE_MAX_DEPTH) {
    p->i = p->count;
    p->depth--;
    return;
  }
  uint32_t first = p->i;
  flag(p, first, SKIM_TF_LEAD);
  switch (kind(p)) {
  case '{':
    skim_parse_block(p);
    break;
  case ';':
    p->i++;
    break;
  case '@':
    skim_parse_decorators(p);
    if (word(p) == SKIM_KW_EXPORT) {
      p->decorators = first + 1;
      skim_parse_export(p);
    }
    break;
  case SKIM_T_WORD:
    if (word_statement(p)) break;
    if (kind_at(p, p->i + 1) == ':' && !skim_keyword_attrs[word(p)]) {
      p->i += 2;
      break;
    }
    [[fallthrough]];
  default:
    skim_parse_expression(p);
    semicolon(p);
    break;
  }
  if (
    !p->dry && !(p->t[first].flags & SKIM_TF_STRIP) &&
    !(word_at(p, first) == SKIM_KW_IMPORT && kind_at(p, first + 2) == '=')
  )
    p->runtime = true;
  p->depth--;
}
