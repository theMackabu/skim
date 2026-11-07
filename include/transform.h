#ifndef SKIM_TRANSFORM_H
#define SKIM_TRANSFORM_H

#include "str.h"

char *skim_transform_typescript(const char *src, size_t len);
void skim_transform_typescript_into(const char *src, size_t len, skim_str_t *out, skim_str_t *scratch);

void skim_cleanup_output_spacing(skim_str_t *out, skim_str_t *scratch);

bool skim_has_module_syntax(const char *src, size_t len);
bool skim_source_has_module_word(const char *src, size_t len);
bool skim_source_needs_module_marker(const char *src, size_t len);

#endif
