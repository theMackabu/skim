#include "chars.h"
#include "print_internal.h"

#include <stdio.h>

constexpr uint32_t MAX_PARTS = 32;

static bool param_used(const skim_printer_t *pr, size_t candidate) {
  const skim_str_t *params = &pr->params;
  const char *name = params->data + candidate;
  size_t len = params->len - candidate;
  for (size_t at = 0; at < candidate; at += strlen(params->data + at) + 1)
    if (strcmp(params->data + at, name) == 0) return true;
  for (uint32_t i = 0; i < pr->count; i++) {
    const skim_token_t *token = &pr->t[i];
    if (
      token->kind == SKIM_T_WORD && token->end - token->start == len && memcmp(pr->src + token->start, name, len) == 0
    )
      return true;
  }
  return false;
}

static uint32_t make_param(skim_printer_t *pr, uint32_t name) {
  const char *text = token_text(pr, name);
  uint32_t base = token_len(pr, name);
  while (base > 1 && skim_is_digit(text[base - 1]))
    base--;
  while (base > 1 && *text == '_') {
    text++;
    base--;
  }
  skim_str_t *params = &pr->params;
  size_t start = params->len;
  for (unsigned suffix = 1;; suffix++) {
    char digits[16];
    params->len = start;
    skim_str_putc(params, '_');
    skim_str_putn(params, text, base);
    snprintf(digits, sizeof(digits), "%u", suffix);
    if (suffix > 1) skim_str_puts(params, digits);
    if (!param_used(pr, start)) break;
  }
  params->len++;
  return (uint32_t)start + 1;
}

static const char *param_text(const skim_printer_t *pr, uint32_t param) {
  return pr->params.data + param - 1;
}

void skim_print_alias(skim_printer_t *pr, uint32_t name) {
  put(pr, param_text(pr, pr->param));
  put(pr, ".");
  put_token(pr, name);
  put(pr, " = ");
  put_token(pr, name);
}

static void close_part(skim_printer_t *pr, uint32_t name, uint32_t parent) {
  put(pr, "})(");
  put_token(pr, name);
  put(pr, " || (");
  put_token(pr, name);
  put(pr, " = ");
  if (parent) {
    put(pr, param_text(pr, parent));
    put(pr, ".");
    put_token(pr, name);
    put(pr, " || (");
    put(pr, param_text(pr, parent));
    put(pr, ".");
    put_token(pr, name);
    put(pr, " = {})");
  } else {
    put(pr, "{}");
  }
  put(pr, "));");
}

uint32_t skim_print_namespace(skim_printer_t *pr, const skim_node_t *node) {
  bool exported = pr->exported;
  uint32_t outer = pr->param;
  uint32_t params[MAX_PARTS];
  uint32_t names[MAX_PARTS];
  uint32_t parts = 0;
  uint32_t open = node->decl.body;
  uint32_t close = token_close(pr, open);
  uint32_t anchor = exported && node->first > 0 ? node->first - 1 : node->first;
  pr->exported = false;

  skim_print_begin(pr, node->first);
  for (uint32_t name = node->decl.name; name < open && parts < MAX_PARTS; name += 2) {
    names[parts] = name;
    params[parts] = make_param(pr, name);
    if (parts > 0 || !skim_print_declared(pr, name, name, false)) {
      if (parts == 0 && exported && !outer) put(pr, "export ");
      put(pr, "let ");
      put_token(pr, name);
      put(pr, ";");
      skim_print_line(pr, anchor);
    }
    skim_print_separate(pr);
    put(pr, "(function (");
    put(pr, param_text(pr, params[parts]));
    put(pr, ") {");
    if (name + 2 < open) skim_print_line(pr, anchor);
    parts++;
  }

  pr->param = params[parts - 1];
  skim_print_resume(pr, open + 1);
  skim_print_range(pr, open + 1, close);
  skim_print_flush(pr);
  skim_print_separate(pr);
  skim_print_begin(pr, close);
  pr->param = outer;

  for (uint32_t k = parts; k > 0; k--) {
    close_part(pr, names[k - 1], k > 1 ? params[k - 2] : exported ? outer : 0);
    if (k > 1) skim_print_line(pr, anchor);
  }
  skim_print_resume(pr, node->end);
  return node->end;
}

static uint32_t declarator_end(const skim_printer_t *pr, uint32_t from, uint32_t end) {
  while (from < end) {
    const skim_token_t *token = &pr->t[from];
    bool template = token->kind == SKIM_T_TEMPLATE && !(token->flags & SKIM_TF_TAIL) && token->pair > from;
    if (token->flags & SKIM_TF_STRIP) from++;
    else if (token->kind == ',' || token->kind == ';') return from;
    else if (token->kind == '(' || token->kind == '[' || token->kind == '{') from = token_close(pr, from) + 1;
    else from = template ? token->pair : from + 1;
  }
  return end;
}

static uint32_t initializer(const skim_printer_t *pr, uint32_t name, uint32_t end) {
  uint32_t equals = name + 1;
  while (equals < end && pr->t[equals].flags & SKIM_TF_STRIP)
    equals++;
  return equals < end && pr->t[equals].kind == '=' ? equals : 0;
}

static void print_bound_names(skim_printer_t *pr, uint32_t from, uint32_t end) {
  uint32_t scope = UINT32_MAX;
  for (uint32_t d = 0; d < pr->ast->decl_count; d++) {
    const skim_decl_t *decl = &pr->ast->decls[d];
    if (decl->token < from || decl->token >= end) continue;
    if (scope == UINT32_MAX) scope = decl->scope;
    else if (decl->scope == scope) put(pr, ", ");
    if (decl->scope == scope) skim_print_alias(pr, decl->token);
  }
  if (scope != UINT32_MAX) put(pr, ";");
}

static uint32_t print_variables(skim_printer_t *pr, const skim_node_t *node) {
  uint32_t keyword = node->exported.body;
  uint32_t end = node->end;
  bool simple = true;
  for (uint32_t i = keyword + 1; i < end; i = declarator_end(pr, i, end) + 1)
    simple = simple && pr->t[i].kind == SKIM_T_WORD;

  if (!simple) {
    skim_print_range(pr, keyword, end);
    skim_print_flush(pr);
    if (pr->t[end - 1].kind != ';') put(pr, ";");
    skim_print_line(pr, node->first);
    print_bound_names(pr, keyword + 1, end);
    return end;
  }

  uint32_t printed = keyword;
  for (uint32_t name = keyword + 1; name < end; name = declarator_end(pr, name, end) + 1) {
    uint32_t equals = initializer(pr, name, declarator_end(pr, name, end));
    if (!equals) continue;
    skim_print_range(pr, printed, equals + 1);
    skim_print_flush(pr);
    put(pr, " ");
    put(pr, param_text(pr, pr->param));
    put(pr, ".");
    put_token(pr, name);
    put(pr, " =");
    printed = equals + 1;
  }
  skim_print_range(pr, printed, end);
  return end;
}

static uint32_t declared_name(const skim_printer_t *pr, uint32_t body) {
  uint32_t name = 0;
  if (pr->cursor < pr->ast->node_count && pr->ast->nodes[pr->cursor].first == body) {
    const skim_node_t *child = &pr->ast->nodes[pr->cursor];
    if (child->kind == SKIM_N_CLASS) return child->decl.name;
  }
  if (token_word(pr, body) == SKIM_KW_ASYNC) name = body + 2;
  else if (token_word(pr, body) == SKIM_KW_FUNCTION) name = body + 1;
  if (name && pr->t[name].kind == '*') name++;
  return name && pr->t[name].kind == SKIM_T_WORD ? name : 0;
}

static bool exports_itself(const skim_printer_t *pr, uint32_t body) {
  if (pr->cursor == pr->ast->node_count || pr->ast->nodes[pr->cursor].first != body) return false;
  switch (pr->ast->nodes[pr->cursor].kind) {
  case SKIM_N_ENUM:
  case SKIM_N_NAMESPACE:
  case SKIM_N_IMPORT_EQUALS:
    return true;
  default:
    return false;
  }
}

uint32_t skim_print_namespace_export(skim_printer_t *pr, const skim_node_t *node) {
  uint32_t body = node->exported.body;
  uint32_t keyword = node->exported.keyword;
  if (node->exported.form == SKIM_EXPORT_ASSIGNMENT) {
    skim_print_begin(pr, node->first);
    put(pr, "module.exports");
    skim_print_resume(pr, keyword + 1);
    return keyword + 1;
  }
  if (node->exported.form != SKIM_EXPORT_DECLARATION) return node->first;

  skim_print_begin(pr, node->first);
  if (keyword != node->first) {
    skim_print_range(pr, node->first, keyword);
    skim_print_flush(pr);
    put(pr, " ");
  }
  if (token_word(pr, body) == SKIM_KW_ABSTRACT) body++;
  pr->copied = pr->kept = pr->t[body].start;
  if (exports_itself(pr, body)) {
    pr->exported = true;
    return body;
  }
  switch (token_word(pr, body)) {
  case SKIM_KW_CONST:
  case SKIM_KW_LET:
  case SKIM_KW_VAR:
    return print_variables(pr, node);
  default:
    break;
  }
  uint32_t name = declared_name(pr, body);
  if (!name) return body;
  skim_print_range(pr, body, node->end);
  skim_print_flush(pr);
  skim_print_line(pr, node->first);
  skim_print_alias(pr, name);
  put(pr, ";");
  return node->end;
}
