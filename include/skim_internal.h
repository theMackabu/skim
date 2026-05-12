#ifndef SKIM_INTERNAL_H
#define SKIM_INTERNAL_H

#include "skim.h"

typedef struct {
  char *data;
  size_t len;
  size_t cap;
} skim_str_t;

typedef struct {
  const char *src;
  size_t len;
} skim_source_t;

typedef struct {
  size_t *decl_buckets;
  size_t *known_member_buckets;
} skim_transform_state_t;

extern skim_options_t skim_options;
extern skim_source_mode_t skim_source_mode;
extern skim_transform_state_t skim_default_state;
extern skim_transform_state_t *skim_state;

#define SKIM_BUCKET_COUNT 4096u

#define SKIM_DECL_VALUE 1u
#define SKIM_DECL_ENUM 2u
#define SKIM_DECL_NAMESPACE 4u

void skim_decl_reset(void);
void skim_transform_state_free(skim_transform_state_t *state);
void skim_decl_remember(const char *src, const char *name_start, const char *name_end, size_t pos, unsigned flags);
bool skim_decl_seen_before(const char *src, const char *name, size_t pos, unsigned flags);

void skim_die(const char *msg);
void skim_str_reserve(skim_str_t *s, size_t extra);
void skim_str_putc(skim_str_t *s, char c);
void skim_str_putn(skim_str_t *s, const char *p, size_t n);
void skim_str_puts(skim_str_t *s, const char *p);

char *skim_slice_dup(const char *src, size_t start, size_t end);
bool skim_has_effective_code(const char *src, size_t len);
bool skim_has_module_syntax(const char *src, size_t len);
bool skim_has_erasable_import_syntax(const char *src, size_t len);
bool skim_has_export_declare(const char *src, size_t len);
bool skim_has_type_only_module_syntax(const char *src, size_t len);

bool skim_is_id_start(char c);
bool skim_is_id_part(char c);
bool skim_word_at(const char *src, size_t len, size_t i, const char *word);

size_t skim_skip_ws(const char *src, size_t len, size_t i);
size_t skim_skip_ws_comments(const char *src, size_t len, size_t i);
size_t skim_skip_string_raw(const char *src, size_t len, size_t i);
size_t skim_skip_balanced(const char *src, size_t len, size_t i, char open, char close);
size_t skim_skip_statement_like(const char *src, size_t len, size_t i);
size_t skim_parse_identifier(const char *src, size_t len, size_t i, size_t *start, size_t *end);

void skim_emit_preserved_newlines(skim_str_t *out, const char *src, size_t from, size_t to);
size_t skim_copy_string(skim_str_t *out, const char *src, size_t len, size_t i);
size_t skim_copy_line_comment(skim_str_t *out, const char *src, size_t len, size_t i);
size_t skim_copy_block_comment(skim_str_t *out, const char *src, size_t len, size_t i);
bool skim_emit_char_needs_dispatch(char c);
bool skim_emit_copy_plain_identifier(skim_str_t *out, const char *src, size_t end, size_t *io);
bool skim_emit_copy_plain_span(skim_str_t *out, const char *src, size_t end, size_t *io);

char *skim_transform_typescript(const char *src, size_t len);
char *skim_transform_typescript_len(const char *src, size_t len, size_t *out_len);
void skim_transform_typescript_into(const char *src, size_t len, skim_str_t *out, skim_str_t *scratch);
void skim_transform_range(const char *src, size_t len, size_t start, size_t end, skim_str_t *out);
void skim_transform_js_range(const char *src, size_t len, size_t start, size_t end, skim_str_t *out);
bool skim_transform_try_at(skim_str_t *out, const char *src, size_t len, size_t *io);

bool skim_ts_annotations_try(skim_str_t *out, const char *src, size_t len, size_t *io);
bool skim_ts_module_try(skim_str_t *out, const char *src, size_t len, size_t *io);
bool skim_ts_enum_try(skim_str_t *out, const char *src, size_t len, size_t *io);
bool skim_ts_enum_ref_try(skim_str_t *out, const char *src, size_t len, size_t *io);

void skim_ts_enum_reset(void);
void skim_ts_enum_suppress_push(void);
void skim_ts_enum_suppress_pop(void);

void skim_ts_namespace_reset(void);
bool skim_ts_namespace_try(skim_str_t *out, const char *src, size_t len, size_t *io);
bool skim_ts_class_try(skim_str_t *out, const char *src, size_t len, size_t *io);

#endif
