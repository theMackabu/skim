#ifndef SKIM_TS_ENUM_INTERNAL_H
#define SKIM_TS_ENUM_INTERNAL_H

#include "typescript_state.h"

#include <stdbool.h>
#include <stddef.h>

enum_value_t skim_enum_value_unknown(void);
enum_value_t skim_enum_value_number(double n);
enum_value_t skim_enum_value_string_dup(const char *s);
enum_value_t skim_enum_value_clone(enum_value_t v);
void skim_enum_value_free(enum_value_t v);
char *skim_enum_number_dup(double n);
char *skim_enum_value_to_literal(enum_value_t v);
enum_value_t skim_enum_eval_expr(
  const char *expr,
  size_t start,
  size_t end,
  const char *enum_name,
  char **members,
  enum_value_t *values,
  size_t member_count
);

bool skim_enum_lookup_qualified_member(const char *enum_name, const char *member_name, enum_value_t *out);

#endif
