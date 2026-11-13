#ifndef SKIM_PRINT_INTERNAL_H
#define SKIM_PRINT_INTERNAL_H

#include "print.h"

#include <string.h>

static inline const char *token_text(const skim_printer_t *pr, uint32_t i) {
  return pr->src + pr->t[i].start;
}

static inline uint32_t token_len(const skim_printer_t *pr, uint32_t i) {
  return pr->t[i].end - pr->t[i].start;
}

static inline bool token_equal(const skim_printer_t *pr, uint32_t a, uint32_t b) {
  uint32_t n = token_len(pr, a);
  return n == token_len(pr, b) && memcmp(token_text(pr, a), token_text(pr, b), n) == 0;
}

static inline uint8_t token_word(const skim_printer_t *pr, uint32_t i) {
  return pr->t[i].kind == SKIM_T_WORD ? pr->t[i].word : SKIM_KW_NONE;
}

static inline uint32_t token_close(const skim_printer_t *pr, uint32_t open) {
  uint32_t close = pr->t[open].pair;
  return close < pr->count ? close : pr->count;
}

static inline const char *string_content(const skim_printer_t *pr, uint32_t i, uint32_t *len) {
  const char *text = token_text(pr, i);
  uint32_t n = token_len(pr, i);
  bool closed = n >= 2 && text[n - 1] == text[0];
  *len = n - (closed ? 2 : 1);
  return text + 1;
}

static inline void put(skim_printer_t *pr, const char *text) {
  skim_str_puts(pr->out, text);
}

static inline void put_token(skim_printer_t *pr, uint32_t i) {
  skim_str_putn(pr->out, token_text(pr, i), token_len(pr, i));
}

void skim_print_range(skim_printer_t *pr, uint32_t from, uint32_t to);
void skim_print_flush(skim_printer_t *pr);
void skim_print_begin(skim_printer_t *pr, uint32_t token);
void skim_print_resume(skim_printer_t *pr, uint32_t token);
void skim_print_line(skim_printer_t *pr, uint32_t token);
void skim_print_remove(skim_printer_t *pr, uint32_t from, uint32_t to);
void skim_print_alias(skim_printer_t *pr, uint32_t name);

skim_decl_t *skim_print_decl(const skim_printer_t *pr, uint32_t token);
const skim_node_t *skim_print_node(const skim_printer_t *pr, uint32_t token);
uint32_t skim_print_scope_of(const skim_printer_t *pr, uint32_t token);
bool skim_print_declared(const skim_printer_t *pr, uint32_t name, uint32_t before, bool types);

uint32_t skim_print_import(skim_printer_t *pr, const skim_node_t *node);
uint32_t skim_print_import_equals(skim_printer_t *pr, const skim_node_t *node);
uint32_t skim_print_export(skim_printer_t *pr, const skim_node_t *node);
uint32_t skim_print_enum(skim_printer_t *pr, const skim_node_t *node);
uint32_t skim_print_enum_reference(skim_printer_t *pr, uint32_t token);
uint32_t skim_print_namespace(skim_printer_t *pr, const skim_node_t *node);
uint32_t skim_print_namespace_export(skim_printer_t *pr, const skim_node_t *node);
uint32_t skim_print_class(skim_printer_t *pr, const skim_node_t *node);
uint32_t skim_print_constructor(skim_printer_t *pr, const skim_node_t *node);
uint32_t skim_print_field(skim_printer_t *pr, const skim_node_t *node);

void skim_print_number(skim_printer_t *pr, uint32_t token);
void skim_print_string(skim_printer_t *pr, uint32_t token);

bool skim_legacy_octal_literal_at(const char *src, size_t len, size_t i);
size_t skim_copy_legacy_octal_literal(skim_str_t *out, const char *src, size_t len, size_t i);
size_t skim_copy_decimal_number_literal(skim_str_t *out, const char *src, size_t len, size_t i, bool in_expression);
bool skim_based_number_literal_at(const char *src, size_t len, size_t i);
size_t skim_copy_based_number_literal(skim_str_t *out, const char *src, size_t len, size_t i, bool in_expression);

#endif
