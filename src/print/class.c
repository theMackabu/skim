#include "print_internal.h"

static const skim_node_t *
direct_child(const skim_printer_t *pr, const skim_node_t *owner, uint32_t *cursor, uint8_t kind) {
  while (*cursor < pr->ast->node_count && pr->ast->nodes[*cursor].first < owner->end) {
    const skim_node_t *node = &pr->ast->nodes[(*cursor)++];
    if (node->kind == kind) return node;
    if (node->kind != SKIM_N_CLASS) continue;
    while (*cursor < pr->ast->node_count && pr->ast->nodes[*cursor].first < node->end)
      (*cursor)++;
  }
  return NULL;
}

static bool has_field(const skim_printer_t *pr, const skim_node_t *owner, uint32_t first_child, uint32_t name) {
  uint32_t cursor = first_child;
  for (const skim_node_t *field; (field = direct_child(pr, owner, &cursor, SKIM_N_FIELD));)
    if (
      !(field->flags & SKIM_NF_STATIC) && !(pr->t[field->first].flags & SKIM_TF_STRIP) &&
      token_equal(pr, field->field.key, name)
    )
      return true;
  return false;
}

uint32_t skim_print_class(skim_printer_t *pr, const skim_node_t *node) {
  uint32_t first_child = pr->cursor;
  uint32_t cursor = first_child;
  const skim_node_t *constructor = direct_child(pr, node, &cursor, SKIM_N_CONSTRUCTOR);
  uint32_t body = node->decl.body;
  if (!constructor || pr->t[body].kind != '{') return node->first;

  skim_print_range(pr, node->first, body + 1);
  skim_print_flush(pr);
  for (uint32_t entry = constructor->constructor.properties; entry; entry = pr->ast->links[entry + 1]) {
    uint32_t name = pr->ast->links[entry];
    if (has_field(pr, node, first_child, name)) continue;
    skim_print_line(pr, body + 1);
    put_token(pr, name);
    put(pr, ";");
  }
  return body + 1;
}

static uint32_t directives_end(const skim_printer_t *pr, uint32_t i, uint32_t close) {
  while (i < close && pr->t[i].kind == SKIM_T_STRING) {
    uint8_t next = pr->t[i + 1].kind;
    if (next == ';') i += 2;
    else if (i + 1 == close || pr->t[i + 1].flags & SKIM_TF_NEWLINE) i++;
    else break;
  }
  return i;
}

uint32_t skim_print_constructor(skim_printer_t *pr, const skim_node_t *node) {
  uint32_t close = token_close(pr, node->first);
  uint32_t super_end = node->constructor.super_end;
  uint32_t insert = super_end ? super_end : directives_end(pr, node->first + 1, close);
  skim_print_range(pr, node->first, insert);
  skim_print_flush(pr);
  if (super_end && pr->t[super_end - 1].kind != ';') put(pr, ";");
  for (uint32_t entry = node->constructor.properties; entry; entry = pr->ast->links[entry + 1]) {
    skim_print_line(pr, super_end || insert == close ? node->first + 1 : insert);
    put(pr, "this.");
    put_token(pr, pr->ast->links[entry]);
    put(pr, " = ");
    put_token(pr, pr->ast->links[entry]);
    put(pr, ";");
  }
  return insert;
}

static bool field_needs_semicolon(const skim_printer_t *pr, const skim_node_t *node) {
  if (node->field.value || pr->t[node->end - 1].kind == ';') return false;
  switch (token_word(pr, node->field.key)) {
  case SKIM_KW_GET:
  case SKIM_KW_SET:
  case SKIM_KW_STATIC:
  case SKIM_KW_ASYNC:
  case SKIM_KW_ACCESSOR:
    return true;
  default:
    break;
  }
  uint8_t next = pr->t[node->end].kind;
  return next == '[' || next == '(' || next == '*';
}

uint32_t skim_print_field(skim_printer_t *pr, const skim_node_t *node) {
  const skim_options_t *options = pr->options;
  bool initialized = node->field.value || pr->t[node->field.key].kind == SKIM_T_PRIVATE;
  bool removable = options->remove_class_fields_without_initializer || options->allow_declare_fields_false;
  if (removable && (!initialized || node->flags & SKIM_NF_ACCESSOR)) {
    skim_print_remove(pr, node->first, node->end);
    return node->first;
  }
  if (!field_needs_semicolon(pr, node)) return node->first;
  skim_print_range(pr, node->first, node->end);
  skim_print_flush(pr);
  put(pr, ";");
  return node->end;
}
