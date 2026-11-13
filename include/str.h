#ifndef SKIM_STR_H
#define SKIM_STR_H

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

typedef struct {
  char *data;
  size_t len;
  size_t cap;
} skim_str_t;

void skim_die(const char *msg);
void skim_str_grow(skim_str_t *s, size_t extra);
char *skim_slice_dup(const char *src, size_t start, size_t end);

static inline void skim_str_reserve(skim_str_t *s, size_t extra) {
  if (s->len + extra + 1 > s->cap) skim_str_grow(s, extra);
}

void skim_str_putc(skim_str_t *s, char c);
void skim_str_putn(skim_str_t *s, const char *p, size_t n);
void skim_str_puts(skim_str_t *s, const char *p);

#endif
