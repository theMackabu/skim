#include "parse_internal.h"

constexpr uint32_t MAX_TYPE_DEPTH = 400;

static thread_local uint32_t type_depth;
static thread_local bool in_extends;

static uint32_t type_operand(const parser_t *p, uint32_t i);
static uint32_t type_primary_end(const parser_t *p, uint32_t i);

static uint32_t entity_name_end(const parser_t *p, uint32_t i) {
  while (kind_at(p, i) == '.' && kind_at(p, i + 1) == SKIM_T_WORD)
    i += 2;
  return i;
}

static uint32_t type_arguments_after(const parser_t *p, uint32_t i) {
  if (kind_at(p, i) != '<') return i;
  uint32_t end = skim_angle_end(p, i);
  return end ? end : i;
}

static uint32_t template_end(const parser_t *p, uint32_t i) {
  while (i < p->count && !(p->t[i].flags & SKIM_TF_TAIL)) {
    uint32_t next = p->t[i].pair;
    if (next <= i || next >= p->count) return p->count;
    i = next;
  }
  return i < p->count ? i + 1 : p->count;
}

static bool parameter_list(const parser_t *p, uint32_t open) {
  uint32_t first = open + 1;
  switch (kind_at(p, first)) {
  case ')':
  case SKIM_T_SPREAD:
    return true;
  case '{':
  case '[':
    first = after_pair(p, first);
    break;
  case SKIM_T_WORD:
    first++;
    break;
  default:
    return false;
  }
  switch (kind_at(p, first)) {
  case ':':
  case ',':
  case '?':
  case '=':
    return true;
  default:
    return kind_at(p, first) == ')' && kind_at(p, first + 1) == SKIM_T_ARROW;
  }
}

static uint32_t function_type_end(const parser_t *p, uint32_t open) {
  uint32_t after = after_pair(p, open);
  if (kind_at(p, after) != SKIM_T_ARROW || !parameter_list(p, open)) return 0;
  uint32_t end = skim_type_end(p, after + 1);
  return end > after + 1 ? end : after + 1;
}

static uint32_t type_union_end(const parser_t *p, uint32_t i) {
  uint32_t start = i;
  if (kind_at(p, i) == '|' || kind_at(p, i) == '&') i++;
  for (;;) {
    uint32_t next = type_operand(p, i);
    if (next == i) return i == start + 1 ? start : i;
    i = next;
    if (kind_at(p, i) != '|' && kind_at(p, i) != '&') return i;
    i++;
  }
}

static uint32_t conditional_type_end(const parser_t *p, uint32_t i) {
  uint32_t end = type_union_end(p, i);
  if (end == i || word_at(p, end) != SKIM_KW_EXTENDS) return end;
  in_extends = true;
  uint32_t constraint = type_union_end(p, end + 1);
  in_extends = false;
  if (constraint == end + 1 || kind_at(p, constraint) != '?') return end;
  uint32_t when_true = skim_type_end(p, constraint + 1);
  if (when_true == constraint + 1 || kind_at(p, when_true) != ':') return end;
  uint32_t when_false = skim_type_end(p, when_true + 1);
  return when_false == when_true + 1 ? end : when_false;
}

uint32_t skim_type_end(const parser_t *p, uint32_t i) {
  if (type_depth >= MAX_TYPE_DEPTH) return i;
  bool outer_extends = in_extends;
  in_extends = false;
  type_depth++;
  uint32_t end = conditional_type_end(p, i);
  type_depth--;
  in_extends = outer_extends;
  return end;
}

static uint32_t type_word_end(const parser_t *p, uint32_t i) {
  uint32_t next = i + 1;
  switch (p->t[i].word) {
  case SKIM_KW_TYPEOF:
    if (word_at(p, next) == SKIM_KW_IMPORT && kind_at(p, next + 1) == '(') next = after_pair(p, next + 1);
    else if (kind_at(p, next) == SKIM_T_WORD || kind_at(p, next) == SKIM_T_PRIVATE) next++;
    else return next;
    while (kind_at(p, next) == '.' && (kind_at(p, next + 1) == SKIM_T_WORD || kind_at(p, next + 1) == SKIM_T_PRIVATE))
      next += 2;
    return newline_at(p, next) ? next : type_arguments_after(p, next);
  case SKIM_KW_KEYOF:
  case SKIM_KW_UNIQUE:
  case SKIM_KW_READONLY: {
    uint32_t end = type_operand(p, next);
    if (end > next) return end;
    break;
  }
  case SKIM_KW_INFER:
    if (kind_at(p, next) != SKIM_T_WORD) break;
    next++;
    if (word_at(p, next) == SKIM_KW_EXTENDS) {
      bool required = in_extends;
      uint32_t end = type_union_end(p, next + 1);
      if (end > next + 1 && (required || kind_at(p, end) != '?')) return end;
    }
    return next;
  case SKIM_KW_ASSERTS:
    if (kind_at(p, next) != SKIM_T_WORD || newline_at(p, next)) break;
    next++;
    if (word_at(p, next) == SKIM_KW_IS) return skim_type_end(p, next + 1);
    return next;
  case SKIM_KW_ABSTRACT:
    if (word_at(p, next) != SKIM_KW_NEW) break;
    next++;
    [[fallthrough]];
  case SKIM_KW_NEW: {
    uint32_t params = type_arguments_after(p, next);
    if (kind_at(p, params) != '(') break;
    uint32_t end = function_type_end(p, params);
    if (end) return end;
    break;
  }
  case SKIM_KW_IMPORT:
    if (kind_at(p, next) != '(') break;
    return type_arguments_after(p, entity_name_end(p, after_pair(p, next)));
  default:
    break;
  }
  next = entity_name_end(p, i + 1);
  if (next == i + 1 && word_at(p, next) == SKIM_KW_IS && !newline_at(p, next)) return skim_type_end(p, next + 1);
  return type_arguments_after(p, next);
}

static uint32_t type_operand(const parser_t *p, uint32_t i) {
  if (type_depth >= MAX_TYPE_DEPTH) return i;
  type_depth++;
  uint32_t end = type_primary_end(p, i);
  type_depth--;
  return end;
}

static uint32_t type_primary_end(const parser_t *p, uint32_t i) {
  uint32_t end = i;
  switch (kind_at(p, i)) {
  case SKIM_T_WORD:
    end = type_word_end(p, i);
    break;
  case SKIM_T_STRING:
  case SKIM_T_NUMBER:
    end = i + 1;
    break;
  case SKIM_T_TEMPLATE:
    end = template_end(p, i);
    break;
  case '-':
    if (kind_at(p, i + 1) != SKIM_T_NUMBER) return i;
    end = i + 2;
    break;
  case '(': {
    uint32_t function_end = function_type_end(p, i);
    if (function_end) return function_end;
    end = after_pair(p, i);
    break;
  }
  case '<': {
    uint32_t params = skim_angle_end(p, i);
    if (!params || kind_at(p, params) != '(') return i;
    uint32_t function_end = function_type_end(p, params);
    return function_end ? function_end : i;
  }
  case '{':
  case '[':
    end = after_pair(p, i);
    break;
  default:
    return i;
  }
  while (kind_at(p, end) == '[' && !newline_at(p, end))
    end = after_pair(p, end);
  return end;
}

uint32_t skim_angle_end(const parser_t *p, uint32_t i) {
  if (kind_at(p, i) != '<') return 0;
  i++;
  for (;;) {
    while ((word_at(p, i) == SKIM_KW_CONST || word_at(p, i) == SKIM_KW_IN || word_at(p, i) == SKIM_KW_OUT) &&
           kind_at(p, i + 1) == SKIM_T_WORD)
      i++;
    uint32_t end = skim_type_end(p, i);
    if (end == i) return 0;
    i = end;
    if (word_at(p, i) == SKIM_KW_EXTENDS) {
      end = skim_type_end(p, i + 1);
      if (end == i + 1) return 0;
      i = end;
    }
    if (kind_at(p, i) == '=') {
      end = skim_type_end(p, i + 1);
      if (end == i + 1) return 0;
      i = end;
    }
    if (kind_at(p, i) == ',') {
      i++;
      if (kind_at(p, i) == '>') return i + 1;
      continue;
    }
    return kind_at(p, i) == '>' ? i + 1 : 0;
  }
}

static bool starts_expression(const parser_t *p, uint32_t i) {
  switch (kind_at(p, i)) {
  case SKIM_T_WORD:
    return !(skim_keyword_attrs[p->t[i].word] & SKIM_KW_INFIX) && p->t[i].word != SKIM_KW_AS &&
           p->t[i].word != SKIM_KW_SATISFIES;
  case SKIM_T_PRIVATE:
  case SKIM_T_NUMBER:
  case SKIM_T_STRING:
  case SKIM_T_TEMPLATE:
  case SKIM_T_REGEX:
  case SKIM_T_INCDEC:
  case '(':
  case '[':
  case '{':
  case '!':
  case '~':
  case '@':
    return true;
  default:
    return false;
  }
}

bool skim_type_arguments_end(const parser_t *p, uint32_t i, uint32_t *end) {
  uint32_t after = skim_angle_end(p, i);
  if (!after) return false;
  uint8_t next = kind_at(p, after);
  bool follows = next == '(' || next == SKIM_T_TEMPLATE;
  if (!follows && next != '<' && next != '>' && next != '+' && next != '-')
    follows = newline_at(p, after) || !starts_expression(p, after);
  if (follows) *end = after;
  return follows;
}
