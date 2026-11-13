#ifndef SKIM_PRINT_H
#define SKIM_PRINT_H

#include "ast.h"
#include "skim.h"
#include "str.h"

typedef struct skim_enum_member skim_enum_member_t;

typedef struct {
  const char *src;
  size_t len;
  skim_token_t *t;
  skim_ast_t *ast;
  const skim_options_t *options;
  skim_str_t *out;
  skim_enum_member_t *enum_members;
  skim_str_t enum_strings;
  skim_str_t params;
  uint32_t count;
  uint32_t cursor;
  uint32_t copied;
  uint32_t kept;
  uint32_t enum_member_count;
  uint32_t enum_member_cap;
  uint32_t param;
  bool exported;
  bool resolved;
  bool module_syntax;
} skim_printer_t;

void skim_print(
  skim_printer_t *pr,
  const char *src,
  size_t len,
  skim_tokens_t *tokens,
  skim_ast_t *ast,
  const skim_options_t *options,
  skim_source_mode_t mode,
  skim_str_t *out
);
void skim_printer_free(skim_printer_t *pr);

#endif
