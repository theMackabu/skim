#ifndef SKIM_TYPESCRIPT_STATE_H
#define SKIM_TYPESCRIPT_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SKIM_ENUM_SCOPE_STACK 256
#define SKIM_OBJECT_SCAN_MEMO_SIZE 256

typedef enum {
  ENUM_VALUE_UNKNOWN,
  ENUM_VALUE_NUMBER,
  ENUM_VALUE_STRING,
} enum_value_kind_t;

typedef struct {
  enum_value_kind_t kind;
  double number;
  char *string;
} enum_value_t;

typedef struct {
  char *enum_name;
  char *member_name;
  size_t enum_name_len;
  size_t member_name_len;
  enum_value_t value;
  bool inline_ref;
  size_t scope_start;
  size_t hash_bucket;
  size_t next_hash;
} enum_known_member_t;

typedef struct {
  enum_known_member_t *known_members;
  size_t known_member_count;
  size_t known_member_cap;
  size_t *known_member_buckets;
  size_t inline_member_count;
  int enum_expression_depth;
  bool current_enum_inline_refs;
  size_t current_enum_scope_start;
  int enum_suppress_depth;
  struct {
    const char *src;
    size_t i;
    int depth;
    size_t stack[SKIM_ENUM_SCOPE_STACK];
  } enum_scope_scan;
} skim_enum_state_t;

typedef struct {
  char **used_namespace_params;
  size_t used_namespace_param_count;
} skim_namespace_state_t;

typedef struct {
  size_t pos;
  size_t name_start;
  size_t name_end;
} local_decl_t;

typedef struct {
  const char *src;
  local_decl_t *items;
  size_t count;
  size_t cap;
} skim_local_decl_index_t;

typedef struct {
  const char *src;
  size_t pos_plus_one[SKIM_OBJECT_SCAN_MEMO_SIZE];
  bool result[SKIM_OBJECT_SCAN_MEMO_SIZE];
} skim_object_scan_memo_t;

typedef struct {
  uint32_t scan_pos;
  uint32_t prev_plus_one;
  uint8_t kind;
  uint8_t position;
} import_use_t;

typedef struct {
  uint32_t name;
  uint32_t name_len;
  uint32_t last_use_plus_one;
} import_ident_t;

typedef struct {
  const char *src;
  size_t len;
  size_t path_start;
  uint64_t *on_path;
  import_ident_t *idents;
  size_t ident_count;
  size_t ident_mask;
  import_use_t *uses;
  size_t use_count;
  size_t use_cap;
} skim_import_index_t;

typedef struct {
  skim_enum_state_t enums;
  skim_namespace_state_t namespaces;
  skim_local_decl_index_t local_decls;
  skim_object_scan_memo_t object_memo;
  skim_import_index_t import_index;
} skim_ts_state_t;

#endif
