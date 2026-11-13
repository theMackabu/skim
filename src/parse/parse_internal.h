#ifndef SKIM_PARSE_INTERNAL_H
#define SKIM_PARSE_INTERNAL_H

#include "ast.h"

typedef struct {
  skim_token_t *t;
  const char *src;
  skim_ast_t *ast;
  uint32_t i;
  uint32_t count;
  uint32_t dry;
  uint32_t depth;
  bool constructor;
  bool runtime;
  uint32_t decorators;
  uint32_t block;
  uint32_t function;
} parser_t;

constexpr uint32_t SKIM_PARSE_MAX_DEPTH = 1200;

static inline uint8_t kind_at(const parser_t *p, uint32_t i) {
  return p->t[i].kind;
}

static inline uint8_t word_at(const parser_t *p, uint32_t i) {
  return p->t[i].kind == SKIM_T_WORD ? p->t[i].word : SKIM_KW_NONE;
}

static inline bool newline_at(const parser_t *p, uint32_t i) {
  return p->t[i].flags & SKIM_TF_NEWLINE;
}

static inline uint8_t kind(const parser_t *p) {
  return p->t[p->i].kind;
}

static inline uint8_t word(const parser_t *p) {
  return word_at(p, p->i);
}

static inline uint32_t after_pair(const parser_t *p, uint32_t open) {
  uint32_t close = p->t[open].pair;
  return close < p->count ? close + 1 : p->count;
}

static inline uint32_t close_of(const parser_t *p, uint32_t open) {
  uint32_t close = p->t[open].pair;
  return close < p->count ? close : p->count;
}

static inline void strip(parser_t *p, uint32_t from, uint32_t to) {
  if (p->dry) return;
  for (uint32_t i = from; i < to; i++)
    p->t[i].flags |= SKIM_TF_STRIP;
}

static inline void flag(parser_t *p, uint32_t i, uint16_t flags) {
  if (!p->dry) p->t[i].flags |= flags;
}

uint32_t skim_type_end(const parser_t *p, uint32_t i);
uint32_t skim_angle_end(const parser_t *p, uint32_t i);
bool skim_type_arguments_end(const parser_t *p, uint32_t i, uint32_t *end);

uint32_t skim_node_open(parser_t *p, skim_node_kind_t kind, uint32_t first, uint8_t flags);
skim_node_t *skim_node_close(parser_t *p, uint32_t node);
uint32_t skim_link_push(parser_t *p, uint32_t token, uint32_t tail);

typedef struct {
  uint32_t block;
  uint32_t function;
} scope_mark_t;

scope_mark_t skim_scope_push(parser_t *p, uint32_t first, bool function);
void skim_scope_pop(parser_t *p, scope_mark_t mark);
void skim_declare(parser_t *p, uint32_t token, skim_decl_kind_t kind);

void skim_parse_statement(parser_t *p);
void skim_parse_statements(parser_t *p, uint32_t end);
void skim_parse_block(parser_t *p);
void skim_parse_expression(parser_t *p);
void skim_parse_assignment(parser_t *p, bool consequent);
void skim_parse_list(parser_t *p, uint32_t open);
void skim_parse_binding(parser_t *p, skim_decl_kind_t kind);
void skim_parse_variables(parser_t *p);
void skim_parse_decorators(parser_t *p);
void skim_parse_parameters(parser_t *p, uint32_t *properties);
bool skim_parse_function_tail(parser_t *p);
void skim_parse_heritage(parser_t *p);
void skim_parse_decorator(parser_t *p);
void skim_parse_class(parser_t *p, uint8_t flags);
void skim_parse_enum(parser_t *p, uint32_t first, uint8_t flags);
void skim_parse_namespace(parser_t *p, uint32_t first, uint8_t flags);
void skim_parse_import(parser_t *p);
void skim_parse_export(parser_t *p);

#endif
