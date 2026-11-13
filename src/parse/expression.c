#include "parse_internal.h"

#include <string.h>

static void leave(parser_t *p, uint32_t open) {
  uint32_t after = after_pair(p, open);
  if (p->i < after) p->i = after;
}

static bool arrow_follows(const parser_t *p, uint32_t open, uint32_t *arrow) {
  uint32_t after = after_pair(p, open);
  *arrow = after;
  if (kind_at(p, after) == SKIM_T_ARROW) return true;
  if (kind_at(p, after) != ':') return false;
  uint32_t end = skim_type_end(p, after + 1);
  *arrow = end;
  return end > after + 1 && kind_at(p, end) == SKIM_T_ARROW;
}

static void arrow_body(parser_t *p) {
  if (kind(p) == '{') skim_parse_block(p);
  else skim_parse_assignment(p, false);
}

static void arrow_function(parser_t *p, uint32_t open, uint32_t arrow) {
  uint32_t after = after_pair(p, open);
  scope_mark_t scope = skim_scope_push(p, open, true);
  p->i = open;
  skim_parse_parameters(p, NULL);
  if (arrow > after) {
    strip(p, after, arrow);
    flag(p, arrow, SKIM_TF_JOIN);
  }
  p->i = arrow + 1;
  arrow_body(p);
  skim_scope_pop(p, scope);
}

static void simple_arrow_function(parser_t *p, uint32_t param) {
  scope_mark_t scope = skim_scope_push(p, param, true);
  skim_declare(p, param, SKIM_D_PARAM);
  p->i = param + 2;
  arrow_body(p);
  skim_scope_pop(p, scope);
}

static bool arrow_accepted(parser_t *p, uint32_t open, uint32_t arrow, bool consequent) {
  if (!consequent || kind_at(p, after_pair(p, open)) != ':') return true;
  uint32_t saved = p->i;
  p->dry++;
  arrow_function(p, open, arrow);
  bool accepted = kind(p) == ':';
  p->dry--;
  p->i = saved;
  return accepted;
}

static void template_literal(parser_t *p) {
  uint32_t piece = p->i;
  while (!(p->t[piece].flags & SKIM_TF_TAIL)) {
    uint32_t next = p->t[piece].pair;
    if (next <= piece || next >= p->count) {
      p->i = p->count;
      return;
    }
    p->i = piece + 1;
    skim_parse_expression(p);
    piece = next;
  }
  if (p->i < piece + 1) p->i = piece + 1;
}

void skim_parse_list(parser_t *p, uint32_t open) {
  uint32_t close = close_of(p, open);
  p->i = open + 1;
  while (p->i < close) {
    if (kind(p) == ',') {
      if (p->i + 1 == close && kind_at(p, close) == ')') strip(p, p->i, p->i + 1);
      p->i++;
      continue;
    }
    uint32_t before = p->i;
    skim_parse_assignment(p, false);
    if (p->i == before) p->i++;
  }
  leave(p, open);
}

static bool member_modifier_applies(const parser_t *p) {
  switch (kind_at(p, p->i + 1)) {
  case SKIM_T_WORD:
  case SKIM_T_STRING:
  case SKIM_T_NUMBER:
  case SKIM_T_PRIVATE:
  case '[':
  case '*':
    return true;
  default:
    return false;
  }
}

static bool redundant_property_value(const parser_t *p, uint32_t key) {
  uint32_t value = p->i + 1;
  if (key + 1 != p->i || kind_at(p, key) != SKIM_T_WORD || kind_at(p, value) != SKIM_T_WORD) return false;
  if (kind_at(p, value + 1) != ',' && kind_at(p, value + 1) != '}') return false;
  uint32_t n = p->t[key].end - p->t[key].start;
  return n == p->t[value].end - p->t[value].start &&
         memcmp(p->src + p->t[key].start, p->src + p->t[value].start, n) == 0;
}

static void object_literal(parser_t *p) {
  uint32_t open = p->i;
  uint32_t close = close_of(p, open);
  p->i++;
  while (p->i < close) {
    uint32_t before = p->i;
    if (kind(p) == ',') {
      p->i++;
      continue;
    }
    if (kind(p) == SKIM_T_SPREAD) {
      p->i++;
      skim_parse_assignment(p, false);
      continue;
    }
    while ((word(p) == SKIM_KW_ASYNC || word(p) == SKIM_KW_GET || word(p) == SKIM_KW_SET) && member_modifier_applies(p))
      p->i++;
    if (kind(p) == '*') p->i++;
    uint32_t key = p->i;
    if (kind(p) == '[') skim_parse_list(p, p->i);
    else p->i++;
    switch (kind(p)) {
    case '(':
    case '<':
      skim_parse_function_tail(p);
      break;
    case ':':
      if (redundant_property_value(p, key)) {
        flag(p, key, SKIM_TF_REF);
        strip(p, p->i, p->i + 2);
        p->i += 2;
        break;
      }
      p->i++;
      skim_parse_assignment(p, false);
      break;
    case '=':
      flag(p, key, SKIM_TF_REF);
      p->i++;
      skim_parse_assignment(p, false);
      break;
    default:
      if (kind_at(p, key) == SKIM_T_WORD) flag(p, key, SKIM_TF_REF);
      break;
    }
    if (p->i == before) p->i++;
  }
  leave(p, open);
}

void skim_parse_binding(parser_t *p, skim_decl_kind_t decl) {
  if (kind(p) == SKIM_T_WORD) {
    skim_declare(p, p->i++, decl);
    return;
  }
  if (kind(p) != '{' && kind(p) != '[') return;
  uint32_t open = p->i;
  uint32_t close = close_of(p, open);
  bool object = kind(p) == '{';
  p->i++;
  while (p->i < close) {
    uint32_t before = p->i;
    if (kind(p) == ',') {
      p->i++;
      continue;
    }
    if (kind(p) == SKIM_T_SPREAD) p->i++;
    if (object) {
      uint32_t key = p->i;
      if (kind(p) == '[') skim_parse_list(p, p->i);
      else p->i++;
      if (kind(p) == ':' && redundant_property_value(p, key)) {
        skim_declare(p, key, decl);
        strip(p, p->i, p->i + 2);
        p->i += 2;
      } else if (kind(p) == ':') {
        p->i++;
        skim_parse_binding(p, decl);
      } else if (kind_at(p, key) == SKIM_T_WORD) {
        skim_declare(p, key, decl);
      }
    } else {
      skim_parse_binding(p, decl);
    }
    if (kind(p) == '=') {
      p->i++;
      skim_parse_assignment(p, false);
    }
    if (p->i == before) p->i++;
  }
  leave(p, open);
}

typedef enum {
  OPERAND_NONE,
  OPERAND_VALUE,
  OPERAND_FINAL,
} operand_t;

static bool literal_word(uint8_t word) {
  switch (word) {
  case SKIM_KW_THIS:
  case SKIM_KW_SUPER:
  case SKIM_KW_NULL:
  case SKIM_KW_TRUE:
  case SKIM_KW_FALSE:
  case SKIM_KW_IMPORT:
    return true;
  default:
    return false;
  }
}

static operand_t async_operand(parser_t *p, bool consequent) {
  uint32_t next = p->i + 1;
  uint32_t arrow = 0;
  if (newline_at(p, next)) return OPERAND_NONE;
  if (word_at(p, next) == SKIM_KW_FUNCTION) {
    p->i = next + 1;
    if (kind(p) == '*') p->i++;
    if (kind(p) == SKIM_T_WORD) p->i++;
    skim_parse_function_tail(p);
    return OPERAND_VALUE;
  }
  if (kind_at(p, next) == SKIM_T_WORD && kind_at(p, next + 1) == SKIM_T_ARROW) {
    simple_arrow_function(p, next);
    return OPERAND_FINAL;
  }
  uint32_t open = next;
  if (kind_at(p, next) == '<') {
    open = skim_angle_end(p, next);
    if (!open) return OPERAND_NONE;
  }
  if (kind_at(p, open) != '(' || !arrow_follows(p, open, &arrow) || !arrow_accepted(p, open, arrow, consequent))
    return OPERAND_NONE;
  strip(p, next, open);
  arrow_function(p, open, arrow);
  return OPERAND_FINAL;
}

static operand_t word_operand(parser_t *p, bool consequent, bool *prefix) {
  uint8_t w = p->t[p->i].word;
  uint32_t next = p->i + 1;
  switch (w) {
  case SKIM_KW_FUNCTION:
    p->i++;
    if (kind(p) == '*') p->i++;
    if (kind(p) == SKIM_T_WORD) p->i++;
    skim_parse_function_tail(p);
    return OPERAND_VALUE;
  case SKIM_KW_CLASS:
    skim_parse_class(p, SKIM_NF_EXPRESSION);
    return OPERAND_VALUE;
  case SKIM_KW_ASYNC: {
    operand_t result = async_operand(p, consequent);
    if (result != OPERAND_NONE) return result;
    break;
  }
  case SKIM_KW_NEW:
    if (kind_at(p, next) == '.') break;
    [[fallthrough]];
  case SKIM_KW_TYPEOF:
  case SKIM_KW_VOID:
  case SKIM_KW_DELETE:
  case SKIM_KW_AWAIT:
  case SKIM_KW_YIELD:
    p->i++;
    *prefix = true;
    return OPERAND_NONE;
  default:
    break;
  }
  if (kind_at(p, next) == SKIM_T_ARROW) {
    simple_arrow_function(p, p->i);
    return OPERAND_FINAL;
  }
  if (!literal_word(w)) flag(p, p->i, SKIM_TF_REF);
  p->i++;
  return OPERAND_VALUE;
}

static operand_t paren_operand(parser_t *p, bool consequent) {
  uint32_t open = p->i;
  uint32_t arrow = 0;
  if (arrow_follows(p, open, &arrow) && arrow_accepted(p, open, arrow, consequent)) {
    arrow_function(p, open, arrow);
    return OPERAND_FINAL;
  }
  p->i++;
  skim_parse_expression(p);
  leave(p, open);
  return OPERAND_VALUE;
}

static operand_t angle_operand(parser_t *p, bool consequent, bool *prefix) {
  uint32_t start = p->i;
  uint32_t end = skim_angle_end(p, start);
  uint32_t arrow = 0;
  *prefix = true;
  if (!end) {
    p->i++;
    return OPERAND_NONE;
  }
  strip(p, start, end);
  p->i = end;
  if (kind_at(p, end) != '(' || !arrow_follows(p, end, &arrow) || !arrow_accepted(p, end, arrow, consequent))
    return OPERAND_NONE;
  arrow_function(p, end, arrow);
  return OPERAND_FINAL;
}

static operand_t operand(parser_t *p, bool consequent) {
  for (;;) {
    bool prefix = false;
    operand_t result = OPERAND_NONE;
    switch (kind(p)) {
    case SKIM_T_WORD:
      result = word_operand(p, consequent, &prefix);
      break;
    case SKIM_T_NUMBER:
    case SKIM_T_STRING:
    case SKIM_T_REGEX:
    case SKIM_T_PRIVATE:
      p->i++;
      return OPERAND_VALUE;
    case SKIM_T_TEMPLATE:
      template_literal(p);
      return OPERAND_VALUE;
    case '(':
      return paren_operand(p, consequent);
    case '[':
      skim_parse_list(p, p->i);
      return OPERAND_VALUE;
    case '{':
      object_literal(p);
      return OPERAND_VALUE;
    case '<':
      result = angle_operand(p, consequent, &prefix);
      break;
    case '@':
      skim_parse_decorators(p);
      prefix = true;
      break;
    case SKIM_T_OP:
    case SKIM_T_INCDEC:
    case SKIM_T_SPREAD:
    case '!':
    case '~':
    case '+':
    case '-':
    case '*':
    case '/':
    case '&':
    case '|':
    case '=':
    case '>':
      p->i++;
      prefix = true;
      break;
    default:
      return OPERAND_NONE;
    }
    if (result != OPERAND_NONE || !prefix) return result;
  }
}

static bool word_operator(parser_t *p, bool *binary) {
  uint8_t w = p->t[p->i].word;
  if (skim_keyword_attrs[w] & SKIM_KW_INFIX) {
    p->i++;
    *binary = true;
    return true;
  }
  if ((w != SKIM_KW_AS && w != SKIM_KW_SATISFIES) || newline_at(p, p->i)) return false;
  uint32_t end = skim_type_end(p, p->i + 1);
  strip(p, p->i, end);
  p->i = end;
  return true;
}

typedef enum {
  CHAIN_FULL,
  CHAIN_HERITAGE,
  CHAIN_DECORATOR,
} chain_t;

static bool operators(parser_t *p, chain_t chain) {
  bool heritage = chain != CHAIN_FULL;
  bool updated = false;
  for (;;) {
    uint32_t end = 0;
    bool binary = false;
    uint8_t k = kind(p);
    if (updated && (k == '.' || k == SKIM_T_CHAIN || k == '[' || k == '(' || k == SKIM_T_TEMPLATE)) return false;
    switch (k) {
    case '.':
    case SKIM_T_CHAIN:
      p->i++;
      if (kind(p) == SKIM_T_WORD || kind(p) == SKIM_T_PRIVATE) p->i++;
      continue;
    case '[':
      if (chain == CHAIN_DECORATOR) return false;
      [[fallthrough]];
    case '(':
      skim_parse_list(p, p->i);
      continue;
    case SKIM_T_TEMPLATE:
      if (chain == CHAIN_DECORATOR) return false;
      template_literal(p);
      continue;
    case '!':
      if (newline_at(p, p->i)) return false;
      strip(p, p->i, p->i + 1);
      p->i++;
      continue;
    case SKIM_T_INCDEC:
      if (newline_at(p, p->i) || heritage) return false;
      p->i++;
      updated = true;
      continue;
    case '<':
      if (heritage ? (end = skim_angle_end(p, p->i)) != 0 : skim_type_arguments_end(p, p->i, &end)) {
        strip(p, p->i, end);
        p->i = end;
        continue;
      }
      if (heritage) return false;
      p->i++;
      return true;
    case '?':
      if (heritage) return false;
      p->i++;
      skim_parse_assignment(p, true);
      if (kind(p) == ':') p->i++;
      return true;
    case SKIM_T_OP:
    case '=':
    case '+':
    case '-':
    case '*':
    case '/':
    case '&':
    case '|':
    case '>':
      if (heritage) return false;
      p->i++;
      return true;
    case SKIM_T_WORD:
      if (heritage || !word_operator(p, &binary)) return false;
      if (binary) return true;
      continue;
    default:
      return false;
    }
  }
}

void skim_parse_assignment(parser_t *p, bool consequent) {
  if (++p->depth > SKIM_PARSE_MAX_DEPTH) {
    p->i = p->count;
    p->depth--;
    return;
  }
  while (operand(p, consequent) == OPERAND_VALUE && operators(p, CHAIN_FULL)) {}
  p->depth--;
}

void skim_parse_heritage(parser_t *p) {
  if (operand(p, false) == OPERAND_VALUE) operators(p, CHAIN_HERITAGE);
}

void skim_parse_decorator(parser_t *p) {
  if (kind(p) == '(') {
    skim_parse_list(p, p->i);
    return;
  }
  if (kind(p) != SKIM_T_WORD) return;
  flag(p, p->i, SKIM_TF_REF);
  p->i++;
  operators(p, CHAIN_DECORATOR);
}

void skim_parse_expression(parser_t *p) {
  for (;;) {
    skim_parse_assignment(p, false);
    if (kind(p) != ',') return;
    p->i++;
  }
}
