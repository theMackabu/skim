#ifndef SKIM_SYNTAX_H
#define SKIM_SYNTAX_H

#include "ast.h"
#include <stdint.h>

typedef enum: uint8_t {
  SKIM_TOKEN_EOF,
  SKIM_TOKEN_IDENTIFIER,
  SKIM_TOKEN_KEYWORD,
  SKIM_TOKEN_OTHER,
} skim_syntax_token_kind_t;

typedef enum: uint8_t {
  SKIM_KEYWORD_NONE,
  SKIM_KEYWORD_IMPORT,
  SKIM_KEYWORD_EXPORT,
  SKIM_KEYWORD_INTERFACE,
  SKIM_KEYWORD_TYPE,
  SKIM_KEYWORD_DECLARE,
  SKIM_KEYWORD_ENUM,
  SKIM_KEYWORD_NAMESPACE,
  SKIM_KEYWORD_MODULE,
  SKIM_KEYWORD_CLASS,
  SKIM_KEYWORD_CONST,
} skim_syntax_keyword_t;

typedef struct {
  skim_syntax_token_kind_t kind;
  skim_syntax_keyword_t keyword;
  size_t start;
  size_t end;
} skim_syntax_token_t;

skim_syntax_token_t skim_syntax_token_at(const char *src, size_t len, size_t pos);
bool skim_syntax_item_needs_statement_semicolon(const skim_ast_item_t *item, const char *src, size_t len, const skim_str_t *out);

#endif
