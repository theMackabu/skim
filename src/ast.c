#include "ast.h"
#include "emit.h"
#include "str.h"
#include "syntax.h"

#include <stdlib.h>

void skim_ast_print_program(const skim_ast_program_t *program, skim_str_t *out) {
  for (size_t i = 0; i < program->item_count; i++) {
    const skim_ast_item_t *item = &program->items[i];
    if (item->kind == SKIM_AST_ITEM_INTERFACE || item->kind == SKIM_AST_ITEM_TYPE_ALIAS) {
      skim_emit_preserved_newlines(out, program->src, item->start, item->end);
      continue;
    }
    skim_transform_range(program->src, program->len, item->start, item->end, out);
    if (
      i + 1 < program->item_count && skim_syntax_item_needs_statement_semicolon(item, program->src, program->len, out)
    )
      skim_str_putc(out, ';');
  }
}

void skim_ast_free_program(skim_ast_program_t *program) {
  free(program->items);
  *program = (skim_ast_program_t){0};
}
