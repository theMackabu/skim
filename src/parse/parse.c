#include "parse_internal.h"
#include "str.h"

#include <stdlib.h>

static void *grow(void *items, uint32_t count, uint32_t *cap, size_t size) {
  if (count < *cap) return items;
  *cap = *cap ? *cap * 2 : 64;
  items = realloc(items, size * *cap);
  if (!items) skim_die("out of memory");
  return items;
}

uint32_t skim_node_open(parser_t *p, skim_node_kind_t kind, uint32_t first, uint8_t flags) {
  if (p->dry) return 0;
  skim_ast_t *ast = p->ast;
  ast->nodes = grow(ast->nodes, ast->node_count, &ast->node_cap, sizeof(*ast->nodes));
  ast->nodes[ast->node_count] = (skim_node_t){.first = first, .kind = kind, .flags = flags};
  p->t[first].flags |= SKIM_TF_NODE;
  return ast->node_count++;
}

skim_node_t *skim_node_close(parser_t *p, uint32_t node) {
  if (p->dry) return NULL;
  p->ast->nodes[node].end = p->i;
  return &p->ast->nodes[node];
}

uint32_t skim_link_push(parser_t *p, uint32_t token, uint32_t tail) {
  skim_ast_t *ast = p->ast;
  uint32_t link = ast->link_count ? ast->link_count : 2;
  ast->link_count = link;
  while (link + 2 > ast->link_cap)
    ast->links = grow(ast->links, ast->link_cap, &ast->link_cap, sizeof(*ast->links));
  ast->links[link] = token;
  ast->links[link + 1] = 0;
  if (tail) ast->links[tail + 1] = link;
  ast->link_count += 2;
  return link;
}

scope_mark_t skim_scope_push(parser_t *p, uint32_t first, bool function) {
  scope_mark_t mark = {p->block, p->function};
  if (p->dry) return mark;
  skim_ast_t *ast = p->ast;
  ast->scopes = grow(ast->scopes, ast->scope_count, &ast->scope_cap, sizeof(*ast->scopes));
  ast->scopes[ast->scope_count] = (skim_scope_t){.first = first, .end = p->count};
  p->block = ast->scope_count++;
  if (function) p->function = p->block;
  return mark;
}

void skim_scope_pop(parser_t *p, scope_mark_t mark) {
  if (p->dry) return;
  p->ast->scopes[p->block].end = p->i;
  p->block = mark.block;
  p->function = mark.function;
}

void skim_declare(parser_t *p, uint32_t token, skim_decl_kind_t kind) {
  if (p->dry) return;
  skim_ast_t *ast = p->ast;
  uint32_t scope = kind == SKIM_D_VAR || kind == SKIM_D_PARAM ? p->function : p->block;
  ast->decls = grow(ast->decls, ast->decl_count, &ast->decl_cap, sizeof(*ast->decls));
  ast->decls[ast->decl_count++] = (skim_decl_t){.token = token, .scope = scope, .kind = kind};
}

void skim_parse(skim_tokens_t *tokens, skim_ast_t *ast, const char *src) {
  parser_t p = {.t = tokens->items, .src = src, .ast = ast, .count = tokens->count};
  ast->node_count = 0;
  ast->link_count = 0;
  ast->scope_count = 0;
  ast->decl_count = 0;
  ast->module = false;
  skim_scope_push(&p, 0, true);
  skim_parse_statements(&p, p.count);
  ast->scopes[0].end = p.count;
}

void skim_ast_free(skim_ast_t *ast) {
  free(ast->nodes);
  free(ast->links);
  free(ast->scopes);
  free(ast->decls);
  *ast = (skim_ast_t){0};
}
