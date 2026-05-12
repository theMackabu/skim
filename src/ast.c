#include "ast.h"
#include "syntax.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
  const char *src;
  const char *name;
  size_t name_len;
  size_t pos;
  unsigned flags;
  size_t next;
} skim_decl_t;

static skim_decl_t *decls;
static size_t decl_count;
static size_t decl_cap;

static skim_transform_state_t *decl_state(void) {
  return skim_state ? skim_state : &skim_default_state;
}

static size_t *decl_buckets(void) {
  skim_transform_state_t *state = decl_state();
  if (!state->decl_buckets) {
    state->decl_buckets = malloc(sizeof(*state->decl_buckets) * SKIM_BUCKET_COUNT);
    if (!state->decl_buckets) skim_die("out of memory");
  }
  return state->decl_buckets;
}

static void clear_decl_buckets(void) {
  size_t *buckets = decl_buckets();
  for (size_t i = 0; i < SKIM_BUCKET_COUNT; i++)
    buckets[i] = (size_t)-1;
}

static size_t decl_hash(const char *name, size_t len) {
  size_t h = 1469598103934665603ull;
  for (size_t i = 0; i < len; i++) {
    h ^= (unsigned char)name[i];
    h *= 1099511628211ull;
  }
  return h & (SKIM_BUCKET_COUNT - 1);
}

void skim_decl_reset(void) {
  free(decls);
  decls = NULL;
  decl_count = 0;
  decl_cap = 0;
  clear_decl_buckets();
}

void skim_decl_remember(const char *src, const char *name_start, const char *name_end, size_t pos, unsigned flags) {
  size_t name_len = (size_t)(name_end - name_start);
  if (name_len == 0 || flags == 0) return;
  if (decl_count == decl_cap) {
    size_t cap = decl_cap ? decl_cap * 2 : 128;
    skim_decl_t *next = realloc(decls, sizeof(*decls) * cap);
    if (!next) skim_die("out of memory");
    decls = next;
    decl_cap = cap;
  }
  size_t bucket = decl_hash(name_start, name_len);
  size_t *buckets = decl_buckets();
  decls[decl_count].src = src;
  decls[decl_count].name = name_start;
  decls[decl_count].name_len = name_len;
  decls[decl_count].pos = pos;
  decls[decl_count].flags = flags;
  decls[decl_count].next = buckets[bucket];
  buckets[bucket] = decl_count;
  decl_count++;
}

bool skim_decl_seen_before(const char *src, const char *name, size_t pos, unsigned flags) {
  size_t name_len = strlen(name);
  size_t *buckets = decl_buckets();
  for (size_t i = buckets[decl_hash(name, name_len)]; i != (size_t)-1; i = decls[i].next) {
    if (decls[i].src != src || decls[i].pos >= pos || !(decls[i].flags & flags)) continue;
    if (decls[i].name_len != name_len) continue;
    if (memcmp(decls[i].name, name, name_len) == 0) return true;
  }
  return false;
}

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
