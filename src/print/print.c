#include "chars.h"
#include "print_internal.h"

#include <stdlib.h>

void skim_print_flush(skim_printer_t *pr) {
  if (pr->kept > pr->copied) skim_str_putn(pr->out, pr->src + pr->copied, pr->kept - pr->copied);
  pr->copied = pr->kept;
}

void skim_print_resume(skim_printer_t *pr, uint32_t token) {
  pr->copied = pr->kept = token ? pr->t[token - 1].end : 0;
}

void skim_print_begin(skim_printer_t *pr, uint32_t token) {
  if (pr->t[token].start > pr->kept) pr->kept = pr->t[token].start;
  skim_print_flush(pr);
}

void skim_print_remove(skim_printer_t *pr, uint32_t from, uint32_t to) {
  for (uint32_t i = from; i < to; i++)
    pr->t[i].flags |= SKIM_TF_STRIP;
}

skim_decl_t *skim_print_decl(const skim_printer_t *pr, uint32_t token) {
  uint32_t low = 0;
  uint32_t high = pr->ast->decl_count;
  while (low < high) {
    uint32_t mid = low + (high - low) / 2;
    if (pr->ast->decls[mid].token < token) low = mid + 1;
    else high = mid;
  }
  return low < pr->ast->decl_count && pr->ast->decls[low].token == token ? &pr->ast->decls[low] : NULL;
}

const skim_node_t *skim_print_node(const skim_printer_t *pr, uint32_t token) {
  uint32_t low = 0;
  uint32_t high = pr->ast->node_count;
  while (low < high) {
    uint32_t mid = low + (high - low) / 2;
    if (pr->ast->nodes[mid].first < token) low = mid + 1;
    else high = mid;
  }
  return low < pr->ast->node_count && pr->ast->nodes[low].first == token ? &pr->ast->nodes[low] : NULL;
}

uint32_t skim_print_scope_of(const skim_printer_t *pr, uint32_t token) {
  const skim_decl_t *decl = skim_print_decl(pr, token);
  return decl ? decl->scope : 0;
}

bool skim_print_declared(const skim_printer_t *pr, uint32_t name, uint32_t before, bool types) {
  uint32_t scope = skim_print_scope_of(pr, name);
  for (uint32_t i = 0; i < pr->ast->decl_count; i++) {
    const skim_decl_t *decl = &pr->ast->decls[i];
    if (decl->token >= before) break;
    if (decl->scope != scope || (decl->kind == SKIM_D_TYPE) != types || decl->token == name) continue;
    if (token_equal(pr, decl->token, name)) return true;
  }
  return false;
}

void skim_print_line(skim_printer_t *pr, uint32_t token) {
  uint32_t start = pr->t[token].start;
  uint32_t line = start;
  while (line > 0 && pr->src[line - 1] != '\n' && pr->src[line - 1] != '\r')
    line--;
  uint32_t indent = line;
  while (indent < start && (pr->src[indent] == ' ' || pr->src[indent] == '\t'))
    indent++;
  if (indent != start) {
    skim_str_putc(pr->out, ' ');
    return;
  }
  skim_str_putc(pr->out, '\n');
  skim_str_putn(pr->out, pr->src + line, indent - line);
}

static bool opens_operand(char c) {
  return skim_is_id_part(c) || c == '(' || c == '[' || c == '{' || c == '"' || c == '\'' || c == '`';
}

static bool glues(const skim_printer_t *pr, uint32_t at) {
  if (pr->out->len == 0 || at >= pr->len) return false;
  char prev = pr->out->data[pr->out->len - 1];
  char next = pr->src[at];
  if (skim_is_id_part(prev) && skim_is_id_part(next)) return true;
  return (prev == '+' || prev == '-' || prev == '/' || prev == '<') && next == prev;
}

void skim_print_separate(skim_printer_t *pr) {
  size_t end = pr->out->len;
  while (end > 0 && skim_is_space(pr->out->data[end - 1]))
    end--;
  char last = end ? pr->out->data[end - 1] : ';';
  if (last != ';' && last != '{' && last != '}') skim_str_putc(pr->out, ';');
}

static bool continues_statement(const skim_printer_t *pr, uint32_t next) {
  if (next >= pr->count) return false;
  switch (pr->t[next].kind) {
  case '(':
  case '[':
  case '+':
  case '-':
  case '/':
  case SKIM_T_TEMPLATE:
  case SKIM_T_REGEX:
    return true;
  default:
    return false;
  }
}

static uint32_t skip_stripped(skim_printer_t *pr, uint32_t first, uint32_t to) {
  uint32_t next = first + 1;
  while (next < to && (pr->t[next].flags & (SKIM_TF_STRIP | SKIM_TF_LEAD)) == SKIM_TF_STRIP)
    next++;
  skim_print_flush(pr);
  if (pr->t[first].flags & SKIM_TF_EMPTY) skim_str_puts(pr->out, " {}");
  else if (pr->t[first].flags & SKIM_TF_LEAD && continues_statement(pr, next)) skim_print_separate(pr);
  const skim_token_t *resume = &pr->t[next];
  if (pr->t[first].flags & SKIM_TF_NEWLINE) {
    if (resume->flags & SKIM_TF_JOIN) skim_str_putc(pr->out, ' ');
    else if (pr->t[first].start > pr->kept) skim_str_putn(pr->out, pr->src + pr->kept, pr->t[first].start - pr->kept);
    pr->copied = resume->start;
  } else {
    pr->copied = pr->t[next - 1].end;
    bool spaced = pr->t[first].start > pr->kept && pr->copied == resume->start;
    if (glues(pr, pr->copied) || (spaced && opens_operand(pr->src[pr->copied]))) skim_str_putc(pr->out, ' ');
  }
  pr->kept = pr->copied;
  return next;
}

static uint32_t print_node(skim_printer_t *pr, uint32_t i) {
  while (pr->cursor < pr->ast->node_count && pr->ast->nodes[pr->cursor].first < i)
    pr->cursor++;
  if (pr->cursor == pr->ast->node_count || pr->ast->nodes[pr->cursor].first != i) return i;
  const skim_node_t *node = &pr->ast->nodes[pr->cursor++];
  switch (node->kind) {
  case SKIM_N_IMPORT:
    return skim_print_import(pr, node);
  case SKIM_N_IMPORT_EQUALS:
    return skim_print_import_equals(pr, node);
  case SKIM_N_EXPORT:
    return skim_print_export(pr, node);
  case SKIM_N_ENUM:
    return skim_print_enum(pr, node);
  case SKIM_N_NAMESPACE:
    return skim_print_namespace(pr, node);
  case SKIM_N_CLASS:
    return skim_print_class(pr, node);
  case SKIM_N_CONSTRUCTOR:
    return skim_print_constructor(pr, node);
  case SKIM_N_FIELD:
    return skim_print_field(pr, node);
  default:
    return i;
  }
}

void skim_print_range(skim_printer_t *pr, uint32_t from, uint32_t to) {
  for (uint32_t i = from; i < to;) {
    const skim_token_t *token = &pr->t[i];
    if (!(token->flags & (SKIM_TF_STRIP | SKIM_TF_REWRITE | SKIM_TF_NODE | SKIM_TF_PAREN))) {
      pr->kept = token->end;
      i++;
      continue;
    }
    if (token->flags & SKIM_TF_NODE && !(token->flags & SKIM_TF_STRIP)) {
      uint32_t next = print_node(pr, i);
      if (next != i) {
        i = next;
        continue;
      }
      token = &pr->t[i];
    }
    if (token->flags & SKIM_TF_STRIP) {
      i = skip_stripped(pr, i, to);
      continue;
    }
    if (token->flags & SKIM_TF_PAREN) {
      uint32_t close = token_close(pr, i);
      pr->t[i].flags &= (uint16_t)~SKIM_TF_PAREN;
      skim_print_begin(pr, i);
      skim_str_putc(pr->out, '(');
      skim_print_range(pr, i, close + 1 < to ? close + 1 : to);
      skim_print_flush(pr);
      skim_str_putc(pr->out, ')');
      i = close + 1 < to ? close + 1 : to;
      continue;
    }
    if (token->flags & SKIM_TF_REWRITE && token->kind == SKIM_T_WORD) {
      uint32_t next = skim_print_enum_reference(pr, i);
      if (next == i) pr->kept = token->end;
      i = next == i ? i + 1 : next;
      continue;
    }
    if (token->flags & SKIM_TF_REWRITE) {
      skim_print_begin(pr, i);
      if (token->kind == SKIM_T_NUMBER) skim_print_number(pr, i);
      else skim_print_string(pr, i);
      skim_print_resume(pr, i + 1);
      i++;
      continue;
    }
    pr->kept = token->end;
    i++;
  }
}

void skim_printer_free(skim_printer_t *pr) {
  free(pr->enum_members);
  free(pr->enum_strings.data);
  free(pr->params.data);
  *pr = (skim_printer_t){0};
}

static void finish(skim_printer_t *pr, bool marker) {
  skim_str_t *out = pr->out;
  size_t blank = 0;
  while (blank < out->len && skim_is_space(out->data[blank]))
    blank++;
  if (blank) {
    memmove(out->data, out->data + blank, out->len - blank);
    out->len -= blank;
  }
  if (out->len && out->data[out->len - 1] != '\n') skim_str_putc(out, '\n');
  if (marker) skim_str_puts(out, "export {};\n");
  out->data[out->len] = '\0';
}

void skim_print(
  skim_printer_t *pr,
  const char *src,
  size_t len,
  skim_tokens_t *tokens,
  skim_ast_t *ast,
  const skim_options_t *options,
  skim_source_mode_t mode,
  skim_str_t *out
) {
  pr->src = src;
  pr->len = len;
  pr->t = tokens->items;
  pr->count = tokens->count;
  pr->ast = ast;
  pr->options = options;
  pr->out = out;
  pr->cursor = pr->param = 0;
  pr->enum_member_count = 0;
  pr->enum_strings.len = pr->params.len = 0;
  pr->exported = pr->resolved = pr->module_syntax = false;
  pr->copied = pr->kept = len >= 3 && memcmp(src, "\xef\xbb\xbf", 3) == 0 ? 3 : 0;

  out->len = 0;
  skim_str_reserve(out, len + 128);
  skim_print_range(pr, 0, pr->count);
  pr->kept = (uint32_t)len;
  skim_print_flush(pr);
  finish(pr, mode == SKIM_SOURCE_MODULE && ast->module && !pr->module_syntax);
}
