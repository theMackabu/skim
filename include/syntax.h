#ifndef SKIM_SYNTAX_H
#define SKIM_SYNTAX_H

#include "ast.h"

typedef enum {
  SKIM_TOKEN_EOF,
  SKIM_TOKEN_IDENTIFIER,
  SKIM_TOKEN_KEYWORD,
  SKIM_TOKEN_NUMBER,
  SKIM_TOKEN_STRING,
  SKIM_TOKEN_TEMPLATE,
  SKIM_TOKEN_REGEX,
  SKIM_TOKEN_PUNCT,
} skim_syntax_token_kind_t;

typedef enum {
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
  SKIM_KEYWORD_VAR,
  SKIM_KEYWORD_LET,
  SKIM_KEYWORD_FUNCTION,
  SKIM_KEYWORD_ASYNC,
  SKIM_KEYWORD_NEW,
  SKIM_KEYWORD_THIS,
  SKIM_KEYWORD_SUPER,
  SKIM_KEYWORD_AWAIT,
  SKIM_KEYWORD_YIELD,
} skim_syntax_keyword_t;

typedef struct {
  skim_syntax_token_kind_t kind;
  skim_syntax_keyword_t keyword;
  size_t leading_start;
  size_t start;
  size_t end;
  bool leading_had_newline;
} skim_syntax_token_t;

typedef struct {
  const char *src;
  size_t len;
  size_t pos;
} skim_lexer_t;

void skim_lexer_init(skim_lexer_t *lexer, const char *src, size_t len);
skim_syntax_token_t skim_lexer_next(skim_lexer_t *lexer, bool regex_allowed);
skim_syntax_token_t skim_syntax_token_at(const char *src, size_t len, size_t pos, bool regex_allowed);
bool skim_syntax_token_is_punct(const skim_syntax_token_t *token, const char *punct, const char *src);

void skim_parse_program(skim_ast_program_t *program, const char *src, size_t len);
bool skim_syntax_item_needs_statement_semicolon(
  const skim_ast_item_t *item,
  const char *src,
  size_t len,
  const skim_str_t *out
);

#endif
