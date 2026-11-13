#include "parse_internal.h"

static void semicolon(parser_t *p) {
  if (kind(p) == ';') p->i++;
}

static void module_source(parser_t *p) {
  if (word(p) == SKIM_KW_FROM) p->i++;
  if (kind(p) == SKIM_T_STRING) p->i++;
  bool attributes = kind(p) == SKIM_T_WORD && !newline_at(p, p->i) && kind_at(p, p->i + 1) == '{';
  if (attributes) p->i = after_pair(p, p->i + 1);
  semicolon(p);
}

static bool type_modifier(const parser_t *p) {
  if (word(p) != SKIM_KW_TYPE) return false;
  uint32_t next = p->i + 1;
  switch (kind_at(p, next)) {
  case '{':
  case '*':
    return true;
  case SKIM_T_WORD:
    if (word_at(p, next) != SKIM_KW_FROM) return true;
    return kind_at(p, next + 1) != SKIM_T_STRING;
  default:
    return false;
  }
}

static void entity_reference(parser_t *p) {
  if (word(p) == SKIM_KW_REQUIRE && kind_at(p, p->i + 1) == '(') {
    p->i = after_pair(p, p->i + 1);
    return;
  }
  if (kind(p) == SKIM_T_WORD) {
    flag(p, p->i, SKIM_TF_REF);
    p->i++;
  }
  while (kind(p) == '.' && kind_at(p, p->i + 1) == SKIM_T_WORD)
    p->i += 2;
}

static void import_specifiers(parser_t *p, bool type_only) {
  uint32_t open = p->i;
  uint32_t close = close_of(p, open);
  for (uint32_t item = open + 1; item < close;) {
    uint32_t end = item;
    while (end < close && kind_at(p, end) != ',')
      end++;
    bool is_type = type_only || (end - item > 1 && word_at(p, item) == SKIM_KW_TYPE &&
                                 !(end - item == 3 && word_at(p, item + 1) == SKIM_KW_AS));
    if (end > item) skim_declare(p, end - 1, is_type ? SKIM_D_TYPE : SKIM_D_IMPORT);
    item = end + 1;
  }
  p->i = after_pair(p, open);
}

static void import_declaration(parser_t *p, uint32_t first, uint8_t flags) {
  bool type_only = false;
  p->i++;
  if (type_modifier(p)) {
    type_only = true;
    p->i++;
  }
  if (kind(p) == SKIM_T_WORD && kind_at(p, p->i + 1) == '=') {
    uint32_t node = skim_node_open(p, SKIM_N_IMPORT_EQUALS, first, flags);
    uint32_t name = p->i;
    skim_declare(p, name, type_only ? SKIM_D_TYPE : SKIM_D_ALIAS);
    p->i += 2;
    uint32_t value = p->i;
    entity_reference(p);
    semicolon(p);
    skim_node_t *alias = skim_node_close(p, node);
    if (type_only) strip(p, first, p->i);
    if (!alias) return;
    alias->alias.name = name;
    alias->alias.value = value;
    if ((type_only || flags & SKIM_NF_EXPORT) && !p->block) p->ast->module = true;
    return;
  }

  uint32_t node = skim_node_open(p, SKIM_N_IMPORT, first, 0);
  if (!p->dry && !p->block) p->ast->module = true;
  uint32_t clause = p->i;
  skim_decl_kind_t binding = type_only ? SKIM_D_TYPE : SKIM_D_IMPORT;
  if (kind(p) == SKIM_T_WORD && !(word(p) == SKIM_KW_FROM && kind_at(p, p->i + 1) == SKIM_T_STRING)) {
    skim_declare(p, p->i++, binding);
    if (kind(p) == ',') p->i++;
  }
  if (kind(p) == '*') {
    p->i++;
    if (word(p) == SKIM_KW_AS) p->i++;
    if (kind(p) == SKIM_T_WORD) skim_declare(p, p->i++, binding);
  } else if (kind(p) == '{') {
    import_specifiers(p, type_only);
  }
  uint32_t source = word(p) == SKIM_KW_FROM ? p->i + 1 : p->i;
  module_source(p);
  skim_node_t *import = skim_node_close(p, node);
  if (type_only) strip(p, first, p->i);
  if (!import) return;
  import->import.clause = clause;
  import->import.source = source;
}

void skim_parse_import(parser_t *p) {
  import_declaration(p, p->i, 0);
}

static bool default_declaration(parser_t *p) {
  switch (word(p)) {
  case SKIM_KW_CLASS:
  case SKIM_KW_FUNCTION:
  case SKIM_KW_INTERFACE:
    return true;
  case SKIM_KW_ABSTRACT:
    return word_at(p, p->i + 1) == SKIM_KW_CLASS;
  case SKIM_KW_ASYNC:
    return word_at(p, p->i + 1) == SKIM_KW_FUNCTION && !newline_at(p, p->i + 1);
  default:
    return kind(p) == '@';
  }
}

void skim_parse_export(parser_t *p) {
  uint32_t keyword = p->i;
  uint32_t first = p->decorators ? p->decorators - 1 : keyword;
  uint32_t node = skim_node_open(p, SKIM_N_EXPORT, first, 0);
  p->decorators = 0;
  skim_export_form_t form = SKIM_EXPORT_DECLARATION;
  bool removed = false;
  p->i++;
  uint32_t body = p->i;

  if (word(p) == SKIM_KW_TYPE && (kind_at(p, body + 1) == '{' || kind_at(p, body + 1) == '*')) {
    p->i++;
    removed = true;
  }
  switch (kind(p)) {
  case '{':
    form = SKIM_EXPORT_NAMED;
    if (word_at(p, after_pair(p, p->i)) != SKIM_KW_FROM) {
      for (uint32_t item = p->i + 1; item < close_of(p, p->i); item++)
        if (kind_at(p, item - 1) != SKIM_T_WORD && kind_at(p, item) == SKIM_T_WORD) flag(p, item, SKIM_TF_REF);
    }
    p->i = after_pair(p, p->i);
    module_source(p);
    break;
  case '*':
    form = SKIM_EXPORT_STAR;
    p->i++;
    if (word(p) == SKIM_KW_AS) p->i += 2;
    module_source(p);
    break;
  case '=':
    form = SKIM_EXPORT_ASSIGNMENT;
    p->i++;
    skim_parse_expression(p);
    semicolon(p);
    break;
  default:
    if (word(p) == SKIM_KW_AS && word_at(p, body + 1) == SKIM_KW_NAMESPACE) {
      p->i += 3;
      semicolon(p);
      removed = true;
      break;
    }
    if (word(p) == SKIM_KW_IMPORT && kind_at(p, body + 1) == SKIM_T_WORD) {
      import_declaration(p, body, SKIM_NF_EXPORT);
      break;
    }
    if (word(p) == SKIM_KW_DEFAULT) {
      form = SKIM_EXPORT_DEFAULT;
      p->i++;
      body = p->i;
      if (!default_declaration(p)) {
        skim_parse_assignment(p, false);
        semicolon(p);
        break;
      }
    }
    skim_parse_statement(p);
    if (kind_at(p, body) == '@') {
      uint32_t declaration = p->i;
      skim_parse_statement(p);
      if (p->t[declaration].flags & SKIM_TF_STRIP) removed = true;
    }
    if (p->t[body].flags & SKIM_TF_STRIP && p->t[body].word != SKIM_KW_ABSTRACT) removed = true;
    break;
  }
  if (removed) strip(p, first, p->i);
  skim_node_t *exported = skim_node_close(p, node);
  if (!exported) return;
  bool namespace_export = word_at(p, body) == SKIM_KW_NAMESPACE || word_at(p, body) == SKIM_KW_MODULE;
  if (!p->block && form != SKIM_EXPORT_ASSIGNMENT && !namespace_export && word_at(p, body) != SKIM_KW_AS)
    p->ast->module = true;
  exported->exported.body = body;
  exported->exported.form = form;
  exported->exported.keyword = keyword;
}

void skim_parse_enum(parser_t *p, uint32_t first, uint8_t flags) {
  uint32_t node = skim_node_open(p, SKIM_N_ENUM, first, flags);
  if (flags & SKIM_NF_CONST) p->i++;
  p->i++;
  uint32_t name = p->i++;
  uint32_t body = p->i;
  skim_declare(p, name, SKIM_D_ENUM);
  if (kind(p) == '{') {
    uint32_t close = close_of(p, body);
    p->i++;
    while (p->i < close) {
      uint32_t before = p->i;
      if (kind(p) == ',') {
        p->i++;
        continue;
      }
      if (kind(p) == '[') p->i = after_pair(p, p->i);
      else p->i++;
      if (kind(p) == '=') {
        p->i++;
        skim_parse_assignment(p, false);
      }
      if (p->i == before) p->i++;
    }
    uint32_t after = after_pair(p, body);
    if (p->i < after) p->i = after;
  }
  skim_node_t *declaration = skim_node_close(p, node);
  if (!declaration) return;
  declaration->decl.name = name;
  declaration->decl.body = body;
}

void skim_parse_namespace(parser_t *p, uint32_t first, uint8_t flags) {
  uint32_t node = skim_node_open(p, SKIM_N_NAMESPACE, first, flags);
  p->i++;
  uint32_t name = p->i++;
  while (kind(p) == '.' && kind_at(p, p->i + 1) == SKIM_T_WORD)
    p->i += 2;
  uint32_t body = p->i;
  uint32_t decl = p->dry ? 0 : p->ast->decl_count;
  bool outer_runtime = p->runtime;
  if (kind_at(p, name) == SKIM_T_WORD) skim_declare(p, name, SKIM_D_NAMESPACE);
  p->runtime = false;
  if (kind(p) == '{') {
    scope_mark_t scope = skim_scope_push(p, body, true);
    skim_parse_block(p);
    skim_scope_pop(p, scope);
  } else {
    semicolon(p);
  }
  bool runtime = p->runtime;
  p->runtime = outer_runtime;
  skim_node_t *declaration = skim_node_close(p, node);
  if (!declaration) return;
  declaration->decl.name = name;
  declaration->decl.body = body;
  if (runtime) return;
  strip(p, first, p->i);
  if (kind_at(p, name) == SKIM_T_WORD) p->ast->decls[decl].kind = SKIM_D_TYPE;
}
