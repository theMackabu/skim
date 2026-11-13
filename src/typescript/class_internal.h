#ifndef SKIM_TS_CLASS_INTERNAL_H
#define SKIM_TS_CLASS_INTERNAL_H

#include "str.h"

bool skim_class_member_modifier_at(
  const char *src,
  size_t len,
  size_t i,
  const char **word,
  size_t *word_len,
  bool *is_abstract
);
size_t skim_class_skip_type_tail(const char *src, size_t len, size_t i);
size_t skim_class_skip_decorator_line(const char *src, size_t len, size_t i);
void skim_class_emit_decorator_range(skim_str_t *out, const char *src, size_t len, size_t start, size_t end);
void skim_class_transform_body_range(const char *src, size_t len, size_t start, size_t end, skim_str_t *out);

#endif
