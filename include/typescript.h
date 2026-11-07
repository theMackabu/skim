#ifndef SKIM_TYPESCRIPT_H
#define SKIM_TYPESCRIPT_H

#include "str.h"

bool skim_transform_try_at(skim_str_t *out, const char *src, size_t len, size_t *io);

bool skim_ts_annotations_try(skim_str_t *out, const char *src, size_t len, size_t *io);
bool skim_ts_module_try(skim_str_t *out, const char *src, size_t len, size_t *io);
bool skim_ts_enum_try(skim_str_t *out, const char *src, size_t len, size_t *io);
bool skim_ts_enum_ref_try(skim_str_t *out, const char *src, size_t len, size_t *io);
bool skim_ts_namespace_try(skim_str_t *out, const char *src, size_t len, size_t *io);
bool skim_ts_class_try(skim_str_t *out, const char *src, size_t len, size_t *io);

void skim_ts_enum_reset(void);
void skim_ts_enum_suppress_push(void);
void skim_ts_enum_suppress_pop(void);
void skim_ts_namespace_reset(void);
bool skim_ts_local_value_decl_before(const char *src, size_t len, size_t pos, const char *name);

void skim_ts_enum_scope_invalidate(void);
void skim_ts_annotations_memo_invalidate(void);
void skim_ts_namespace_decl_index_invalidate(void);
void skim_ts_module_index_invalidate(void);

#endif
