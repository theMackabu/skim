#ifndef SKIM_TOKEN_H
#define SKIM_TOKEN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum : uint8_t {
  SKIM_T_EOF,
  SKIM_T_WORD,
  SKIM_T_PRIVATE,
  SKIM_T_NUMBER,
  SKIM_T_STRING,
  SKIM_T_TEMPLATE,
  SKIM_T_REGEX,
  SKIM_T_OP,
  SKIM_T_ARROW,
  SKIM_T_SPREAD,
  SKIM_T_CHAIN,
  SKIM_T_INCDEC,
} skim_token_kind_t;

typedef enum : uint8_t {
  SKIM_KW_NONE,
#define SKIM_KEYWORD(name, text, attrs) SKIM_KW_##name,
#include "keyword_list.h"
  SKIM_KW_COUNT,
} skim_keyword_t;

constexpr uint8_t SKIM_KW_PREFIX = 1;
constexpr uint8_t SKIM_KW_INFIX = 2;
constexpr uint8_t SKIM_KW_LEAD = 4;

constexpr uint16_t SKIM_TF_NEWLINE = 1 << 0;
constexpr uint16_t SKIM_TF_STRIP = 1 << 1;
constexpr uint16_t SKIM_TF_REF = 1 << 2;
constexpr uint16_t SKIM_TF_REWRITE = 1 << 3;
constexpr uint16_t SKIM_TF_NODE = 1 << 4;
constexpr uint16_t SKIM_TF_TAIL = 1 << 5;
constexpr uint16_t SKIM_TF_JOIN = 1 << 6;
constexpr uint16_t SKIM_TF_VISITED = 1 << 7;
constexpr uint16_t SKIM_TF_EMPTY = 1 << 8;
constexpr uint16_t SKIM_TF_LEAD = 1 << 9;
constexpr uint16_t SKIM_TF_PAREN = 1 << 10;

typedef struct {
  uint32_t start;
  uint32_t end;
  uint32_t pair;
  uint8_t kind;
  uint8_t word;
  uint16_t flags;
} skim_token_t;

typedef struct {
  skim_token_t *items;
  uint32_t count;
  uint32_t cap;
  uint32_t *stack;
  uint32_t stack_cap;
} skim_tokens_t;

extern const uint8_t skim_keyword_attrs[SKIM_KW_COUNT];

void skim_lex(skim_tokens_t *tokens, const char *src, size_t len);
void skim_tokens_free(skim_tokens_t *tokens);

#endif
