#ifndef SKIM_SCAN_CACHE_H
#define SKIM_SCAN_CACHE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint32_t close;
  uint32_t open;
} skim_bracket_pair_t;

typedef struct {
  const char *src;
  size_t len;
  skim_bracket_pair_t *pairs;
  uint32_t *pair_buckets;
  bool pairs_built;
} skim_scan_cache_t;

void skim_scan_cache_enter(skim_scan_cache_t *saved, const char *src, size_t len);
void skim_scan_cache_leave(const skim_scan_cache_t *saved);
bool skim_scan_cache_owns(const char *src);
size_t skim_scan_cache_len(void);
bool skim_back_skip_block(const char *src, size_t *k);

#endif
