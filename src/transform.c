#include "ast.h"

#include <stdlib.h>
#include <string.h>

skim_options_t skim_options;
skim_transform_state_t *skim_state;

static size_t transform_depth;
skim_source_mode_t skim_source_mode = SKIM_SOURCE_MODULE;

static bool source_mode_needs_module_marker(void) {
  return skim_source_mode == SKIM_SOURCE_MODULE;
}

void skim_transform_state_free(skim_transform_state_t *state) {
  if (!state) return;
  free(state->decl_buckets);
  free(state->known_member_buckets);
  *state = (skim_transform_state_t){0};
}

static bool cleanup_is_assignment_equal(const char *src, size_t len, size_t i) {
  if (src[i] != '=') return false;
  char prev = i > 0 ? src[i - 1] : '\0';
  char next = i + 1 < len ? src[i + 1] : '\0';
  if (next == '=' || next == '>') return false;
  if (
    prev == '=' || prev == '!' || prev == '<' || prev == '>' || prev == '+' || prev == '-' || prev == '*' ||
    prev == '/' || prev == '%' || prev == '&' || prev == '|' || prev == '^' || prev == '?' || prev == ':'
  )
    return false;
  return true;
}

static void cleanup_trim_trailing_horizontal_space(skim_str_t *out) {
  while (out->len > 0 && (out->data[out->len - 1] == ' ' || out->data[out->len - 1] == '\t'))
    out->len--;
  if (out->data) out->data[out->len] = '\0';
}

static char cleanup_prev_non_ws(const skim_str_t *out) {
  size_t i = out->len;
  while (i > 0 && skim_is_space(out->data[i - 1]))
    i--;
  return i > 0 ? out->data[i - 1] : '\0';
}

static size_t cleanup_skip_ws(const char *src, size_t len, size_t i) {
  while (i < len && (src[i] == ' ' || src[i] == '\t' || src[i] == '\n' || src[i] == '\r'))
    i++;
  return i;
}

static bool cleanup_next_is_redundant_paren_tail(char c) {
  return c == '*' || c == '/' || c == '%' || c == '+' || c == '-' || c == '<' || c == '>' || c == '|' || c == '&' ||
         c == '^' || c == '?' || c == ':' || c == ',' || c == ';' || c == ')' || c == ']';
}

static bool cleanup_try_copy_unwrapped_identifier_paren(skim_str_t *out, const char *src, size_t len, size_t *io) {
  size_t i = *io;
  if (src[i] != '(') return false;
  char prev = cleanup_prev_non_ws(out);
  if (skim_is_id_part(prev) || prev == ')' || prev == ']' || prev == '}') return false;
  size_t name_start = cleanup_skip_ws(src, len, i + 1);
  if (name_start >= len || !skim_is_id_start(src[name_start])) return false;
  size_t name_end = name_start + 1;
  while (name_end < len && skim_is_id_part(src[name_end]))
    name_end++;
  size_t close = cleanup_skip_ws(src, len, name_end);
  if (close >= len || src[close] != ')') return false;
  size_t next = cleanup_skip_ws(src, len, close + 1);
  if (next >= len || !cleanup_next_is_redundant_paren_tail(src[next])) return false;
  skim_str_putn(out, src + name_start, name_end - name_start);
  *io = close + 1;
  return true;
}

static size_t cleanup_copy_string(skim_str_t *out, const char *src, size_t len, size_t i) {
  size_t start = i;
  i = skim_skip_string_raw(src, len, i);
  skim_str_putn(out, src + start, i - start);
  return i;
}

static size_t cleanup_copy_line_comment(skim_str_t *out, const char *src, size_t len, size_t i) {
  size_t start = i;
  while (i < len && src[i] != '\n' && src[i] != '\r')
    i++;
  skim_str_putn(out, src + start, i - start);
  return i;
}

static size_t cleanup_copy_block_comment(skim_str_t *out, const char *src, size_t len, size_t i) {
  size_t start = i;
  i = skim_skip_block_comment(src, len, i + 2);
  skim_str_putn(out, src + start, i - start);
  return i;
}

static void cleanup_output_spacing(skim_str_t *out, skim_str_t *scratch) {
  if (!out->data || out->len == 0) return;
  const char *src = out->data;
  size_t len = out->len;
  skim_str_t cleaned = scratch ? *scratch : (skim_str_t){0};
  cleaned.len = 0;
  if (cleaned.data) cleaned.data[0] = '\0';
  skim_str_reserve(&cleaned, len + 1);

  size_t i = cleanup_skip_ws(src, len, 0);
  for (; i < len;) {
    if (!(skim_char_class[(unsigned char)src[i]] & SKIM_CC_CLEANUP_SPECIAL)) {
      skim_str_reserve(&cleaned, len - i);
      char *dst = cleaned.data + cleaned.len;
      size_t run = i;
      do {
        *dst++ = src[run++];
      } while (run < len && !skim_char_has(src[run], SKIM_CC_CLEANUP_SPECIAL));
      *dst = '\0';
      cleaned.len += run - i;
      i = run;
      continue;
    }
    if (src[i] == '\'' || src[i] == '"' || src[i] == '`') {
      i = cleanup_copy_string(&cleaned, src, len, i);
      continue;
    }
    if (i + 1 < len && src[i] == '/' && src[i + 1] == '/') {
      i = cleanup_copy_line_comment(&cleaned, src, len, i);
      continue;
    }
    if (i + 1 < len && src[i] == '/' && src[i + 1] == '*') {
      i = cleanup_copy_block_comment(&cleaned, src, len, i);
      continue;
    }
    if (src[i] == '/' && skim_slash_starts_regex(src, 0, i)) {
      size_t after = skim_skip_regex_literal(src, len, i);
      skim_str_putn(&cleaned, src + i, after - i);
      i = after;
      continue;
    }
    if (skim_is_space(src[i])) {
      bool has_newline = false;
      size_t last_newline = i;
      size_t j = i;
      while (j < len && skim_is_space(src[j])) {
        if (src[j] == '\n' || src[j] == '\r') {
          has_newline = true;
          last_newline = j;
        }
        j++;
      }
      if (has_newline) {
        cleanup_trim_trailing_horizontal_space(&cleaned);
        if (cleaned.len > 0 && cleaned.data[cleaned.len - 1] != '\n') skim_str_putc(&cleaned, '\n');
        size_t indent = last_newline + 1;
        size_t indent_end = indent;
        while (indent_end < j && (src[indent_end] == ' ' || src[indent_end] == '\t'))
          indent_end++;
        skim_str_putn(&cleaned, src + indent, indent_end - indent);
      } else {
        skim_str_putc(&cleaned, ' ');
      }
      i = j;
      continue;
    }
    if (cleanup_try_copy_unwrapped_identifier_paren(&cleaned, src, len, &i)) continue;
    if (cleanup_is_assignment_equal(src, len, i)) {
      cleanup_trim_trailing_horizontal_space(&cleaned);
      if (cleaned.len > 0 && cleaned.data[cleaned.len - 1] != '\n') skim_str_putc(&cleaned, ' ');
      skim_str_putc(&cleaned, '=');
      size_t next = i + 1;
      while (next < len && (src[next] == ' ' || src[next] == '\t'))
        next++;
      if (next < len && src[next] != '\n' && src[next] != '\r') skim_str_putc(&cleaned, ' ');
      i = next;
      continue;
    }
    if (src[i] == ';') {
      cleanup_trim_trailing_horizontal_space(&cleaned);
      skim_str_putc(&cleaned, ';');
      i++;
      continue;
    }
    if (src[i] == ')') {
      cleanup_trim_trailing_horizontal_space(&cleaned);
      skim_str_putc(&cleaned, src[i++]);
      continue;
    }
    if (src[i] == '{' && cleanup_prev_non_ws(&cleaned) == ')') {
      cleanup_trim_trailing_horizontal_space(&cleaned);
      skim_str_putc(&cleaned, ' ');
      skim_str_putc(&cleaned, src[i++]);
      continue;
    }
    skim_str_putc(&cleaned, src[i++]);
  }

  cleanup_trim_trailing_horizontal_space(&cleaned);
  if (cleaned.len > 0 && cleaned.data[cleaned.len - 1] != '\n') skim_str_putc(&cleaned, '\n');
  if (scratch) *scratch = *out;
  else free(out->data);
  *out = cleaned;
}

void skim_transform_typescript_into(const char *src, size_t len, skim_str_t *out, skim_str_t *scratch) {
  bool root_transform = transform_depth++ == 0;
  skim_scan_cache_t saved_cache;
  skim_scan_cache_enter(&saved_cache, src, len);
  if (root_transform) {
    skim_decl_reset();
    skim_ts_enum_reset();
    skim_ts_namespace_reset();
  }
  out->len = 0;
  if (out->data) out->data[0] = '\0';
  skim_str_reserve(out, len + 128);
  skim_ast_program_t program = {0};
  skim_parse_program(&program, src, len);
  skim_ast_print_program(&program, out);
  skim_ast_free_program(&program);
  if (
    source_mode_needs_module_marker() && skim_source_has_module_word(src, len) &&
    !skim_has_module_syntax(out->data ? out->data : "", out->len) && skim_source_needs_module_marker(src, len)
  ) {
    if (skim_has_effective_code(out->data ? out->data : "", out->len)) skim_str_putc(out, '\n');
    else {
      out->len = 0;
      if (out->data) out->data[0] = '\0';
    }
    skim_str_puts(out, "export {};\n");
  }
  if (root_transform) cleanup_output_spacing(out, scratch);
  skim_scan_cache_leave(&saved_cache);
  transform_depth--;
  if (root_transform) {
    skim_ts_enum_reset();
    skim_ts_namespace_reset();
    skim_decl_reset();
  }
  if (!out->data) skim_str_reserve(out, 0);
}

char *skim_transform_typescript(const char *src, size_t len) {
  skim_str_t out = {0};
  skim_transform_typescript_into(src, len, &out, NULL);
  return out.data ? out.data : skim_slice_dup("", 0, 0);
}
