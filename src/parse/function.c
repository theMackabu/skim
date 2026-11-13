#include "parse_internal.h"

static bool parameter_modifier(const parser_t *p) {
  switch (word(p)) {
  case SKIM_KW_PUBLIC:
  case SKIM_KW_PRIVATE:
  case SKIM_KW_PROTECTED:
  case SKIM_KW_READONLY:
  case SKIM_KW_OVERRIDE:
    break;
  default:
    return false;
  }
  uint8_t next = kind_at(p, p->i + 1);
  return next == SKIM_T_WORD || next == '{' || next == '[';
}

static void strip_type_annotation(parser_t *p) {
  if (kind(p) != ':') return;
  uint32_t end = skim_type_end(p, p->i + 1);
  strip(p, p->i, end);
  p->i = end;
}

void skim_parse_parameters(parser_t *p, uint32_t *properties) {
  uint32_t open = p->i;
  uint32_t close = close_of(p, open);
  uint32_t tail = 0;
  p->i++;
  while (p->i < close) {
    uint32_t before = p->i;
    if (kind(p) == ',') {
      if (p->i + 1 == close) strip(p, p->i, p->i + 1);
      p->i++;
      continue;
    }
    skim_parse_decorators(p);
    bool property = false;
    while (parameter_modifier(p)) {
      strip(p, p->i, p->i + 1);
      property = true;
      p->i++;
    }
    if (word(p) == SKIM_KW_THIS) {
      uint32_t end = kind_at(p, p->i + 1) == ':' ? skim_type_end(p, p->i + 2) : p->i + 1;
      if (kind_at(p, end) == ',') end++;
      strip(p, p->i, end);
      p->i = end;
      continue;
    }
    if (kind(p) == SKIM_T_SPREAD) p->i++;
    if (property && properties && kind(p) == SKIM_T_WORD && !p->dry) {
      tail = skim_link_push(p, p->i, tail);
      if (!*properties) *properties = tail;
    }
    skim_parse_binding(p, SKIM_D_PARAM);
    if (kind(p) == '?') {
      strip(p, p->i, p->i + 1);
      p->i++;
    }
    strip_type_annotation(p);
    if (kind(p) == '=') {
      p->i++;
      skim_parse_assignment(p, false);
    }
    if (p->i == before) p->i++;
  }
  uint32_t after = after_pair(p, open);
  if (p->i < after) p->i = after;
}

static void constructor_body(parser_t *p, uint32_t properties) {
  uint32_t open = p->i;
  uint32_t close = close_of(p, open);
  uint32_t node = skim_node_open(p, SKIM_N_CONSTRUCTOR, open, 0);
  uint32_t super_end = 0;
  p->i++;
  while (p->i < close) {
    uint32_t before = p->i;
    bool super_call = !super_end && word(p) == SKIM_KW_SUPER && kind_at(p, p->i + 1) == '(';
    skim_parse_statement(p);
    if (super_call) super_end = p->i;
    if (p->i == before) p->i++;
  }
  uint32_t after = after_pair(p, open);
  if (p->i < after) p->i = after;
  skim_node_t *constructor = skim_node_close(p, node);
  if (!constructor) return;
  constructor->constructor.properties = properties;
  constructor->constructor.super_end = super_end;
}

bool skim_parse_function_tail(parser_t *p) {
  bool constructor = p->constructor;
  uint32_t properties = 0;
  p->constructor = false;
  if (kind(p) == '<') {
    uint32_t end = skim_angle_end(p, p->i);
    if (end) {
      strip(p, p->i, end);
      p->i = end;
    }
  }
  if (kind(p) != '(') return false;
  scope_mark_t scope = skim_scope_push(p, p->i, true);
  skim_parse_parameters(p, constructor ? &properties : NULL);
  strip_type_annotation(p);
  bool body = kind(p) == '{';
  if (body && properties) constructor_body(p, properties);
  else if (body) skim_parse_block(p);
  skim_scope_pop(p, scope);
  return body;
}

void skim_parse_decorators(parser_t *p) {
  while (kind(p) == '@') {
    p->i++;
    skim_parse_decorator(p);
  }
}
