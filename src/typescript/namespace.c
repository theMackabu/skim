#include "skim_internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char **used_namespace_params;
static size_t used_namespace_param_count;

static bool range_has_newline(const char *src, size_t start, size_t end) {
  for (size_t i = start; i < end; i++) {
    if (src[i] == '\n' || src[i] == '\r') return true;
  }
  return false;
}

void skim_ts_namespace_reset(void) {
  for (size_t i = 0; i < used_namespace_param_count; i++) {
    free(used_namespace_params[i]);
  }
  free(used_namespace_params);
  used_namespace_params = NULL;
  used_namespace_param_count = 0;
}

static bool namespace_param_used(const char *name) {
  for (size_t i = 0; i < used_namespace_param_count; i++) {
    if (strcmp(used_namespace_params[i], name) == 0) return true;
  }
  return false;
}

static void remember_namespace_param(const char *name) {
  char **next = realloc(used_namespace_params, sizeof(*used_namespace_params) * (used_namespace_param_count + 1));
  if (!next) skim_die("out of memory");
  used_namespace_params = next;
  used_namespace_params[used_namespace_param_count++] = skim_slice_dup(name, 0, strlen(name));
}

static char *make_namespace_param(const char *name) {
  size_t len = strlen(name);
  size_t base_len = len;
  while (base_len > 0 && name[base_len - 1] >= '0' && name[base_len - 1] <= '9')
    base_len--;

  skim_str_t candidate = {0};
  skim_str_putc(&candidate, '_');
  skim_str_putn(&candidate, name, base_len ? base_len : len);
  if (!namespace_param_used(candidate.data)) {
    remember_namespace_param(candidate.data);
    return candidate.data;
  }
  free(candidate.data);

  skim_str_t full = {0};
  skim_str_putc(&full, '_');
  skim_str_puts(&full, name);
  if (!namespace_param_used(full.data)) {
    remember_namespace_param(full.data);
    return full.data;
  }

  for (unsigned suffix = 2;; suffix++) {
    full.len = 0;
    skim_str_putc(&full, '_');
    skim_str_puts(&full, name);
    char buf[32];
    snprintf(buf, sizeof(buf), "%u", suffix);
    skim_str_puts(&full, buf);
    if (!namespace_param_used(full.data)) {
      remember_namespace_param(full.data);
      return full.data;
    }
  }
}

static void terminate_namespace_body(skim_str_t *body) {
  size_t end = body->len;
  while (end > 0 && (body->data[end - 1] == ' ' || body->data[end - 1] == '\t' || body->data[end - 1] == '\n' ||
                     body->data[end - 1] == '\r'))
    end--;
  if (end == 0) return;
  char last = body->data[end - 1];
  if (last != ';' && last != '}') {
    body->len = end;
    body->data[body->len] = '\0';
    skim_str_putc(body, ';');
  }
}

static bool namespace_body_has_ambient_decl(const char *src, size_t len, size_t start, size_t end) {
  for (size_t i = start; i < end;) {
    i = skim_skip_ws_comments(src, len, i);
    if (i >= end) break;
    if (skim_word_at(src, len, i, "export")) i = skim_skip_ws_comments(src, len, i + 6);
    if (i < end && skim_word_at(src, len, i, "declare")) {
      size_t after_declare = skim_skip_ws_comments(src, len, i + 7);
      if (
        !skim_word_at(src, len, after_declare, "namespace") && !skim_word_at(src, len, after_declare, "module") &&
        !skim_word_at(src, len, after_declare, "global") && !skim_word_at(src, len, after_declare, "interface")
      ) {
        return true;
      }
      size_t body = after_declare;
      while (body < end && src[body] != '{' && src[body] != ';') {
        if (src[body] == '\'' || src[body] == '"' || src[body] == '`') body = skim_skip_string_raw(src, len, body);
        else body++;
      }
      i = body < end && src[body] == '{' ? skim_skip_balanced(src, len, body, '{', '}')
                                         : skim_skip_statement_like(src, len, body);
      continue;
    }
    if (i < end && skim_word_at(src, len, i, "function")) {
      size_t p = i + 8;
      int paren = 0, bracket = 0, angle = 0;
      while (p < end) {
        char c = src[p];
        if (c == '\'' || c == '"' || c == '`') {
          p = skim_skip_string_raw(src, len, p);
          continue;
        }
        if (c == '(') paren++;
        else if (c == ')' && paren > 0) paren--;
        else if (c == '[') bracket++;
        else if (c == ']' && bracket > 0) bracket--;
        else if (c == '<') angle++;
        else if (c == '>' && angle > 0) angle--;
        else if (c == ';' && paren == 0 && bracket == 0 && angle == 0) return true;
        else if (c == '{' && paren == 0 && bracket == 0 && angle == 0) break;
        p++;
      }
    }
    if (src[i] == '\'' || src[i] == '"' || src[i] == '`') i = skim_skip_string_raw(src, len, i);
    else i++;
  }
  return false;
}

static size_t skip_type_like_decl(const char *src, size_t len, size_t i) {
  size_t body = i;
  while (body < len && src[body] != ';' && src[body] != '{' && src[body] != '\n' && src[body] != '\r') {
    if (src[body] == '\'' || src[body] == '"' || src[body] == '`') body = skim_skip_string_raw(src, len, body);
    else body++;
  }
  if (body < len && src[body] == '{') return skim_skip_balanced(src, len, body, '{', '}');
  return skim_skip_statement_like(src, len, body);
}

static bool namespace_source_has_runtime_code(const char *src, size_t len, size_t start, size_t end) {
  for (size_t i = start; i < end;) {
    i = skim_skip_ws_comments(src, len, i);
    if (i >= end) break;
    if (src[i] == ';') {
      i++;
      continue;
    }

    size_t item = i;
    if (skim_word_at(src, len, i, "export")) i = skim_skip_ws_comments(src, len, i + 6);
    if (skim_word_at(src, len, i, "declare")) {
      i = skim_skip_statement_like(src, len, i);
      continue;
    }
    if (skim_word_at(src, len, i, "type") || skim_word_at(src, len, i, "interface")) {
      size_t kw_len = skim_word_at(src, len, i, "type") ? 4 : 9;
      size_t name_start = skim_skip_ws_comments(src, len, i + kw_len);
      if (range_has_newline(src, i + kw_len, name_start)) return true;
      i = skip_type_like_decl(src, len, i);
      continue;
    }
    if (skim_word_at(src, len, i, "namespace") || skim_word_at(src, len, i, "module")) {
      size_t kw_len = skim_word_at(src, len, i, "namespace") ? 9 : 6;
      size_t j = skim_skip_ws_comments(src, len, i + kw_len);
      if (range_has_newline(src, i + kw_len, j)) return true;
      size_t name_start = 0, name_end = 0;
      j = skim_parse_identifier(src, len, j, &name_start, &name_end);
      if (name_start == name_end) return true;
      j = skim_skip_ws_comments(src, len, j);
      while (j < end && src[j] == '.') {
        j = skim_skip_ws_comments(src, len, j + 1);
        j = skim_parse_identifier(src, len, j, &name_start, &name_end);
        if (name_start == name_end) return true;
        j = skim_skip_ws_comments(src, len, j);
      }
      if (j >= end || src[j] != '{') return true;
      size_t close = skim_skip_balanced(src, len, j, '{', '}');
      if (namespace_source_has_runtime_code(src, len, j + 1, close > 0 ? close - 1 : close)) return true;
      i = close;
      if (i < end && src[i] == ';') i++;
      continue;
    }
    if (item != i && skim_word_at(src, len, i, "type")) {
      i = skip_type_like_decl(src, len, i);
      continue;
    }
    return true;
  }
  return false;
}

static bool future_runtime_namespace_decl(const char *src, size_t len, size_t start, const char *name) {
  size_t name_len = strlen(name);
  for (size_t i = start; i < len;) {
    if (src[i] == '\'' || src[i] == '"' || src[i] == '`') {
      i = skim_skip_string_raw(src, len, i);
      continue;
    }
    if (i + 1 < len && src[i] == '/' && src[i + 1] == '/') {
      i += 2;
      while (i < len && src[i] != '\n')
        i++;
      continue;
    }
    if (i + 1 < len && src[i] == '/' && src[i + 1] == '*') {
      i += 2;
      while (i + 1 < len && !(src[i] == '*' && src[i + 1] == '/'))
        i++;
      if (i + 1 < len) i += 2;
      continue;
    }
    size_t item = i;
    if (skim_word_at(src, len, item, "export")) item = skim_skip_ws_comments(src, len, item + 6);
    if (skim_word_at(src, len, item, "namespace") || skim_word_at(src, len, item, "module")) {
      size_t kw_len = skim_word_at(src, len, item, "namespace") ? 9 : 6;
      size_t name_start = 0, name_end = 0;
      size_t after_name = skim_parse_identifier(src, len, item + kw_len, &name_start, &name_end);
      if (name_end - name_start == name_len && memcmp(src + name_start, name, name_len) == 0) {
        size_t body_open = skim_skip_ws_comments(src, len, after_name);
        if (body_open < len && src[body_open] == '{') {
          size_t body_close = skim_skip_balanced(src, len, body_open, '{', '}');
          if (namespace_source_has_runtime_code(src, len, body_open + 1, body_close > 0 ? body_close - 1 : body_close))
            return true;
          i = body_close;
          continue;
        }
      }
    }
    i++;
  }
  return false;
}

static bool transform_export_const(skim_str_t *out, const char *ns, const char *src, size_t len, size_t *io) {
  size_t i = *io;
  if (!skim_word_at(src, len, i, "export")) return false;
  size_t j = skim_skip_ws_comments(src, len, i + 6);
  const char *kind = NULL;
  if (skim_word_at(src, len, j, "const")) kind = "const";
  else if (skim_word_at(src, len, j, "let")) kind = "let";
  else if (skim_word_at(src, len, j, "var")) kind = "var";
  if (!kind) return false;

  size_t name_start = 0, name_end = 0;
  size_t after_name = skim_parse_identifier(src, len, j + strlen(kind), &name_start, &name_end);
  size_t eq = skim_skip_ws_comments(src, len, after_name);
  if (name_start == name_end || eq >= len || src[eq] != '=') return false;
  size_t value_start = skim_skip_ws_comments(src, len, eq + 1);
  size_t end = skim_skip_statement_like(src, len, value_start);
  size_t value_end = end;
  if (value_end > value_start && src[value_end - 1] == ';') value_end--;

  skim_str_puts(out, kind);
  skim_str_putc(out, ' ');
  skim_str_putn(out, src + name_start, name_end - name_start);
  skim_str_puts(out, " = ");
  skim_str_puts(out, ns);
  skim_str_putc(out, '.');
  skim_str_putn(out, src + name_start, name_end - name_start);
  skim_str_puts(out, " = ");
  skim_str_putn(out, src + value_start, value_end - value_start);
  skim_str_puts(out, ";\n");
  *io = end;
  return true;
}

static bool transform_export_import_equals(skim_str_t *out, const char *ns, const char *src, size_t len, size_t *io) {
  size_t i = *io;
  if (!skim_word_at(src, len, i, "export")) return false;
  size_t imp = skim_skip_ws_comments(src, len, i + 6);
  if (!skim_word_at(src, len, imp, "import")) return false;
  size_t name_start = 0, name_end = 0;
  size_t after_name = skim_parse_identifier(src, len, imp + 6, &name_start, &name_end);
  size_t eq = skim_skip_ws_comments(src, len, after_name);
  if (name_start == name_end || eq >= len || src[eq] != '=') return false;
  size_t rhs = skim_skip_ws_comments(src, len, eq + 1);
  size_t end = rhs;
  while (end < len && src[end] != ';' && src[end] != '\n' && src[end] != '\r')
    end++;
  size_t rhs_end = end;
  while (rhs_end > rhs && (src[rhs_end - 1] == ' ' || src[rhs_end - 1] == '\t'))
    rhs_end--;
  if (end < len && src[end] == ';') end++;

  skim_str_puts(out, "var ");
  skim_str_putn(out, src + name_start, name_end - name_start);
  skim_str_puts(out, " = ");
  skim_str_putn(out, src + rhs, rhs_end - rhs);
  skim_str_puts(out, ";\n");
  skim_str_puts(out, ns);
  skim_str_putc(out, '.');
  skim_str_putn(out, src + name_start, name_end - name_start);
  skim_str_puts(out, " = ");
  skim_str_putn(out, src + name_start, name_end - name_start);
  skim_str_puts(out, ";\n");
  *io = end;
  return true;
}

static bool prior_namespace_decl(const char *src, size_t pos, const char *name) {
  return skim_decl_seen_before(src, name, pos, SKIM_DECL_NAMESPACE);
}

static bool prior_enum_decl(const char *src, size_t pos, const char *name) {
  return skim_decl_seen_before(src, name, pos, SKIM_DECL_ENUM);
}

static bool prior_value_decl(const char *src, size_t pos, const char *name) {
  return skim_decl_seen_before(src, name, pos, SKIM_DECL_VALUE);
}

static bool local_value_decl_before(const char *src, size_t len, size_t pos, const char *name) {
  static const char *decls[] = {"class", "function", "var", "let", "const"};
  size_t name_len = strlen(name);
  for (size_t i = 0; i < pos && i < len; i++) {
    for (size_t d = 0; d < sizeof(decls) / sizeof(decls[0]); d++) {
      const char *kw = decls[d];
      if (!skim_word_at(src, len, i, kw)) continue;
      size_t name_start = 0, name_end = 0;
      skim_parse_identifier(src, len, i + strlen(kw), &name_start, &name_end);
      if (name_end - name_start == name_len && memcmp(src + name_start, name, name_len) == 0) return true;
    }
  }
  return false;
}

static void
transform_namespace_body(skim_str_t *out, const char *ns_param, const char *src, size_t len, size_t start, size_t end);

static bool transform_export_function(skim_str_t *out, const char *ns, const char *src, size_t len, size_t *io) {
  size_t i = *io;
  if (!skim_word_at(src, len, i, "export")) return false;
  size_t fn = skim_skip_ws_comments(src, len, i + 6);
  bool is_async = false;
  if (skim_word_at(src, len, fn, "async")) {
    is_async = true;
    fn = skim_skip_ws_comments(src, len, fn + 5);
  }
  if (!skim_word_at(src, len, fn, "function")) return false;

  size_t name_start = 0, name_end = 0;
  size_t after_name = skim_parse_identifier(src, len, fn + 8, &name_start, &name_end);
  if (name_start == name_end) return false;
  size_t body_open = after_name;
  while (body_open < len && src[body_open] != '{') {
    if (src[body_open] == '\'' || src[body_open] == '"' || src[body_open] == '`') {
      body_open = skim_skip_string_raw(src, len, body_open);
    } else if (src[body_open] == ';') {
      skim_emit_preserved_newlines(out, src, i, body_open + 1);
      *io = body_open + 1;
      return true;
    } else body_open++;
  }
  if (body_open >= len) return false;
  size_t body_close = skim_skip_balanced(src, len, body_open, '{', '}');

  if (is_async) skim_str_puts(out, "async ");
  skim_transform_range(src, len, fn, body_close, out);
  skim_str_puts(out, "\n");
  skim_str_puts(out, ns);
  skim_str_putc(out, '.');
  skim_str_putn(out, src + name_start, name_end - name_start);
  skim_str_puts(out, " = ");
  skim_str_putn(out, src + name_start, name_end - name_start);
  skim_str_puts(out, ";\n");
  *io = body_close;
  return true;
}

static bool transform_export_class(skim_str_t *out, const char *ns, const char *src, size_t len, size_t *io) {
  size_t i = *io;
  if (!skim_word_at(src, len, i, "export")) return false;
  size_t cls = skim_skip_ws_comments(src, len, i + 6);
  if (skim_word_at(src, len, cls, "abstract")) cls = skim_skip_ws_comments(src, len, cls + 8);
  if (!skim_word_at(src, len, cls, "class")) return false;

  size_t name_start = 0, name_end = 0;
  size_t after_name = skim_parse_identifier(src, len, cls + 5, &name_start, &name_end);
  if (name_start == name_end) return false;
  size_t body_open = after_name;
  while (body_open < len && src[body_open] != '{') {
    if (src[body_open] == '\'' || src[body_open] == '"' || src[body_open] == '`')
      body_open = skim_skip_string_raw(src, len, body_open);
    else body_open++;
  }
  if (body_open >= len) return false;
  size_t body_close = skim_skip_balanced(src, len, body_open, '{', '}');

  skim_transform_range(src, len, cls, body_close, out);
  skim_str_puts(out, "\n");
  skim_str_puts(out, ns);
  skim_str_putc(out, '.');
  skim_str_putn(out, src + name_start, name_end - name_start);
  skim_str_puts(out, " = ");
  skim_str_putn(out, src + name_start, name_end - name_start);
  skim_str_puts(out, ";\n");
  *io = body_close;
  return true;
}

static bool transform_export_enum(skim_str_t *out, const char *ns, const char *src, size_t len, size_t *io) {
  size_t i = *io;
  if (!skim_word_at(src, len, i, "export")) return false;
  size_t e = skim_skip_ws_comments(src, len, i + 6);
  if (skim_word_at(src, len, e, "const")) e = skim_skip_ws_comments(src, len, e + 5);
  if (!skim_word_at(src, len, e, "enum")) return false;

  size_t name_start = 0, name_end = 0;
  skim_parse_identifier(src, len, e + 4, &name_start, &name_end);
  if (name_start == name_end) return false;

  skim_str_t tmp = {0};
  size_t enum_io = e;
  if (!skim_ts_enum_try(&tmp, src, len, &enum_io)) {
    free(tmp.data);
    return false;
  }
  skim_str_putn(out, tmp.data, tmp.len);
  free(tmp.data);
  skim_str_puts(out, ns);
  skim_str_putc(out, '.');
  skim_str_putn(out, src + name_start, name_end - name_start);
  skim_str_puts(out, " = ");
  skim_str_putn(out, src + name_start, name_end - name_start);
  skim_str_puts(out, ";\n");
  *io = enum_io;
  return true;
}

static bool transform_export_namespace(skim_str_t *out, const char *ns, const char *src, size_t len, size_t *io) {
  size_t i = *io;
  if (!skim_word_at(src, len, i, "export")) return false;
  size_t kw = skim_skip_ws_comments(src, len, i + 6);
  bool is_namespace = skim_word_at(src, len, kw, "namespace");
  bool is_module = skim_word_at(src, len, kw, "module");
  if (!is_namespace && !is_module) return false;

  size_t name_start = 0, name_end = 0;
  size_t after_name = skim_parse_identifier(src, len, kw + (is_namespace ? 9 : 6), &name_start, &name_end);
  if (name_start == name_end) return false;
  char *name = skim_slice_dup(src, name_start, name_end);
  size_t body_open = skim_skip_ws_comments(src, len, after_name);
  if (body_open < len && src[body_open] == '.') {
    char **parts = NULL;
    size_t part_count = 0;
    parts = realloc(parts, sizeof(*parts) * (part_count + 1));
    if (!parts) skim_die("out of memory");
    parts[part_count++] = skim_slice_dup(name, 0, strlen(name));
    size_t chain_i = body_open;
    while (chain_i < len && src[chain_i] == '.') {
      size_t part_start = 0, part_end = 0;
      chain_i = skim_parse_identifier(src, len, chain_i + 1, &part_start, &part_end);
      if (part_start == part_end) break;
      char **next = realloc(parts, sizeof(*parts) * (part_count + 1));
      if (!next) skim_die("out of memory");
      parts = next;
      parts[part_count++] = skim_slice_dup(src, part_start, part_end);
      chain_i = skim_skip_ws_comments(src, len, chain_i);
    }
    if (part_count >= 2 && chain_i < len && src[chain_i] == '{') {
      size_t body_start = chain_i + 1;
      size_t body_end = skim_skip_balanced(src, len, chain_i, '{', '}') - 1;
      skim_str_t synthetic = {0};
      for (size_t p = 0; p < part_count; p++) {
        skim_str_puts(&synthetic, p == 0 ? "namespace " : "export namespace ");
        skim_str_puts(&synthetic, parts[p]);
        skim_str_puts(&synthetic, " { ");
      }
      skim_str_putn(&synthetic, src + body_start, body_end - body_start);
      for (size_t p = 0; p < part_count; p++)
        skim_str_puts(&synthetic, " }");
      char *lowered = skim_transform_typescript(synthetic.data, synthetic.len);
      skim_str_puts(out, lowered);
      skim_str_puts(out, ns);
      skim_str_putc(out, '.');
      skim_str_puts(out, name);
      skim_str_puts(out, " = ");
      skim_str_puts(out, name);
      skim_str_puts(out, ";\n");
      free(lowered);
      free(synthetic.data);
      *io = body_end + 1;
      if (*io < len && src[*io] == '}') (*io)++;
      if (*io < len && src[*io] == ';') (*io)++;
      for (size_t p = 0; p < part_count; p++)
        free(parts[p]);
      free(parts);
      free(name);
      return true;
    }
    for (size_t p = 0; p < part_count; p++)
      free(parts[p]);
    free(parts);
  }
  if (body_open >= len || src[body_open] != '{') {
    free(name);
    return false;
  }
  size_t body_start = body_open + 1;
  size_t body_end = skim_skip_balanced(src, len, body_open, '{', '}') - 1;
  char *param = make_namespace_param(name);

  skim_str_t body = {0};
  transform_namespace_body(&body, param, src, len, body_start, body_end);
  bool has_body = namespace_source_has_runtime_code(src, len, body_start, body_end) &&
                  skim_has_effective_code(body.data ? body.data : "", body.len);
  terminate_namespace_body(&body);

  if (!has_body) {
    free(body.data);
    free(param);
    free(name);
    *io = body_end + 1;
    if (*io < len && src[*io] == '}') (*io)++;
    if (*io < len && src[*io] == ';') (*io)++;
    return true;
  }

  if (!local_value_decl_before(src, len, i, name)) {
    skim_str_puts(out, "let ");
    skim_str_puts(out, name);
    skim_str_puts(out, ";\n");
  }
  if (has_body) {
    skim_str_puts(out, "(function (");
    skim_str_puts(out, param);
    skim_str_puts(out, ") {\n");
    skim_str_putn(out, body.data, body.len);
    skim_str_puts(out, "\n})(");
    skim_str_puts(out, name);
    skim_str_puts(out, " || (");
    skim_str_puts(out, name);
    skim_str_puts(out, " = ");
    skim_str_puts(out, ns);
    skim_str_putc(out, '.');
    skim_str_puts(out, name);
    skim_str_puts(out, " || (");
    skim_str_puts(out, ns);
    skim_str_putc(out, '.');
    skim_str_puts(out, name);
    skim_str_puts(out, " = {})));\n");
  }

  free(body.data);
  free(param);
  free(name);
  *io = body_end + 1;
  if (*io < len && src[*io] == '}') (*io)++;
  if (*io < len && src[*io] == ';') (*io)++;
  return true;
}

static void
transform_namespace_body(skim_str_t *out, const char *ns_param, const char *src, size_t len, size_t start, size_t end) {
  for (size_t i = start; i < end;) {
    if (src[i] == '\'' || src[i] == '"' || src[i] == '`') {
      i = skim_copy_string(out, src, end, i);
      continue;
    }
    if (i + 1 < len && src[i] == '/' && src[i + 1] == '/') {
      i = skim_copy_line_comment(out, src, end, i);
      continue;
    }
    if (i + 1 < len && src[i] == '/' && src[i + 1] == '*') {
      i = skim_copy_block_comment(out, src, end, i);
      continue;
    }
    if (src[i] == 'e' && transform_export_import_equals(out, ns_param, src, end, &i)) continue;
    if (src[i] == 'e' && transform_export_namespace(out, ns_param, src, end, &i)) continue;
    if (src[i] == 'e' && transform_export_enum(out, ns_param, src, end, &i)) continue;
    if (src[i] == 'e' && transform_export_function(out, ns_param, src, end, &i)) continue;
    if (src[i] == 'e' && transform_export_class(out, ns_param, src, end, &i)) continue;
    if (src[i] == 'e' && transform_export_const(out, ns_param, src, end, &i)) continue;
    if (skim_transform_try_at(out, src, end, &i)) continue;
    if (isdigit((unsigned char)src[i])) {
      size_t j = i;
      while (j < end && isdigit((unsigned char)src[j]))
        j++;
      if (j + 1 < end && src[j] == '.' && src[j + 1] == ':') {
        skim_str_putn(out, src + i, j - i);
        i = j + 1;
        continue;
      }
    }
    if (skim_emit_copy_plain_identifier(out, src, end, &i)) continue;
    if (skim_emit_copy_plain_span(out, src, end, &i)) continue;
    skim_str_putc(out, src[i++]);
  }
}

bool skim_ts_namespace_try(skim_str_t *out, const char *src, size_t len, size_t *io) {
  size_t i = *io;
  bool is_export = false;
  if (skim_word_at(src, len, i, "export")) {
    size_t j = skim_skip_ws_comments(src, len, i + 6);
    if (!skim_word_at(src, len, j, "namespace") && !skim_word_at(src, len, j, "module")) return false;
    is_export = true;
    i = j;
  } else if (!skim_word_at(src, len, i, "namespace") && !skim_word_at(src, len, i, "module")) {
    return false;
  }

  size_t name_start = 0, name_end = 0;
  size_t after_keyword = i + (skim_word_at(src, len, i, "namespace") ? 9 : 6);
  i = skim_parse_identifier(src, len, after_keyword, &name_start, &name_end);
  if (name_start == name_end) return false;
  if (range_has_newline(src, after_keyword, name_start)) return false;
  if (skim_word_at(src, len, *io, "module") && name_end - name_start == 2 && memcmp(src + name_start, "in", 2) == 0)
    return false;
  char *name = skim_slice_dup(src, name_start, name_end);
  i = skim_skip_ws_comments(src, len, i);
  if (i < len && src[i] == '.') {
    char **parts = NULL;
    size_t part_count = 0;
    parts = realloc(parts, sizeof(*parts) * (part_count + 1));
    if (!parts) skim_die("out of memory");
    parts[part_count++] = skim_slice_dup(name, 0, strlen(name));
    size_t chain_i = i;
    while (chain_i < len && src[chain_i] == '.') {
      size_t part_start = 0, part_end = 0;
      chain_i = skim_parse_identifier(src, len, chain_i + 1, &part_start, &part_end);
      if (part_start == part_end) break;
      char **next = realloc(parts, sizeof(*parts) * (part_count + 1));
      if (!next) skim_die("out of memory");
      parts = next;
      parts[part_count++] = skim_slice_dup(src, part_start, part_end);
      chain_i = skim_skip_ws_comments(src, len, chain_i);
    }
    if (part_count >= 2 && chain_i < len && src[chain_i] == '{') {
      size_t body_start = chain_i + 1;
      size_t body_end = skim_skip_balanced(src, len, chain_i, '{', '}') - 1;
      skim_str_t synthetic = {0};
      for (size_t p = 0; p < part_count; p++) {
        if (p == 0) {
          if (is_export) skim_str_puts(&synthetic, "export ");
          skim_str_puts(&synthetic, "namespace ");
        } else {
          skim_str_puts(&synthetic, "export namespace ");
        }
        skim_str_puts(&synthetic, parts[p]);
        skim_str_puts(&synthetic, " { ");
      }
      skim_str_putn(&synthetic, src + body_start, body_end - body_start);
      for (size_t p = 0; p < part_count; p++)
        skim_str_puts(&synthetic, " }");
      char *lowered = skim_transform_typescript(synthetic.data, synthetic.len);
      skim_str_puts(out, lowered);
      free(lowered);
      free(synthetic.data);
      *io = body_end + 1;
      if (*io < len && src[*io] == '}') (*io)++;
      if (*io < len && src[*io] == ';') (*io)++;
      for (size_t p = 0; p < part_count; p++)
        free(parts[p]);
      free(parts);
      free(name);
      return true;
    }
    for (size_t p = 0; p < part_count; p++)
      free(parts[p]);
    free(parts);
    size_t inner_start = 0, inner_end = 0;
    i = skim_parse_identifier(src, len, i + 1, &inner_start, &inner_end);
    if (inner_start == inner_end) {
      free(name);
      return false;
    }
    char *inner = skim_slice_dup(src, inner_start, inner_end);
    i = skim_skip_ws_comments(src, len, i);
    if (i >= len || src[i] != '{') {
      free(inner);
      free(name);
      return false;
    }
    size_t body_start = i + 1;
    size_t body_end = skim_skip_balanced(src, len, i, '{', '}') - 1;

    skim_str_t body = {0};
    skim_ts_enum_suppress_push();
    transform_namespace_body(&body, "_NS", src, len, body_start, body_end);
    skim_ts_enum_suppress_pop();
    bool has_body = namespace_source_has_runtime_code(src, len, body_start, body_end) &&
                    skim_has_effective_code(body.data ? body.data : "", body.len);
    free(body.data);
    if (!is_export && !has_body) {
      *io = body_end + 1;
      if (*io < len && src[*io] == '}') (*io)++;
      if (*io < len && src[*io] == ';') (*io)++;
      free(inner);
      free(name);
      return true;
    }

    if (!has_body) {
      if (
        is_export && future_runtime_namespace_decl(src, len, body_end + 1, name) &&
        !prior_namespace_decl(src, *io, name) && !prior_enum_decl(src, *io, name) &&
        !prior_value_decl(src, *io, name) && !local_value_decl_before(src, len, *io, name)
      ) {
        skim_str_puts(out, "export let ");
        skim_str_puts(out, name);
        skim_str_puts(out, ";\n");
      }
      *io = body_end + 1;
      if (*io < len && src[*io] == '}') (*io)++;
      if (*io < len && src[*io] == ';') (*io)++;
      free(inner);
      free(name);
      return true;
    }
    if (
      !prior_namespace_decl(src, *io, name) && !prior_enum_decl(src, *io, name) && !prior_value_decl(src, *io, name) &&
      !local_value_decl_before(src, len, *io, name)
    ) {
      if (is_export) skim_str_puts(out, "export ");
      skim_str_puts(out, "let ");
      skim_str_puts(out, name);
      skim_str_puts(out, ";\n");
    }

    char *outer_param = make_namespace_param(name);
    char *inner_param = make_namespace_param(inner);
    body.data = NULL;
    body.len = 0;
    body.cap = 0;
    transform_namespace_body(&body, inner_param, src, len, body_start, body_end);
    terminate_namespace_body(&body);

    skim_str_puts(out, "(function (");
    skim_str_puts(out, outer_param);
    skim_str_puts(out, ") {\nlet ");
    skim_str_puts(out, inner);
    skim_str_puts(out, ";\n(function (");
    skim_str_puts(out, inner_param);
    skim_str_puts(out, ") {\n");
    skim_str_putn(out, body.data, body.len);
    skim_str_puts(out, "\n})(");
    skim_str_puts(out, inner);
    skim_str_puts(out, " || (");
    skim_str_puts(out, inner);
    skim_str_puts(out, " = ");
    skim_str_puts(out, outer_param);
    skim_str_putc(out, '.');
    skim_str_puts(out, inner);
    skim_str_puts(out, " || (");
    skim_str_puts(out, outer_param);
    skim_str_putc(out, '.');
    skim_str_puts(out, inner);
    skim_str_puts(out, " = {})));\n})(");
    skim_str_puts(out, name);
    skim_str_puts(out, " || (");
    skim_str_puts(out, name);
    skim_str_puts(out, " = {}));\n");

    free(body.data);
    free(outer_param);
    free(inner_param);
    *io = body_end + 1;
    if (*io < len && src[*io] == '}') (*io)++;
    if (*io < len && src[*io] == ';') (*io)++;
    free(inner);
    free(name);
    return true;
  }
  if (i >= len || src[i] != '{') {
    free(name);
    return false;
  }
  size_t body_start = i + 1;
  size_t body_end = skim_skip_balanced(src, len, i, '{', '}') - 1;

  skim_str_t body = {0};
  skim_ts_enum_suppress_push();
  transform_namespace_body(&body, "_NS", src, len, body_start, body_end);
  skim_ts_enum_suppress_pop();
  bool has_body = namespace_source_has_runtime_code(src, len, body_start, body_end) &&
                  skim_has_effective_code(body.data ? body.data : "", body.len);
  bool has_ambient_decl = namespace_body_has_ambient_decl(src, len, body_start, body_end);

  if (!has_body && !has_ambient_decl) {
    if (
      is_export && future_runtime_namespace_decl(src, len, body_end + 1, name) &&
      !prior_namespace_decl(src, *io, name) && !prior_enum_decl(src, *io, name) && !prior_value_decl(src, *io, name) &&
      !local_value_decl_before(src, len, *io, name)
    ) {
      skim_str_puts(out, "export let ");
      skim_str_puts(out, name);
      skim_str_puts(out, ";\n");
    }
    free(body.data);
    *io = body_end + 1;
    if (*io < len && src[*io] == '}') (*io)++;
    if (*io < len && src[*io] == ';') (*io)++;
    free(name);
    return true;
  }

  if (
    !prior_namespace_decl(src, *io, name) && !prior_enum_decl(src, *io, name) && !prior_value_decl(src, *io, name) &&
    !local_value_decl_before(src, len, *io, name)
  ) {
    if (is_export) skim_str_puts(out, "export ");
    skim_str_puts(out, "let ");
    skim_str_puts(out, name);
    skim_str_puts(out, ";\n");
  }
  if (!has_body && !has_ambient_decl) {
    free(body.data);
    *io = body_end + 1;
    if (*io < len && src[*io] == '}') (*io)++;
    if (*io < len && src[*io] == ';') (*io)++;
    free(name);
    return true;
  }

  free(body.data);
  body.data = NULL;
  body.len = 0;
  body.cap = 0;
  char *param = make_namespace_param(name);
  transform_namespace_body(&body, param, src, len, body_start, body_end);
  terminate_namespace_body(&body);

  skim_str_puts(out, "(function (");
  skim_str_puts(out, param);
  skim_str_puts(out, ") {\n");
  skim_str_putn(out, body.data, body.len);
  free(body.data);
  free(param);
  skim_str_puts(out, "\n})(");
  skim_str_puts(out, name);
  skim_str_puts(out, " || (");
  skim_str_puts(out, name);
  skim_str_puts(out, " = {}));\n");

  *io = body_end + 1;
  if (*io < len && src[*io] == '}') (*io)++;
  if (*io < len && src[*io] == ';') (*io)++;
  free(name);
  return true;
}
