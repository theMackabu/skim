#ifndef SKIM_AST_H
#define SKIM_AST_H

#include "token.h"

typedef enum : uint8_t {
  SKIM_N_IMPORT,
  SKIM_N_IMPORT_EQUALS,
  SKIM_N_EXPORT,
  SKIM_N_ENUM,
  SKIM_N_NAMESPACE,
  SKIM_N_CLASS,
  SKIM_N_CONSTRUCTOR,
  SKIM_N_FIELD,
} skim_node_kind_t;

typedef enum : uint8_t {
  SKIM_EXPORT_DECLARATION,
  SKIM_EXPORT_NAMED,
  SKIM_EXPORT_STAR,
  SKIM_EXPORT_DEFAULT,
  SKIM_EXPORT_ASSIGNMENT,
} skim_export_form_t;

constexpr uint8_t SKIM_NF_EXPORT = 1 << 0;
constexpr uint8_t SKIM_NF_CONST = 1 << 1;
constexpr uint8_t SKIM_NF_STATIC = 1 << 2;
constexpr uint8_t SKIM_NF_ACCESSOR = 1 << 3;
constexpr uint8_t SKIM_NF_EXPRESSION = 1 << 4;

typedef struct {
  uint32_t first;
  uint32_t end;
  union {
    struct {
      uint32_t clause;
      uint32_t source;
    } import;
    struct {
      uint32_t name;
      uint32_t value;
    } alias;
    struct {
      uint32_t body;
      uint32_t keyword;
      skim_export_form_t form;
    } exported;
    struct {
      uint32_t name;
      uint32_t body;
    } decl;
    struct {
      uint32_t properties;
      uint32_t super_end;
    } constructor;
    struct {
      uint32_t key;
      uint32_t value;
    } field;
  };
  uint8_t kind;
  uint8_t flags;
} skim_node_t;

typedef enum : uint8_t {
  SKIM_D_VAR,
  SKIM_D_LET,
  SKIM_D_PARAM,
  SKIM_D_FUNCTION,
  SKIM_D_CLASS,
  SKIM_D_ENUM,
  SKIM_D_NAMESPACE,
  SKIM_D_IMPORT,
  SKIM_D_ALIAS,
  SKIM_D_AMBIENT,
  SKIM_D_TYPE,
} skim_decl_kind_t;

typedef struct {
  uint32_t first;
  uint32_t end;
} skim_scope_t;

typedef struct {
  uint32_t token;
  uint32_t scope : 26;
  uint32_t kind : 4;
  uint32_t used : 1;
  uint32_t typed : 1;
} skim_decl_t;

typedef struct {
  skim_node_t *nodes;
  uint32_t *links;
  skim_scope_t *scopes;
  skim_decl_t *decls;
  uint32_t node_count;
  uint32_t node_cap;
  uint32_t link_count;
  uint32_t link_cap;
  uint32_t scope_count;
  uint32_t scope_cap;
  uint32_t decl_count;
  uint32_t decl_cap;
  bool module;
} skim_ast_t;

void skim_parse(skim_tokens_t *tokens, skim_ast_t *ast, const char *src);
void skim_ast_free(skim_ast_t *ast);

#endif
