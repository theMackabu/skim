#include "chars.h"
#include "class_internal.h"
#include "emit.h"
#include "skip.h"
#include "str.h"
#include "typescript.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
  char *name;
  char *param;
  bool is_param_property;
} param_t;

typedef struct {
  bool is_export;
  bool is_default;
  bool has_decorators;
  size_t decorators_start;
  size_t decorators_end;
} class_prefix_t;

static bool modifier_at(const char *src, size_t len, size_t i, const char **word, size_t *word_len) {
  static const char *mods[] = {"public", "private", "protected", "readonly", "override"};
  for (size_t k = 0; k < sizeof(mods) / sizeof(mods[0]); k++) {
    if (skim_word_at(src, len, i, mods[k])) {
      *word = mods[k];
      *word_len = strlen(mods[k]);
      return true;
    }
  }
  return false;
}

static void emit_class_header(
  skim_str_t *out,
  const char *src,
  size_t len,
  size_t class_word,
  size_t body_open,
  const class_prefix_t *prefix
) {
  if (prefix->is_export) skim_str_puts(out, "export ");
  if (prefix->is_default) skim_str_puts(out, "default ");
  if (prefix->has_decorators) {
    skim_class_emit_decorator_range(out, src, len, prefix->decorators_start, prefix->decorators_end);
    skim_str_putc(out, ' ');
  }
  skim_str_putn(out, src + class_word, 5);
  for (size_t i = class_word + 5; i <= body_open;) {
    if (i < body_open && skim_word_at(src, len, i, "extends")) {
      size_t expr = skim_skip_ws_comments(src, len, i + 7);
      if (expr < body_open && src[expr] == '(') {
        size_t close = skim_skip_balanced(src, len, expr, '(', ')');
        size_t after = skim_skip_ws_comments(src, len, close);
        size_t inner = skim_skip_ws_comments(src, len, expr + 1);
        if (
          close <= body_open && after == body_open && !skim_word_at(src, len, inner, "await") &&
          !skim_word_at(src, len, inner, "yield")
        ) {
          skim_str_puts(out, "extends ");
          skim_transform_range(src, len, expr + 1, close - 1, out);
          i = close;
          continue;
        }
      }
    }
    if (i < body_open && src[i] == '<') {
      i = skim_class_skip_angle_type_list(src, len, i);
      continue;
    }
    if (skim_transform_try_at(out, src, len, &i)) continue;
    if (skim_emit_copy_plain_identifier(out, src, body_open + 1, &i)) continue;
    if (skim_emit_copy_plain_span(out, src, body_open + 1, &i)) continue;
    skim_str_putc(out, src[i++]);
  }
}

static void emit_normal_class(
  skim_str_t *out,
  const char *src,
  size_t len,
  size_t class_word,
  size_t body_open,
  size_t body_close,
  const class_prefix_t *prefix
) {
  emit_class_header(out, src, len, class_word, body_open, prefix);
  skim_class_transform_body_range(src, len, body_open + 1, body_close, out);
  skim_str_putc(out, src[body_close]);
}

static size_t find_class_body_open(const char *src, size_t len, size_t i) {
  int paren = 0, bracket = 0;
  while (i < len) {
    char c = src[i];
    if (c == '\'' || c == '"' || c == '`') {
      i = skim_skip_string_raw(src, len, i);
      continue;
    }
    if (skim_skip_comment(src, len, &i)) continue;
    if (c == '(') paren++;
    else if (c == ')' && paren > 0) paren--;
    else if (c == '[') bracket++;
    else if (c == ']' && bracket > 0) bracket--;
    else if (c == '<') {
      i = skim_class_skip_angle_type_list(src, len, i);
      continue;
    } else if (c == '{' && paren == 0 && bracket == 0) return i;
    i++;
  }
  return i;
}

static void
split_params(const char *src, size_t len, size_t start, size_t end, param_t **out_params, size_t *out_count) {
  param_t *params = NULL;
  size_t count = 0;
  size_t item_start = start;
  int paren = 0, bracket = 0, brace = 0, angle = 0;
  for (size_t i = start; i <= end; i++) {
    char c = i < end ? src[i] : ',';
    if (i < end && (c == '\'' || c == '"' || c == '`')) {
      i = skim_skip_string_raw(src, len, i) - 1;
      continue;
    }
    if (c == '(') paren++;
    else if (c == ')' && paren > 0) paren--;
    else if (c == '[') bracket++;
    else if (c == ']' && bracket > 0) bracket--;
    else if (c == '{') brace++;
    else if (c == '}' && brace > 0) brace--;
    else if (c == '<') angle++;
    else if (c == '>' && angle > 0) angle--;
    if (paren || bracket || brace || angle || c != ',') continue;

    size_t a = skim_skip_ws(src, len, item_start);
    size_t b = i;
    while (b > a && (src[b - 1] == ' ' || src[b - 1] == '\t' || src[b - 1] == '\n'))
      b--;
    if (a < b) {
      bool is_prop = false;
      const char *mod = NULL;
      size_t mod_len = 0;
      size_t decorators_start = a;
      size_t decorators_end = a;
      for (;;) {
        a = skim_skip_ws(src, len, a);
        if (a >= b || src[a] != '@') break;
        size_t after = skim_class_skip_decorator_line(src, len, a);
        if (after <= a || after > b) break;
        decorators_end = after;
        a = after;
      }
      a = skim_skip_ws(src, len, a);
      for (;;) {
        size_t m = skim_skip_ws(src, len, a);
        if (!modifier_at(src, len, m, &mod, &mod_len)) break;
        is_prop = true;
        a = m + mod_len;
      }

      size_t name_start = 0, name_end = 0;
      size_t after_name = skim_parse_identifier(src, len, a, &name_start, &name_end);
      if (name_start != name_end) {
        size_t tail = after_name;
        if (tail < b && src[tail] == '?') tail++;
        size_t colon = skim_skip_ws(src, len, tail);
        size_t init = b;
        if (colon < b && src[colon] == ':') {
          init = colon + 1;
          int init_angle = 0;
          while (init < b) {
            if (src[init] == '<') init_angle++;
            else if (src[init] == '>' && init_angle > 0) init_angle--;
            else if (src[init] == '=' && init_angle == 0) {
              if (init + 1 < b && src[init + 1] == '>') {
                init += 2;
                continue;
              }
              break;
            }
            init++;
          }
        } else {
          init = skim_skip_ws(src, len, tail);
        }

        skim_str_t param = {0};
        if (decorators_end > decorators_start) {
          skim_class_emit_decorator_range(&param, src, len, decorators_start, decorators_end);
          skim_str_putc(&param, ' ');
        }
        skim_str_putn(&param, src + name_start, name_end - name_start);
        if (init < b && src[init] == '=') {
          skim_str_putc(&param, ' ');
          skim_str_putn(&param, src + init, b - init);
        }

        params = realloc(params, sizeof(param_t) * (count + 1));
        if (!params) skim_die("out of memory");
        params[count].name = skim_slice_dup(src, name_start, name_end);
        params[count].param = param.data;
        params[count].is_param_property = is_prop;
        count++;
      }
    }
    item_start = i + 1;
  }
  *out_params = params;
  *out_count = count;
}

static void free_params(param_t *params, size_t count) {
  for (size_t i = 0; i < count; i++) {
    free(params[i].name);
    free(params[i].param);
  }
  free(params);
}

static size_t find_after_top_level_super(const char *src, size_t len, size_t start, size_t end) {
  int brace = 0, paren = 0, bracket = 0;
  for (size_t i = start; i < end;) {
    if (src[i] == '\'' || src[i] == '"' || src[i] == '`') {
      i = skim_skip_string_raw(src, len, i);
      continue;
    }
    if (i + 1 < len && src[i] == '/' && src[i + 1] == '/') {
      i += 2;
      while (i < end && src[i] != '\n')
        i++;
      continue;
    }
    if (i + 1 < len && src[i] == '/' && src[i + 1] == '*') {
      i += 2;
      while (i + 1 < end && !(src[i] == '*' && src[i + 1] == '/'))
        i++;
      if (i + 1 < end) i += 2;
      continue;
    }
    if (brace == 0 && paren == 0 && bracket == 0 && skim_word_at(src, len, i, "super")) {
      size_t call = skim_skip_ws_comments(src, len, i + 5);
      if (call < end && src[call] == '(') {
        size_t after = skim_skip_balanced(src, len, call, '(', ')');
        after = skim_skip_ws_comments(src, len, after);
        if (after < end && src[after] == ';') after++;
        return after;
      }
    }
    if (src[i] == '{') brace++;
    else if (src[i] == '}' && brace > 0) brace--;
    else if (src[i] == '(') paren++;
    else if (src[i] == ')' && paren > 0) paren--;
    else if (src[i] == '[') bracket++;
    else if (src[i] == ']' && bracket > 0) bracket--;
    i++;
  }
  return start;
}

static bool range_contains(const char *src, size_t start, size_t end, const char *needle) {
  size_t n = strlen(needle);
  if (n == 0 || end < start || end - start < n) return false;
  for (size_t i = start; i + n <= end; i++) {
    if (memcmp(src + i, needle, n) == 0) return true;
  }
  return false;
}

static bool scan_class_decorators(const char *src, size_t len, size_t *io, class_prefix_t *prefix) {
  size_t j = skim_skip_ws_comments(src, len, *io);
  if (j >= len || src[j] != '@') return false;
  prefix->has_decorators = true;
  prefix->decorators_start = j;
  for (;;) {
    j = skim_skip_ws_comments(src, len, j);
    if (j >= len || src[j] != '@') break;
    size_t after = skim_class_skip_decorator_line(src, len, j);
    if (after <= j) break;
    prefix->decorators_end = after;
    j = after;
  }
  *io = j;
  return true;
}

bool skim_ts_class_try(skim_str_t *out, const char *src, size_t len, size_t *io) {
  size_t i = *io;
  if (!skim_word_at(src, len, i, "class") && !skim_word_at(src, len, i, "export") && src[i] != '@') return false;

  size_t class_word = i;
  class_prefix_t prefix = {0};
  size_t j = i;
  scan_class_decorators(src, len, &j, &prefix);
  j = skim_skip_ws_comments(src, len, j);
  if (skim_word_at(src, len, j, "export")) {
    prefix.is_export = true;
    j = skim_skip_ws_comments(src, len, j + 6);
    if (skim_word_at(src, len, j, "default")) {
      prefix.is_default = true;
      j = skim_skip_ws_comments(src, len, j + 7);
    }
  }
  if (!prefix.has_decorators) scan_class_decorators(src, len, &j, &prefix);
  j = skim_skip_ws_comments(src, len, j);
  if (skim_word_at(src, len, j, "abstract")) j = skim_skip_ws_comments(src, len, j + 8);
  if (!skim_word_at(src, len, j, "class")) return false;
  class_word = j;

  size_t body_open = find_class_body_open(src, len, class_word);
  if (body_open >= len) return false;
  if (range_contains(src, class_word, body_open, "extends class")) return false;
  size_t body_close = skim_skip_balanced(src, len, body_open, '{', '}') - 1;

  size_t ctor = body_open + 1;
  size_t params_open = 0;
  size_t params_close = 0;
  size_t ctor_body_open = 0;
  size_t ctor_member_start = 0;
  bool found = false;
  while (ctor < body_close) {
    char ctor_char = src[ctor];
    if (skim_is_space(ctor_char) || ctor_char == '/') {
      size_t next = skim_skip_ws_comments(src, body_close, ctor);
      if (next != ctor) {
        ctor = next;
        continue;
      }
      if (ctor_char == '/' && skim_slash_starts_regex(src, body_open, ctor)) {
        ctor = skim_skip_regex_literal(src, body_close, ctor);
        continue;
      }
    }
    if (src[ctor] == '\'' || src[ctor] == '"' || src[ctor] == '`') {
      ctor = skim_skip_string_raw(src, len, ctor);
      continue;
    }
    if (skim_word_at(src, len, ctor, "constructor")) {
      ctor_member_start = ctor;
      size_t line_start = ctor;
      while (line_start > body_open + 1 && src[line_start - 1] != '\n' && src[line_start - 1] != '\r')
        line_start--;
      size_t maybe_modifier = skim_skip_ws_comments(src, len, line_start);
      if (maybe_modifier < ctor) {
        const char *mod = NULL;
        size_t mod_len = 0;
        bool is_abstract = false;
        size_t after_mods = maybe_modifier;
        bool saw_ctor_modifier = false;
        for (;;) {
          after_mods = skim_skip_ws_comments(src, len, after_mods);
          if (!skim_class_member_modifier_at(src, len, after_mods, &mod, &mod_len, &is_abstract)) break;
          if (strcmp(mod, "static") == 0 || strcmp(mod, "accessor") == 0) break;
          saw_ctor_modifier = true;
          after_mods += mod_len;
        }
        after_mods = skim_skip_ws_comments(src, len, after_mods);
        if (saw_ctor_modifier && after_mods == ctor) ctor_member_start = maybe_modifier;
      }
      params_open = skim_skip_ws_comments(src, len, ctor + 11);
      if (params_open < len && src[params_open] == '(') {
        params_close = skim_skip_balanced(src, len, params_open, '(', ')') - 1;
        ctor_body_open = skim_skip_ws_comments(src, len, params_close + 1);
        if (ctor_body_open < len && src[ctor_body_open] == ':') {
          ctor_body_open = skim_skip_ws_comments(src, len, skim_class_skip_type_tail(src, len, ctor_body_open + 1));
        }
        if (ctor_body_open < len && src[ctor_body_open] == '{') {
          found = true;
          break;
        }
        if (ctor_body_open < len && src[ctor_body_open] == ';') {
          ctor = ctor_body_open + 1;
          continue;
        }
      }
    }
    ctor++;
  }
  if (!found) {
    emit_normal_class(out, src, len, class_word, body_open, body_close, &prefix);
    *io = body_close + 1;
    if (*io < len && src[*io] == '}') (*io)++;
    return true;
  }
  if (params_open >= len || src[params_open] != '(') return false;
  if (ctor_body_open >= len || src[ctor_body_open] != '{') return false;
  size_t ctor_body_close = skim_skip_balanced(src, len, ctor_body_open, '{', '}') - 1;

  param_t *params = NULL;
  size_t param_count = 0;
  split_params(src, len, params_open + 1, params_close, &params, &param_count);
  bool has_prop = false;
  for (size_t p = 0; p < param_count; p++)
    has_prop = has_prop || params[p].is_param_property;
  if (!has_prop) {
    free_params(params, param_count);
    emit_normal_class(out, src, len, class_word, body_open, body_close, &prefix);
    *io = body_close + 1;
    if (*io < len && src[*io] == '}') (*io)++;
    return true;
  }

  emit_class_header(out, src, len, class_word, body_open, &prefix);
  skim_str_putc(out, '\n');
  for (size_t p = 0; p < param_count; p++) {
    if (!params[p].is_param_property) continue;
    skim_str_puts(out, "  ");
    skim_str_puts(out, params[p].name);
    skim_str_puts(out, ";\n");
  }
  skim_class_transform_body_range(src, len, body_open + 1, ctor_member_start ? ctor_member_start : ctor, out);
  skim_str_puts(out, "constructor(");
  for (size_t p = 0; p < param_count; p++) {
    if (p) skim_str_puts(out, ", ");
    skim_str_puts(out, params[p].param);
  }
  skim_str_puts(out, ") {");
  skim_str_putc(out, '\n');
  size_t assignment_pos = find_after_top_level_super(src, len, ctor_body_open + 1, ctor_body_close);
  skim_transform_range(src, len, ctor_body_open + 1, assignment_pos, out);
  for (size_t p = 0; p < param_count; p++) {
    if (!params[p].is_param_property) continue;
    skim_str_puts(out, "    this.");
    skim_str_puts(out, params[p].name);
    skim_str_puts(out, " = ");
    skim_str_puts(out, params[p].name);
    skim_str_puts(out, ";\n");
  }
  skim_transform_range(src, len, assignment_pos, ctor_body_close, out);
  skim_str_putc(out, '\n');
  skim_str_putc(out, src[ctor_body_close]);
  skim_class_transform_body_range(src, len, ctor_body_close + 1, body_close, out);
  skim_str_putc(out, src[body_close]);

  *io = body_close + 1;
  if (*io < len && src[*io] == '}') (*io)++;
  free_params(params, param_count);
  return true;
}
