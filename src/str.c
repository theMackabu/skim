#include "str.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void skim_die(const char *msg) {
  fprintf(stderr, "%s\n", msg);
  exit(1);
}

void skim_str_grow(skim_str_t *s, size_t extra) {
  size_t need = s->len + extra + 1;
  size_t cap = s->cap ? s->cap : 4096;
  while (cap < need)
    cap *= 2;
  char *next = realloc(s->data, cap);
  if (!next) skim_die("out of memory");
  s->data = next;
  s->cap = cap;
  s->data[s->len] = '\0';
}

void skim_str_putc(skim_str_t *s, char c) {
  skim_str_reserve(s, 1);
  s->data[s->len++] = c;
  s->data[s->len] = '\0';
}

void skim_str_putn(skim_str_t *s, const char *p, size_t n) {
  skim_str_reserve(s, n);
  memcpy(s->data + s->len, p, n);
  s->len += n;
  s->data[s->len] = '\0';
}

void skim_str_puts(skim_str_t *s, const char *p) {
  skim_str_putn(s, p, strlen(p));
}

char *skim_slice_dup(const char *src, size_t start, size_t end) {
  char *s = malloc(end - start + 1);
  if (!s) skim_die("out of memory");
  memcpy(s, src + start, end - start);
  s[end - start] = '\0';
  return s;
}
