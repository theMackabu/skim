#ifndef SKIM_AST_H
#define SKIM_AST_H

#include "skim_internal.h"

typedef enum {
  SKIM_AST_ITEM_TEXT,
  SKIM_AST_ITEM_IMPORT,
  SKIM_AST_ITEM_EXPORT,
  SKIM_AST_ITEM_TYPE_ALIAS,
  SKIM_AST_ITEM_INTERFACE,
  SKIM_AST_ITEM_DECLARE,
  SKIM_AST_ITEM_ENUM,
  SKIM_AST_ITEM_NAMESPACE,
  SKIM_AST_ITEM_CLASS,
} skim_ast_item_kind_t;

typedef struct {
  skim_ast_item_kind_t kind;
  size_t start;
  size_t content_start;
  size_t end;
} skim_ast_item_t;

typedef struct {
  const char *src;
  size_t len;
  skim_ast_item_t *items;
  size_t item_count;
  size_t item_cap;
} skim_ast_program_t;

void skim_parse_program(skim_ast_program_t *program, const char *src, size_t len);
void skim_ast_print_program(const skim_ast_program_t *program, skim_str_t *out);
void skim_ast_free_program(skim_ast_program_t *program);

#endif
