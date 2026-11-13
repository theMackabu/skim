#ifndef SKIM_TS_TYPE_SYNTAX_H
#define SKIM_TS_TYPE_SYNTAX_H

#include "chars.h"

size_t skim_ts_skip_type_tail(const char *src, size_t len, size_t i);
size_t skim_ts_skip_return_type_tail(const char *src, size_t len, size_t i);
size_t skim_ts_skip_type_declaration_statement(const char *src, size_t len, size_t i);
size_t skim_ts_skip_declare_function_statement(const char *src, size_t len, size_t i);
size_t skim_ts_find_interface_body_or_semicolon(const char *src, size_t len, size_t i);
size_t skim_ts_skip_angle_type_list(const char *src, size_t len, size_t i);

bool skim_colon_looks_like_ternary(const char *src, size_t colon);
bool skim_colon_looks_like_case_label(const char *src, size_t colon);
bool skim_ts_colon_starts_type_annotation(const char *src, size_t len, size_t i);
bool skim_ts_colon_is_in_var_decl(const char *src, size_t colon);
bool skim_ts_colon_after_function_like_params(const char *src, size_t i);

#endif
