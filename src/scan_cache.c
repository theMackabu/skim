#include "chars.h"
#include "scan_cache.h"
#include "str.h"

#include <stdint.h>
#include <stdlib.h>

static skim_scan_cache_t scan_cache;

void skim_scan_cache_enter(skim_scan_cache_t *saved, const char *src, size_t len) {
  *saved = scan_cache;
  scan_cache = (skim_scan_cache_t){.src = src, .len = len};
}

void skim_scan_cache_leave(const skim_scan_cache_t *saved) {
  free(scan_cache.pairs);
  free(scan_cache.pair_buckets);
  scan_cache = *saved;
}

bool skim_scan_cache_owns(const char *src) {
  return src == scan_cache.src;
}

size_t skim_scan_cache_len(void) {
  return scan_cache.len;
}

enum {
  MIN_INDEXED_SOURCE = 4096,
  MIN_INDEXED_BLOCK = 16,
  PAIR_BUCKET_SHIFT = 8,
};

static void build_bracket_pairs(skim_scan_cache_t *cache) {
  const char *src = cache->src;
  size_t len = cache->len;
  size_t bucket_count = (len >> PAIR_BUCKET_SHIFT) + 2;
  size_t pair_count = 0, pair_cap = 256;
  size_t depth = 0, stack_cap = 256;
  uint32_t *stack = malloc(sizeof(*stack) * stack_cap);
  skim_bracket_pair_t *pairs = malloc(sizeof(*pairs) * pair_cap);
  uint32_t *buckets = malloc(sizeof(*buckets) * bucket_count);
  if (!stack || !pairs || !buckets) skim_die("out of memory");

  size_t next_bucket = 0;
  for (size_t i = 0; i < len; i++) {
    if (!skim_char_has(src[i], SKIM_CC_BRACKET)) continue;
    char c = src[i];
    if (c == '(' || c == '[' || c == '{') {
      if (depth == stack_cap) {
        stack_cap *= 2;
        uint32_t *next = realloc(stack, sizeof(*stack) * stack_cap);
        if (!next) skim_die("out of memory");
        stack = next;
      }
      stack[depth++] = (uint32_t)i;
      continue;
    }
    if (depth == 0) continue;
    char opener = c == ')' ? '(' : c == ']' ? '[' : '{';
    if (src[stack[depth - 1]] != opener) {
      depth = 0;
      continue;
    }
    size_t open = stack[--depth];
    if (i - open < MIN_INDEXED_BLOCK) continue;
    if (pair_count == pair_cap) {
      pair_cap *= 2;
      skim_bracket_pair_t *next = realloc(pairs, sizeof(*pairs) * pair_cap);
      if (!next) skim_die("out of memory");
      pairs = next;
    }
    while (next_bucket <= (i >> PAIR_BUCKET_SHIFT))
      buckets[next_bucket++] = (uint32_t)pair_count;
    pairs[pair_count++] = (skim_bracket_pair_t){.close = (uint32_t)i, .open = (uint32_t)open};
  }
  while (next_bucket < bucket_count)
    buckets[next_bucket++] = (uint32_t)pair_count;
  free(stack);
  cache->pairs = pairs;
  cache->pair_buckets = buckets;
  cache->pairs_built = true;
}

bool skim_back_skip_block(const char *src, size_t *k) {
  if (src != scan_cache.src || scan_cache.len < MIN_INDEXED_SOURCE || scan_cache.len > UINT32_MAX) return false;
  if (!scan_cache.pairs_built) build_bracket_pairs(&scan_cache);
  size_t bucket = *k >> PAIR_BUCKET_SHIFT;
  uint32_t lo = scan_cache.pair_buckets[bucket], hi = scan_cache.pair_buckets[bucket + 1];
  while (lo < hi) {
    uint32_t mid = lo + (hi - lo) / 2;
    if (scan_cache.pairs[mid].close < *k) lo = mid + 1;
    else hi = mid;
  }
  if (lo == scan_cache.pair_buckets[bucket + 1] || scan_cache.pairs[lo].close != *k) return false;
  *k = scan_cache.pairs[lo].open;
  return true;
}
