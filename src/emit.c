#include "chars.h"
#include "emit.h"
#include "skip.h"
#include "str.h"
#include "typescript.h"

#include <string.h>

static void trim_tagged_template_space(skim_str_t *out) {
  size_t end = out->len;
  while (end > 0 && (out->data[end - 1] == ' ' || out->data[end - 1] == '\t'))
    end--;
  if (end == out->len || end == 0) return;
  char prev = out->data[end - 1];
  if (!(skim_is_id_part(prev) || prev == ')' || prev == ']')) return;
  if (
    skim_word_before(out->data, end, "return") || skim_word_before(out->data, end, "throw") ||
    skim_word_before(out->data, end, "yield") || skim_word_before(out->data, end, "case") ||
    skim_word_before(out->data, end, "delete") || skim_word_before(out->data, end, "typeof") ||
    skim_word_before(out->data, end, "void")
  )
    return;
  out->len = end;
  out->data[out->len] = '\0';
}

static size_t parse_simple_new_callee(const char *src, size_t len, size_t i) {
  if (!skim_word_at(src, len, i, "new")) return i;
  i = skim_skip_ws(src, len, i + 3);
  size_t start = 0, end = 0;
  i = skim_parse_identifier(src, len, i, &start, &end);
  if (start == end) return start;
  for (;;) {
    size_t dot = skim_skip_ws(src, len, i);
    if (dot >= len || src[dot] != '.') break;
    size_t part_start = 0, part_end = 0;
    size_t after = skim_parse_identifier(src, len, dot + 1, &part_start, &part_end);
    if (part_start == part_end) break;
    i = after;
  }
  return i;
}

static char prev_non_ws_in_source(const char *src, size_t start, size_t i) {
  while (i > start && skim_is_space(src[i - 1]))
    i--;
  return i > start ? src[i - 1] : '\0';
}

static bool try_copy_shorthand_property(skim_str_t *out, const char *src, size_t len, size_t start, size_t *io) {
  size_t i = *io;
  char prev = prev_non_ws_in_source(src, start, i);
  if (prev != '{' && prev != ',') return false;
  if (!skim_is_id_start(src[i])) return false;
  size_t key_start = 0, key_end = 0;
  size_t after_key = skim_parse_identifier(src, len, i, &key_start, &key_end);
  if (key_start != i || key_end == key_start) return false;
  size_t colon = skim_skip_ws(src, len, after_key);
  if (colon >= len || src[colon] != ':') return false;
  size_t value_start = 0, value_end = 0;
  size_t after_value = skim_parse_identifier(src, len, skim_skip_ws(src, len, colon + 1), &value_start, &value_end);
  if (value_end - value_start != key_end - key_start) return false;
  if (memcmp(src + key_start, src + value_start, key_end - key_start) != 0) return false;
  size_t next = skim_skip_ws(src, len, after_value);
  if (next < len && src[next] != ',' && src[next] != '}') return false;
  skim_str_putn(out, src + key_start, key_end - key_start);
  *io = after_value;
  return true;
}

static bool new_argless_boundary(const char *src, size_t len, size_t i) {
  i = skim_skip_ws(src, len, i);
  if (i >= len) return true;
  char c = src[i];
  return c == ';' || c == '\n' || c == '\r' || c == ',' || c == ')' || c == ']' || c == '}';
}

static bool trailing_parameter_comma_at(const char *src, size_t len, size_t comma) {
  if (src[comma] != ',') return false;
  size_t close = skim_skip_ws_comments(src, len, comma + 1);
  return close < len && src[close] == ')';
}

static bool try_copy_argless_new(skim_str_t *out, const char *src, size_t len, size_t *io) {
  size_t i = *io;
  size_t new_start = i;
  size_t callee_end = parse_simple_new_callee(src, len, i);
  if (callee_end <= i) return false;
  size_t after = skim_skip_ws(src, len, callee_end);
  if (after < len && src[after] == '(') return false;
  if (!new_argless_boundary(src, len, after)) return false;
  skim_str_putn(out, src + new_start, callee_end - new_start);
  skim_str_puts(out, "()");
  *io = callee_end;
  return true;
}

static bool try_copy_parenthesized_argless_new(skim_str_t *out, const char *src, size_t len, size_t *io) {
  size_t i = *io;
  if (src[i] != '(') return false;
  size_t new_start = skim_skip_ws(src, len, i + 1);
  size_t callee_end = parse_simple_new_callee(src, len, new_start);
  if (callee_end <= new_start) return false;
  size_t close = skim_skip_ws(src, len, callee_end);
  if (close >= len || src[close] != ')') return false;
  size_t after = skim_skip_ws(src, len, close + 1);
  if (after >= len || (src[after] != '[' && src[after] != '.')) return false;
  skim_str_putn(out, src + new_start, callee_end - new_start);
  skim_str_puts(out, "()");
  *io = close + 1;
  return true;
}

static bool try_copy_async_arrow_head(skim_str_t *out, const char *src, size_t len, size_t *io) {
  size_t i = *io;
  if (!skim_word_at(src, len, i, "async")) return false;
  size_t after_async = skim_skip_ws(src, len, i + 5);
  if (after_async + 1 < len && src[after_async] == '=' && src[after_async + 1] == '>') {
    skim_str_puts(out, "(async)");
    *io = i + 5;
    return true;
  }
  size_t param_start = 0, param_end = 0;
  size_t after_param = skim_parse_identifier(src, len, i + 5, &param_start, &param_end);
  if (param_start == param_end) return false;
  size_t arrow = skim_skip_ws(src, len, after_param);
  if (arrow + 1 >= len || src[arrow] != '=' || src[arrow + 1] != '>') return false;
  skim_str_puts(out, "async(");
  skim_str_putn(out, src + param_start, param_end - param_start);
  skim_str_puts(out, ")");
  *io = after_param;
  return true;
}

void skim_transform_range(const char *src, size_t len, size_t start, size_t end, skim_str_t *out) {
  for (size_t i = start; i < end;) {
    char c = src[i];
    if (skim_emit_copy_plain_span(out, src, end, &i)) continue;
    if (skim_is_id_start(c)) {
      if (c == 'n' && skim_word_at(src, len, i, "new") && try_copy_argless_new(out, src, len, &i)) continue;
      if (c == 'a' && skim_word_at(src, len, i, "async") && try_copy_async_arrow_head(out, src, len, &i)) continue;
      if (try_copy_shorthand_property(out, src, len, start, &i)) continue;
      if (skim_transform_try_at(out, src, len, &i)) continue;
      skim_emit_copy_plain_identifier(out, src, end, &i);
      continue;
    }
    if (c == '\'' || c == '"' || c == '`') {
      if (c == '`') trim_tagged_template_space(out);
      i = skim_copy_string(out, src, len, i);
      continue;
    }
    if (c == '/') {
      if (i + 1 < len && src[i + 1] == '/') {
        i = skim_copy_line_comment(out, src, len, i);
        continue;
      }
      if (i + 1 < len && src[i + 1] == '*') {
        i = skim_copy_block_comment(out, src, len, i);
        continue;
      }
      if (skim_slash_starts_regex(src, start, i)) {
        size_t after = skim_skip_regex_literal(src, end, i);
        skim_str_putn(out, src + i, after - i);
        i = after;
        continue;
      }
    } else if (skim_is_digit(c)) {
      if (skim_legacy_octal_literal_at(src, len, i)) {
        i = skim_copy_legacy_octal_literal(out, src, len, i);
        continue;
      }
      if (skim_based_number_literal_at(src, len, i)) {
        i = skim_copy_based_number_literal(out, src, len, i, true);
        continue;
      }
      if (skim_decimal_number_literal_at(src, len, i)) {
        i = skim_copy_decimal_number_literal(out, src, len, i, true);
        continue;
      }
    } else if (c == '(') {
      if (try_copy_parenthesized_argless_new(out, src, len, &i)) continue;
    } else if (c == ',') {
      if (trailing_parameter_comma_at(src, len, i)) {
        i++;
        continue;
      }
    } else if (c == '@' || c == ':' || c == '?' || c == '!' || c == '<') {
      if (skim_transform_try_at(out, src, len, &i)) continue;
    }
    skim_str_putc(out, src[i++]);
  }
}
