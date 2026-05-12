#include "skim_internal.h"

#include <ctype.h>

bool skim_emit_char_needs_dispatch(char c) {
  return c == '\'' || c == '"' || c == '`' || c == '/' || c == '(' || c == ',' || c == '@' || c == ':' || c == '?' ||
         c == '!' || c == '<' || c == '{' || c == '}' || isdigit((unsigned char)c) || skim_is_id_start(c);
}

bool skim_emit_copy_plain_identifier(skim_str_t *out, const char *src, size_t end, size_t *io) {
  size_t i = *io;
  if (i >= end || !skim_is_id_start(src[i])) return false;
  size_t j = i + 1;
  while (j < end && skim_is_id_part(src[j]))
    j++;
  skim_str_putn(out, src + i, j - i);
  *io = j;
  return true;
}

bool skim_emit_copy_plain_span(skim_str_t *out, const char *src, size_t end, size_t *io) {
  size_t i = *io;
  if (i >= end || skim_emit_char_needs_dispatch(src[i])) return false;
  size_t j = i + 1;
  while (j < end && !skim_emit_char_needs_dispatch(src[j]))
    j++;
  skim_str_putn(out, src + i, j - i);
  *io = j;
  return true;
}
