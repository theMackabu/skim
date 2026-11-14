#include "parse_internal.h"

typedef struct {
  bool is_static;
  bool removed;
  bool accessor;
} member_modifiers_t;

static bool member_name_follows(const parser_t *p, uint8_t modifier) {
  uint32_t next = p->i + 1;
  switch (kind_at(p, next)) {
  case SKIM_T_WORD:
  case SKIM_T_STRING:
  case SKIM_T_NUMBER:
  case SKIM_T_PRIVATE:
  case '[':
  case '*':
    break;
  case '{':
    return modifier == SKIM_KW_STATIC;
  default:
    return false;
  }
  if (!newline_at(p, next)) return true;
  return modifier == SKIM_KW_STATIC || modifier == SKIM_KW_GET || modifier == SKIM_KW_SET;
}

static void member_modifiers(parser_t *p, member_modifiers_t *modifiers) {
  for (;;) {
    uint8_t w = word(p);
    switch (w) {
    case SKIM_KW_PUBLIC:
    case SKIM_KW_PRIVATE:
    case SKIM_KW_PROTECTED:
    case SKIM_KW_READONLY:
    case SKIM_KW_OVERRIDE:
    case SKIM_KW_ABSTRACT:
    case SKIM_KW_DECLARE:
      if (!member_name_follows(p, w)) return;
      if (w == SKIM_KW_ABSTRACT || w == SKIM_KW_DECLARE) modifiers->removed = true;
      if (w != SKIM_KW_OVERRIDE || word_at(p, p->i + 1) != SKIM_KW_ACCESSOR) strip(p, p->i, p->i + 1);
      break;
    case SKIM_KW_STATIC:
      if (!member_name_follows(p, w)) return;
      if (kind_at(p, p->i + 1) == '{') return;
      modifiers->is_static = true;
      break;
    case SKIM_KW_ACCESSOR:
      if (!member_name_follows(p, w)) return;
      modifiers->accessor = true;
      break;
    case SKIM_KW_ASYNC:
    case SKIM_KW_GET:
    case SKIM_KW_SET:
      if (!member_name_follows(p, w)) return;
      break;
    default:
      return;
    }
    p->i++;
  }
}

static bool index_signature(parser_t *p, uint32_t start) {
  if (kind(p) != '[' || kind_at(p, p->i + 1) != SKIM_T_WORD || kind_at(p, p->i + 2) != ':') return false;
  uint32_t end = after_pair(p, p->i);
  if (kind_at(p, end) == ':') end = skim_type_end(p, end + 1);
  if (kind_at(p, end) == ';' || kind_at(p, end) == ',') end++;
  strip(p, start, end);
  p->i = end;
  return true;
}

static void class_member(parser_t *p) {
  uint32_t start = p->i;
  member_modifiers_t modifiers = {0};
  flag(p, start, SKIM_TF_LEAD);
  skim_parse_decorators(p);
  member_modifiers(p, &modifiers);
  if (word(p) == SKIM_KW_STATIC && kind_at(p, p->i + 1) == '{') {
    p->i++;
    skim_parse_block(p);
    return;
  }
  if (kind(p) == '*') p->i++;
  if (index_signature(p, start)) return;

  uint32_t key = p->i;
  switch (kind(p)) {
  case '[':
    skim_parse_list(p, p->i);
    break;
  case SKIM_T_WORD:
  case SKIM_T_STRING:
  case SKIM_T_NUMBER:
  case SKIM_T_PRIVATE:
    p->i++;
    break;
  default:
    return;
  }
  if (kind(p) == '?' || kind(p) == '!') {
    strip(p, p->i, p->i + 1);
    p->i++;
  }

  if (kind(p) == '(' || kind(p) == '<') {
    p->constructor = word_at(p, key) == SKIM_KW_CONSTRUCTOR && !modifiers.is_static && !modifiers.removed;
    if (skim_parse_function_tail(p) && !modifiers.removed) return;
    if (kind(p) == ';') p->i++;
    strip(p, start, p->i);
    return;
  }

  uint8_t flags = (modifiers.is_static ? SKIM_NF_STATIC : 0) | (modifiers.accessor ? SKIM_NF_ACCESSOR : 0);
  uint32_t node = skim_node_open(p, SKIM_N_FIELD, start, flags);
  if (kind(p) == ':') {
    uint32_t end = skim_type_end(p, p->i + 1);
    strip(p, p->i, end);
    p->i = end;
  }
  uint32_t value = 0;
  if (kind(p) == '=') {
    value = p->i;
    p->i++;
    skim_parse_assignment(p, false);
  }
  if (kind(p) == ';') p->i++;
  if (modifiers.removed) strip(p, start, p->i);
  skim_node_t *field = skim_node_close(p, node);
  if (!field) return;
  field->field.key = key;
  field->field.value = value;
}

void skim_parse_class(parser_t *p, uint8_t flags) {
  uint32_t node = skim_node_open(p, SKIM_N_CLASS, p->i, flags);
  uint32_t name = 0;
  p->i++;
  if (kind(p) == SKIM_T_WORD && word(p) != SKIM_KW_EXTENDS && word(p) != SKIM_KW_IMPLEMENTS) name = p->i++;
  if (name && !(flags & SKIM_NF_EXPRESSION)) skim_declare(p, name, SKIM_D_CLASS);
  if (kind(p) == '<') {
    uint32_t end = skim_angle_end(p, p->i);
    if (end) {
      strip(p, p->i, end);
      p->i = end;
    }
  }
  if (word(p) == SKIM_KW_EXTENDS) {
    p->i++;
    skim_parse_heritage(p);
  }
  if (word(p) == SKIM_KW_IMPLEMENTS) {
    uint32_t start = p->i++;
    for (;;) {
      p->i = skim_type_end(p, p->i);
      if (kind(p) != ',') break;
      p->i++;
    }
    strip(p, start, p->i);
  }
  uint32_t body = p->i;
  if (kind(p) == '{') {
    uint32_t close = close_of(p, body);
    p->i++;
    while (p->i < close) {
      uint32_t before = p->i;
      if (kind(p) == ';') p->i++;
      else class_member(p);
      if (p->i == before) p->i++;
    }
    uint32_t after = after_pair(p, body);
    if (p->i < after) p->i = after;
  }
  skim_node_t *class = skim_node_close(p, node);
  if (!class) return;
  class->decl.name = name;
  class->decl.body = body;
}
