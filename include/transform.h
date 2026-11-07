#ifndef SKIM_TRANSFORM_H
#define SKIM_TRANSFORM_H

#include "skim.h"
#include "str.h"

typedef struct {
  size_t *decl_buckets;
  size_t *known_member_buckets;
} skim_transform_state_t;

extern skim_options_t skim_options;
extern skim_source_mode_t skim_source_mode;
extern skim_transform_state_t *skim_state;

void skim_transform_state_free(skim_transform_state_t *state);
char *skim_transform_typescript(const char *src, size_t len);
void skim_transform_typescript_into(const char *src, size_t len, skim_str_t *out, skim_str_t *scratch);

void skim_cleanup_output_spacing(skim_str_t *out, skim_str_t *scratch);

bool skim_has_module_syntax(const char *src, size_t len);
bool skim_source_has_module_word(const char *src, size_t len);
bool skim_source_needs_module_marker(const char *src, size_t len);

#endif
