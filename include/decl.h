#ifndef SKIM_DECL_H
#define SKIM_DECL_H

#include <stdbool.h>
#include <stddef.h>

#define SKIM_BUCKET_COUNT 4096u
#define SKIM_DECL_VALUE 1u
#define SKIM_DECL_ENUM 2u
#define SKIM_DECL_NAMESPACE 4u

typedef struct {
  const char *src;
  const char *name;
  size_t name_len;
  size_t pos;
  unsigned flags;
  size_t bucket;
  size_t next;
} skim_decl_t;

typedef struct {
  skim_decl_t *decls;
  size_t decl_count;
  size_t decl_cap;
  size_t *decl_buckets;
} skim_decl_table_t;

void skim_decl_reset(void);
void skim_decl_remember(const char *src, const char *name_start, const char *name_end, size_t pos, unsigned flags);
bool skim_decl_seen_before(const char *src, const char *name, size_t pos, unsigned flags);

#endif
