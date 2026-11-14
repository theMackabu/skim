#include "print_internal.h"
#include "str.h"

#include <stdlib.h>

typedef struct {
  uint32_t *slots;
  uint32_t mask;
  uint32_t *shadows;
  uint32_t shadow_count;
} import_table_t;

static uint32_t name_hash(const char *text, uint32_t n) {
  uint32_t hash = 2166136261u;
  for (uint32_t i = 0; i < n; i++)
    hash = (hash ^ (unsigned char)text[i]) * 16777619u;
  return hash;
}

static bool table_has(const skim_printer_t *pr, const import_table_t *table, uint32_t token) {
  for (uint32_t slot = name_hash(token_text(pr, token), token_len(pr, token)) & table->mask;;
       slot = (slot + 1) & table->mask) {
    uint32_t entry = table->slots[slot];
    if (!entry) return false;
    if (token_equal(pr, pr->ast->decls[entry - 1].token, token)) return true;
  }
}

static bool scope_contains(const skim_printer_t *pr, uint32_t scope, uint32_t token) {
  return token >= pr->ast->scopes[scope].first && token < pr->ast->scopes[scope].end;
}

static void table_use(skim_printer_t *pr, const import_table_t *table, uint32_t token, bool scoped_alias) {
  skim_decl_t *best = NULL;
  for (uint32_t slot = name_hash(token_text(pr, token), token_len(pr, token)) & table->mask;;
       slot = (slot + 1) & table->mask) {
    uint32_t entry = table->slots[slot];
    if (!entry) break;
    skim_decl_t *decl = &pr->ast->decls[entry - 1];
    if (!scope_contains(pr, decl->scope, token) || !token_equal(pr, decl->token, token)) continue;
    if (!best || pr->ast->scopes[decl->scope].first > pr->ast->scopes[best->scope].first) best = decl;
  }
  if (!best || (scoped_alias && (best->kind != SKIM_D_ALIAS || best->scope == 0))) return;
  for (uint32_t k = 0; k < table->shadow_count; k++) {
    const skim_decl_t *shadow = &pr->ast->decls[table->shadows[k]];
    if (!scope_contains(pr, shadow->scope, token) || !token_equal(pr, shadow->token, token)) continue;
    if (pr->ast->scopes[shadow->scope].first >= pr->ast->scopes[best->scope].first) return;
  }
  best->used = true;
}

static void table_type_use(skim_printer_t *pr, const import_table_t *table, uint32_t token) {
  for (uint32_t slot = name_hash(token_text(pr, token), token_len(pr, token)) & table->mask;;
       slot = (slot + 1) & table->mask) {
    uint32_t entry = table->slots[slot];
    if (!entry) return;
    skim_decl_t *decl = &pr->ast->decls[entry - 1];
    if (decl->token != token && token_equal(pr, decl->token, token)) decl->typed = true;
  }
}

static void table_redeclare(skim_printer_t *pr, const import_table_t *table, const skim_decl_t *shadow) {
  for (uint32_t slot = name_hash(token_text(pr, shadow->token), token_len(pr, shadow->token)) & table->mask;;
       slot = (slot + 1) & table->mask) {
    uint32_t entry = table->slots[slot];
    if (!entry) return;
    skim_decl_t *decl = &pr->ast->decls[entry - 1];
    if (decl->scope == shadow->scope && token_equal(pr, decl->token, shadow->token)) decl->typed = true;
  }
}

static bool imported(const skim_decl_t *decl) {
  return decl->kind == SKIM_D_IMPORT || decl->kind == SKIM_D_ALIAS;
}

static bool alias_target(const skim_printer_t *pr, const skim_node_t *node) {
  return node->kind == SKIM_N_IMPORT_EQUALS && pr->t[node->alias.value].flags & SKIM_TF_REF;
}

static bool alias_reference(const skim_printer_t *pr, uint32_t token) {
  for (uint32_t n = 0; n < pr->ast->node_count; n++)
    if (alias_target(pr, &pr->ast->nodes[n]) && pr->ast->nodes[n].alias.value == token) return true;
  return false;
}

static void resolve_imports(skim_printer_t *pr) {
  pr->resolved = true;
  uint32_t imports = 0;
  for (uint32_t i = 0; i < pr->ast->decl_count; i++)
    imports += imported(&pr->ast->decls[i]);
  if (!imports) return;

  import_table_t table = {.mask = 15};
  while (table.mask < imports * 2)
    table.mask = table.mask * 2 + 1;
  table.slots = calloc(table.mask + 1, sizeof(*table.slots));
  table.shadows = malloc(sizeof(*table.shadows) * (pr->ast->decl_count + 1));
  if (!table.slots || !table.shadows) skim_die("out of memory");
  for (uint32_t i = 0; i < pr->ast->decl_count; i++) {
    if (!imported(&pr->ast->decls[i])) continue;
    uint32_t token = pr->ast->decls[i].token;
    uint32_t slot = name_hash(token_text(pr, token), token_len(pr, token)) & table.mask;
    while (table.slots[slot])
      slot = (slot + 1) & table.mask;
    table.slots[slot] = i + 1;
  }
  for (uint32_t i = 0; i < pr->ast->decl_count; i++) {
    const skim_decl_t *decl = &pr->ast->decls[i];
    if (imported(decl) || decl->kind == SKIM_D_TYPE || !table_has(pr, &table, decl->token)) continue;
    table.shadows[table.shadow_count++] = i;
    table_redeclare(pr, &table, decl);
  }

  for (uint32_t i = 0; i < pr->count; i++) {
    const skim_token_t *token = &pr->t[i];
    if (token->kind != SKIM_T_WORD || !(token->flags & (SKIM_TF_REF | SKIM_TF_STRIP)) || !table_has(pr, &table, i))
      continue;
    if (!(token->flags & SKIM_TF_STRIP)) {
      if (!alias_reference(pr, i)) table_use(pr, &table, i, false);
      continue;
    }
    table_type_use(pr, &table, i);
    if (i > 0 && token_word(pr, i - 1) == SKIM_KW_TYPEOF) table_use(pr, &table, i, true);
  }
  for (bool changed = true; changed;) {
    changed = false;
    for (uint32_t n = 0; n < pr->ast->node_count; n++) {
      const skim_node_t *node = &pr->ast->nodes[n];
      if (!alias_target(pr, node) || pr->t[node->first].flags & SKIM_TF_STRIP) continue;
      const skim_decl_t *alias = skim_print_decl(pr, node->alias.name);
      bool live = node->flags & SKIM_NF_EXPORT || pr->options->only_remove_type_imports || (alias && alias->used);
      if (!live || pr->t[node->alias.value].flags & SKIM_TF_VISITED) continue;
      pr->t[node->alias.value].flags |= SKIM_TF_VISITED;
      table_use(pr, &table, node->alias.value, false);
      changed = true;
    }
  }
  free(table.slots);
  free(table.shadows);
}

static bool binding_kept(skim_printer_t *pr, uint32_t token) {
  if (pr->options->only_remove_type_imports) return true;
  if (!pr->resolved) resolve_imports(pr);
  const skim_decl_t *decl = skim_print_decl(pr, token);
  if (!decl) return false;
  return decl->used || (decl->kind == SKIM_D_IMPORT && !decl->typed);
}

static void put_range(skim_printer_t *pr, uint32_t from, uint32_t to) {
  if (to > from) skim_str_putn(pr->out, pr->src + pr->t[from].start, pr->t[to - 1].end - pr->t[from].start);
}

static uint32_t print_terminated(skim_printer_t *pr, const skim_node_t *node) {
  if (pr->t[node->end - 1].kind == ';') return node->first;
  skim_print_range(pr, node->first, node->end);
  skim_print_flush(pr);
  put(pr, ";");
  return node->end;
}

static bool type_specifier(const skim_printer_t *pr, uint32_t from, uint32_t to) {
  if (to - from < 2 || token_word(pr, from) != SKIM_KW_TYPE) return false;
  return !(to - from == 3 && token_word(pr, from + 1) == SKIM_KW_AS);
}

static uint32_t specifier_end(const skim_printer_t *pr, uint32_t from, uint32_t close) {
  while (from < close && pr->t[from].kind != ',')
    from++;
  return from;
}

uint32_t skim_print_import(skim_printer_t *pr, const skim_node_t *node) {
  uint32_t clause = node->import.clause;
  uint32_t source = node->import.source;
  if (clause >= source) {
    pr->module_syntax = true;
    return print_terminated(pr, node);
  }

  uint32_t cursor = clause;
  uint32_t default_name = 0;
  uint32_t namespace_name = 0;
  uint32_t open = 0;
  if (pr->t[cursor].kind == SKIM_T_WORD) {
    default_name = cursor++;
    if (pr->t[cursor].kind == ',') cursor++;
  }
  if (pr->t[cursor].kind == '*') namespace_name = cursor + 2;
  else if (pr->t[cursor].kind == '{') open = cursor;

  bool keep_default = default_name && binding_kept(pr, default_name);
  bool keep_namespace = namespace_name && binding_kept(pr, namespace_name);
  uint32_t close = open ? token_close(pr, open) : 0;
  uint32_t total = 0, kept = 0;
  for (uint32_t item = open + 1; open && item < close;) {
    uint32_t end = specifier_end(pr, item, close);
    if (end > item) {
      total++;
      if (!type_specifier(pr, item, end) && binding_kept(pr, end - 1)) kept++;
    }
    item = end + 1;
  }

  if (!keep_default && !keep_namespace && !kept) {
    if (default_name || namespace_name || total) {
      skim_print_remove(pr, node->first, node->end);
      return node->first;
    }
    skim_print_begin(pr, node->first);
    put(pr, "import ");
    put_range(pr, source, node->end);
    if (pr->t[node->end - 1].kind != ';') put(pr, ";");
    skim_print_resume(pr, node->end);
    pr->module_syntax = true;
    return node->end;
  }
  pr->module_syntax = true;
  if (kept == total && (!default_name || keep_default) && (!namespace_name || keep_namespace))
    return print_terminated(pr, node);

  skim_print_begin(pr, node->first);
  put(pr, "import ");
  if (keep_default) {
    put_token(pr, default_name);
    if (keep_namespace || kept) put(pr, ", ");
  }
  if (keep_namespace) {
    put(pr, "* as ");
    put_token(pr, namespace_name);
  }
  if (kept) {
    put(pr, "{ ");
    bool first = true;
    for (uint32_t item = open + 1; item < close;) {
      uint32_t end = specifier_end(pr, item, close);
      if (end > item && !type_specifier(pr, item, end) && binding_kept(pr, end - 1)) {
        if (!first) put(pr, ", ");
        put_range(pr, item, end);
        first = false;
      }
      item = end + 1;
    }
    put(pr, " }");
  }
  put(pr, " from ");
  put_range(pr, source, node->end);
  if (pr->t[node->end - 1].kind != ';') put(pr, ";");
  skim_print_resume(pr, node->end);
  return node->end;
}

uint32_t skim_print_import_equals(skim_printer_t *pr, const skim_node_t *node) {
  bool exported = node->flags & SKIM_NF_EXPORT;
  pr->exported = false;
  if (!exported && !binding_kept(pr, node->alias.name)) {
    skim_print_remove(pr, node->first, node->end);
    return node->first;
  }
  bool require = token_word(pr, node->alias.value) == SKIM_KW_REQUIRE && pr->t[node->alias.value + 1].kind == '(';
  skim_print_begin(pr, node->first);
  put(pr, require ? "const " : "var ");
  put_token(pr, node->alias.name);
  put(pr, " = ");
  put_range(pr, node->alias.value, node->end);
  if (pr->t[node->end - 1].kind != ';') put(pr, ";");
  if (exported && pr->param) {
    skim_print_line(pr, node->first);
    skim_print_alias(pr, node->alias.name);
    put(pr, ";");
  }
  skim_print_resume(pr, node->end);
  return node->end;
}

static bool names_value(const skim_printer_t *pr, uint32_t name) {
  bool value = false;
  bool type = false;
  for (uint32_t i = 0; i < pr->ast->decl_count; i++) {
    const skim_decl_t *decl = &pr->ast->decls[i];
    if (decl->scope != 0 || !token_equal(pr, decl->token, name)) continue;
    if (decl->kind == SKIM_D_TYPE || decl->kind == SKIM_D_AMBIENT) type = true;
    else value = true;
  }
  return value || !type;
}

static bool export_specifier_kept(const skim_printer_t *pr, uint32_t from, uint32_t to, bool has_source) {
  if (type_specifier(pr, from, to)) return false;
  return has_source || names_value(pr, from);
}

static uint32_t print_named_export(skim_printer_t *pr, const skim_node_t *node) {
  uint32_t open = node->exported.body;
  uint32_t close = token_close(pr, open);
  uint32_t source = close + 1;
  bool has_source = token_word(pr, source) == SKIM_KW_FROM;
  uint32_t total = 0, kept = 0, typed = 0;
  for (uint32_t item = open + 1; item < close;) {
    uint32_t end = specifier_end(pr, item, close);
    if (end > item) {
      total++;
      typed += type_specifier(pr, item, end);
      kept += export_specifier_kept(pr, item, end, has_source);
    }
    item = end + 1;
  }
  if (kept == total && (total || has_source)) {
    pr->module_syntax = true;
    return print_terminated(pr, node);
  }
  if (!total && !has_source) {
    pr->module_syntax = true;
    skim_print_begin(pr, node->first);
    put(pr, "export {};");
    skim_print_resume(pr, node->end);
    return node->end;
  }
  if (!kept && !(has_source && typed < total)) {
    skim_print_remove(pr, node->first, node->end);
    return node->first;
  }
  pr->module_syntax = true;
  skim_print_begin(pr, node->first);
  put(pr, "export {");
  bool first = true;
  for (uint32_t item = open + 1; item < close;) {
    uint32_t end = specifier_end(pr, item, close);
    if (end > item && export_specifier_kept(pr, item, end, has_source)) {
      put(pr, first ? " " : ", ");
      put_range(pr, item, end);
      first = false;
    }
    item = end + 1;
  }
  put(pr, first ? "}" : " }");
  if (has_source) {
    put(pr, " ");
    put_range(pr, source, node->end);
  }
  if (pr->t[node->end - 1].kind != ';' || !has_source) put(pr, ";");
  skim_print_resume(pr, node->end);
  return node->end;
}

static uint32_t print_decorated_export(skim_printer_t *pr, const skim_node_t *node) {
  skim_print_begin(pr, node->first);
  put(pr, node->exported.form == SKIM_EXPORT_DEFAULT ? "export default " : "export ");
  skim_print_range(pr, node->first, node->exported.keyword);
  skim_print_flush(pr);
  put(pr, " ");
  pr->copied = pr->kept = pr->t[node->exported.body].start;
  pr->module_syntax = true;
  return node->exported.body;
}

static bool default_export_is_type(const skim_printer_t *pr, const skim_node_t *node) {
  uint32_t name = node->exported.body;
  uint32_t end = name + 1 + (pr->t[name + 1].kind == ';');
  if (node->exported.form != SKIM_EXPORT_DEFAULT || pr->t[name].kind != SKIM_T_WORD || end != node->end) return false;
  return !names_value(pr, name);
}

uint32_t skim_print_export(skim_printer_t *pr, const skim_node_t *node) {
  if (pr->param) return skim_print_namespace_export(pr, node);
  if (node->exported.keyword != node->first) return print_decorated_export(pr, node);
  switch (node->exported.form) {
  case SKIM_EXPORT_NAMED:
    return print_named_export(pr, node);
  case SKIM_EXPORT_ASSIGNMENT:
    skim_print_begin(pr, node->first);
    put(pr, "module.exports");
    skim_print_resume(pr, node->first + 1);
    return node->first + 1;
  default:
    break;
  }
  if (default_export_is_type(pr, node)) {
    skim_print_remove(pr, node->first, node->end);
    return node->first;
  }
  pr->module_syntax = true;
  uint32_t body = node->exported.body;
  if (pr->cursor == pr->ast->node_count || pr->ast->nodes[pr->cursor].first != body) return node->first;
  uint8_t child = pr->ast->nodes[pr->cursor].kind;
  if (child != SKIM_N_ENUM && child != SKIM_N_NAMESPACE) return node->first;
  skim_print_begin(pr, node->first);
  pr->copied = pr->kept = pr->t[body].start;
  pr->exported = true;
  return body;
}
