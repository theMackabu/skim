#include "skim_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CC_WORD (SKIM_CC_ID_START | SKIM_CC_ID_PART | SKIM_CC_EMIT_DISPATCH)
#define CC_NUM (SKIM_CC_ID_PART | SKIM_CC_DIGIT | SKIM_CC_EMIT_DISPATCH)
#define CC_D SKIM_CC_EMIT_DISPATCH
#define CC_C SKIM_CC_CLEANUP_SPECIAL
#define CC_WS (SKIM_CC_SPACE | SKIM_CC_CLEANUP_SPECIAL)
#define CC_QUOTE                                                                                                       \
  (SKIM_CC_EMIT_DISPATCH | SKIM_CC_CLEANUP_SPECIAL | SKIM_CC_MARKER | SKIM_CC_BALANCED | SKIM_CC_ITEM_END |            \
   SKIM_CC_ANGLE)
#define CC_OPEN_CLOSE (SKIM_CC_BRACKET | SKIM_CC_BACK_STOP | SKIM_CC_BALANCED | SKIM_CC_ITEM_END | SKIM_CC_ANGLE)
#define CC_DECL (CC_WORD | SKIM_CC_DECL_START)
#define CC_MARK_DECL (CC_DECL | SKIM_CC_MARKER)

const unsigned short skim_char_class[256] = {
  ['a'] = CC_WORD,  ['b'] = CC_WORD,  ['c'] = CC_DECL,  ['d'] = CC_WORD,
  ['e'] = CC_MARK_DECL,               ['f'] = CC_DECL,  ['g'] = CC_WORD,  ['h'] = CC_WORD,
  ['i'] = CC_MARK_DECL,               ['j'] = CC_WORD,  ['k'] = CC_WORD,  ['l'] = CC_DECL,
  ['m'] = CC_WORD,  ['n'] = CC_DECL,  ['o'] = CC_WORD,  ['p'] = CC_WORD,  ['q'] = CC_WORD,
  ['r'] = CC_WORD,  ['s'] = CC_WORD,  ['t'] = CC_DECL,  ['u'] = CC_WORD,  ['v'] = CC_DECL,
  ['w'] = CC_WORD,  ['x'] = CC_WORD,  ['y'] = CC_WORD,  ['z'] = CC_WORD,
  ['A'] = CC_WORD,  ['B'] = CC_WORD,  ['C'] = CC_WORD,  ['D'] = CC_WORD,  ['E'] = CC_WORD,
  ['F'] = CC_WORD,  ['G'] = CC_WORD,  ['H'] = CC_WORD,  ['I'] = CC_WORD,  ['J'] = CC_WORD,
  ['K'] = CC_WORD,  ['L'] = CC_WORD,  ['M'] = CC_WORD,  ['N'] = CC_WORD,  ['O'] = CC_WORD,
  ['P'] = CC_WORD,  ['Q'] = CC_WORD,  ['R'] = CC_WORD,  ['S'] = CC_WORD,  ['T'] = CC_WORD,
  ['U'] = CC_WORD,  ['V'] = CC_WORD,  ['W'] = CC_WORD,  ['X'] = CC_WORD,  ['Y'] = CC_WORD,
  ['Z'] = CC_WORD,  ['_'] = CC_WORD,  ['$'] = CC_WORD,
  ['0'] = CC_NUM,   ['1'] = CC_NUM,   ['2'] = CC_NUM,   ['3'] = CC_NUM,   ['4'] = CC_NUM,
  ['5'] = CC_NUM,   ['6'] = CC_NUM,   ['7'] = CC_NUM,   ['8'] = CC_NUM,   ['9'] = CC_NUM,
  [' '] = CC_WS,
  ['\t'] = CC_WS,
  ['\n'] = CC_WS | SKIM_CC_ITEM_END,
  ['\v'] = CC_WS,
  ['\f'] = CC_WS,
  ['\r'] = CC_WS | SKIM_CC_ITEM_END,
  ['\''] = CC_QUOTE,
  ['"'] = CC_QUOTE,
  ['`'] = CC_QUOTE,
  ['/'] = CC_QUOTE & ~SKIM_CC_ANGLE,
  ['('] = CC_D | CC_C | CC_OPEN_CLOSE,
  [')'] = CC_C | CC_OPEN_CLOSE,
  ['{'] = CC_D | CC_C | CC_OPEN_CLOSE,
  ['}'] = CC_D | CC_OPEN_CLOSE,
  ['['] = CC_OPEN_CLOSE,
  [']'] = CC_OPEN_CLOSE,
  ['<'] = CC_D | SKIM_CC_ANGLE,
  ['>'] = SKIM_CC_ANGLE,
  [';'] = CC_C | SKIM_CC_BACK_STOP | SKIM_CC_ITEM_END | SKIM_CC_ANGLE,
  [':'] = CC_D | SKIM_CC_BACK_STOP | SKIM_CC_ITEM_END,
  ['?'] = CC_D | SKIM_CC_BACK_STOP,
  [','] = CC_D,
  ['@'] = CC_D,
  ['!'] = CC_D,
  ['='] = CC_C | SKIM_CC_ANGLE,
};

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

char *skim_slice_dup(const char *src, size_t start, size_t end) {
  char *s = malloc(end - start + 1);
  if (!s) skim_die("out of memory");
  memcpy(s, src + start, end - start);
  s[end - start] = '\0';
  return s;
}

static skim_scan_cache_t scan_cache;

static void invalidate_position_caches(void) {
  skim_ts_enum_scope_invalidate();
  skim_ts_annotations_memo_invalidate();
  skim_ts_namespace_decl_index_invalidate();
  skim_ts_module_index_invalidate();
}

void skim_scan_cache_enter(skim_scan_cache_t *saved, const char *src, size_t len) {
  *saved = scan_cache;
  scan_cache = (skim_scan_cache_t){.src = src, .len = len};
  invalidate_position_caches();
}

void skim_scan_cache_leave(const skim_scan_cache_t *saved) {
  free(scan_cache.pairs);
  free(scan_cache.pair_buckets);
  scan_cache = *saved;
  invalidate_position_caches();
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

bool skim_colon_looks_like_ternary(const char *src, size_t colon) {
  int paren = 0, bracket = 0, brace = 0;
  for (size_t k = colon; k > 0;) {
    char c = src[--k];
    if (!skim_char_has(c, SKIM_CC_BACK_STOP)) continue;
    if ((c == ')' || c == ']' || c == '}') && skim_back_skip_block(src, &k)) continue;
    if (c == ')') paren++;
    else if (c == '(' && paren > 0) paren--;
    else if (c == '(' && paren == 0 && bracket == 0 && brace == 0) return false;
    else if (c == ']') bracket++;
    else if (c == '[' && bracket > 0) bracket--;
    else if (c == '}') brace++;
    else if (c == '{') {
      if (brace == 0 && paren == 0 && bracket == 0) return false;
      if (brace > 0) brace--;
    } else if (c == '?' && paren == 0 && bracket == 0 && brace == 0) {
      if (k + 1 == colon && k > 0 && skim_is_id_part(src[k - 1])) return false;
      return true;
    } else if ((c == ';' || c == ':') && paren == 0 && bracket == 0 && brace == 0) {
      return false;
    }
  }
  return false;
}

bool skim_colon_looks_like_case_label(const char *src, size_t colon) {
  size_t k = colon;
  while (k > 0 && src[k - 1] != '\n' && src[k - 1] != '\r' && src[k - 1] != ';')
    k--;
  size_t line = skim_skip_ws(src, colon, k);
  if (!skim_word_at(src, colon, line, "case") && !skim_word_at(src, colon, line, "default")) return false;
  int paren = 0, bracket = 0, brace = 0;
  for (size_t p = line; p < colon; p++) {
    char c = src[p];
    if (c == '\'' || c == '"' || c == '`') {
      p = skim_skip_string_raw(src, colon, p) - 1;
      continue;
    }
    if (c == '(') paren++;
    else if (c == ')' && paren > 0) paren--;
    else if (c == '[') bracket++;
    else if (c == ']' && bracket > 0) bracket--;
    else if (c == '{') brace++;
    else if (c == '}' && brace > 0) brace--;
  }
  return paren == 0 && bracket == 0 && brace == 0;
}

bool skim_has_effective_code(const char *src, size_t len) {
  for (size_t i = 0; i < len;) {
    if (skim_is_space(src[i])) {
      i++;
      continue;
    }
    if (skim_skip_comment(src, len, &i)) continue;
    return true;
  }
  return false;
}

size_t skim_skip_ws_comments_slow(const char *src, size_t len, size_t i) {
  for (;;) {
    i = skim_skip_ws(src, len, i);
    if (skim_skip_comment(src, len, &i)) continue;
    return i;
  }
}

size_t skim_skip_type_alias(const char *src, size_t len, size_t i) {
  int paren = 0, bracket = 0, brace = 0, angle = 0;
  bool seen_type_token = false;
  char last_sig = '\0';
  while (i < len) {
    char c = src[i];
    if (skim_is_space(c) || (c == '/' && i + 1 < len && (src[i + 1] == '/' || src[i + 1] == '*'))) {
      size_t next = skim_skip_ws_comments(src, len, i);
      bool newline = skim_range_has_newline(src, i, next);
      if (newline && paren == 0 && bracket == 0 && brace == 0 && angle == 0 && seen_type_token) {
        bool needs_type = last_sig == '=' || last_sig == '|' || last_sig == '&' || last_sig == '?' || last_sig == ':' || last_sig == ',';
        bool continues = next < len && (src[next] == '|' || src[next] == '&' || src[next] == '?' || src[next] == ':');
        if (!needs_type && !continues) return i;
      }
      if (next > i) {
        i = next;
        continue;
      }
    }
    if (c == '\'' || c == '"' || c == '`') {
      seen_type_token = true;
      last_sig = c;
      i = skim_skip_string_raw(src, len, i);
      continue;
    }
    if (c == '(') paren++;
    else if (c == ')') {
      if (paren == 0) return i;
      paren--;
    }
    else if (c == '[') bracket++;
    else if (c == ']') {
      if (bracket == 0) return i;
      bracket--;
    }
    else if (c == '{') brace++;
    else if (c == '}') {
      if (brace == 0) return i;
      brace--;
    }
    else if (c == '<') angle++;
    else if (c == '>' && angle > 0 && (i == 0 || src[i - 1] != '=')) angle--;
    else if (c == ';' && paren == 0 && bracket == 0 && brace == 0 && angle == 0) return i + 1;
    if (!skim_is_space(c)) {
      seen_type_token = true;
      last_sig = c;
    }
    i++;
  }
  return i;
}

bool skim_slash_starts_regex(const char *src, size_t start, size_t i) {
  bool saw_newline = false;
  for (;;) {
    while (i > start && skim_is_space(src[i - 1])) {
      if (src[i - 1] == '\n' || src[i - 1] == '\r') saw_newline = true;
      i--;
    }
    if (i < start + 2 || src[i - 2] != '*' || src[i - 1] != '/') break;
    size_t comment = i - 2;
    while (comment > start && !(src[comment - 1] == '/' && src[comment] == '*'))
      comment--;
    if (comment == start) break;
    i = comment - 1;
  }
  if (i == start) return true;
  if (saw_newline) return true;
  size_t word_start = i;
  while (word_start > start && skim_is_id_part(src[word_start - 1]))
    word_start--;
  if (word_start < i && (word_start == start || src[word_start - 1] != '.')) {
    static const char *words[] = {
      "return", "throw", "yield", "await", "case", "delete", "void", "typeof", "in", "instanceof"
    };
    for (size_t k = 0; k < sizeof(words) / sizeof(words[0]); k++) {
      size_t n = strlen(words[k]);
      if (i - word_start == n && memcmp(src + word_start, words[k], n) == 0) return true;
    }
  }
  char c = src[i - 1];
  if ((c == '-' || c == '+') && i >= 2 && src[i - 2] == c) return false;
  return c == '(' || c == '[' || c == '{' || c == ',' || c == ';' || c == ':' || c == '=' || c == '!' || c == '?' ||
         c == '&' || c == '|' || c == '+' || c == '-' || c == '*' || c == '~' || c == '^' || c == '<' || c == '>';
}

size_t skim_skip_regex_literal(const char *src, size_t len, size_t i) {
  bool in_class = false;
  i++;
  while (i < len) {
    char c = src[i++];
    if (c == '\\' && i < len) {
      i++;
      continue;
    }
    if (c == '[') in_class = true;
    else if (c == ']') in_class = false;
    else if (c == '/' && !in_class) break;
    else if (c == '\n' || c == '\r') break;
  }
  while (i < len && skim_is_id_part(src[i]))
    i++;
  return i;
}

size_t skim_skip_string_raw(const char *src, size_t len, size_t i) {
  char quote = src[i++];
  while (i < len) {
    char c = src[i++];
    if (c == '\\' && i < len && quote != '`') {
      i++;
      continue;
    }
    if (quote == '`' && c == '$' && i < len && src[i] == '{') {
      i = skim_skip_balanced(src, len, i, '{', '}');
      continue;
    }
    if (c == quote) break;
  }
  return i;
}

size_t skim_skip_balanced(const char *src, size_t len, size_t i, char open, char close) {
  size_t start = i;
  int depth = 0;
  while (i < len) {
    if (!skim_char_has(src[i], SKIM_CC_BALANCED)) {
      i++;
      continue;
    }
    if (src[i] == '\'' || src[i] == '"' || src[i] == '`') {
      i = skim_skip_string_raw(src, len, i);
      continue;
    }
    if (skim_skip_comment(src, len, &i)) continue;
    if (src[i] == '/' && skim_slash_starts_regex(src, start, i)) {
      i = skim_skip_regex_literal(src, len, i);
      continue;
    }
    if (src[i] == open) depth++;
    if (src[i] == close) {
      depth--;
      if (depth == 0) return i + 1;
    }
    i++;
  }
  return i;
}

size_t skim_skip_statement_like(const char *src, size_t len, size_t i) {
  while (i < len) {
    if (src[i] == '\'' || src[i] == '"' || src[i] == '`') {
      i = skim_skip_string_raw(src, len, i);
      continue;
    }
    if (i + 1 < len && src[i] == '/' && src[i + 1] == '/') {
      while (i < len && src[i] != '\n')
        i++;
      return i;
    }
    if (i + 1 < len && src[i] == '/' && src[i + 1] == '*') {
      i = skim_skip_block_comment(src, len, i + 2);
      continue;
    }
    if (src[i] == '{') {
      i = skim_skip_balanced(src, len, i, '{', '}');
      continue;
    }
    if (src[i] == ';') return i + 1;
    i++;
  }
  return i;
}

size_t skim_parse_identifier(const char *src, size_t len, size_t i, size_t *start, size_t *end) {
  i = skim_skip_ws_comments(src, len, i);
  if (i >= len || !skim_is_id_start(src[i])) return i;
  *start = i;
  i++;
  while (i < len && skim_is_id_part(src[i]))
    i++;
  *end = i;
  return i;
}

void skim_emit_preserved_newlines(skim_str_t *out, const char *src, size_t from, size_t to) {
  while (from < to) {
    const char *newline = memchr(src + from, '\n', to - from);
    if (!newline) return;
    skim_str_putc(out, '\n');
    from = (size_t)(newline - src) + 1;
  }
}

size_t skim_copy_string(skim_str_t *out, const char *src, size_t len, size_t i) {
  char quote = src[i++];
  skim_str_putc(out, quote);
  while (i < len) {
    size_t run = i;
    while (run < len && src[run] != quote && src[run] != '\\' && src[run] != '$' && src[run] != '\0')
      run++;
    if (run > i) {
      skim_str_putn(out, src + i, run - i);
      i = run;
      if (i >= len) break;
    }
    char c = src[i++];
    if (c == '\0') {
      skim_str_puts(out, "\\0");
      continue;
    }
    if (c == '\\' && i < len && quote != '`') {
      if (src[i] == '\n' || src[i] == '\r') {
        if (src[i] == '\r' && i + 1 < len && src[i + 1] == '\n') i += 2;
        else i++;
        while (i < len && (src[i] == ' ' || src[i] == '\t'))
          i++;
        continue;
      }
      if (src[i] == 'u' && i + 2 < len && src[i + 1] == '{') {
        size_t j = i + 2;
        unsigned value = 0;
        bool ok = false;
        while (j < len && src[j] != '}') {
          int digit = -1;
          if (src[j] >= '0' && src[j] <= '9') digit = src[j] - '0';
          else if (src[j] >= 'a' && src[j] <= 'f') digit = src[j] - 'a' + 10;
          else if (src[j] >= 'A' && src[j] <= 'F') digit = src[j] - 'A' + 10;
          else break;
          value = value * 16u + (unsigned)digit;
          ok = true;
          j++;
        }
        if (ok && j < len && src[j] == '}' && value == 0) {
          skim_str_puts(out, "\\0");
          i = j + 1;
          continue;
        }
      }
      if (
        src[i] == 'u' && i + 4 < len && src[i + 1] == '0' && src[i + 2] == '0' && src[i + 3] == '0' && src[i + 4] == '0'
      ) {
        skim_str_puts(out, "\\0");
        i += 5;
        continue;
      }
      if (src[i] == 'x' && i + 2 < len && src[i + 1] == '0' && src[i + 2] == '0') {
        skim_str_puts(out, "\\0");
        i += 3;
        continue;
      }
      skim_str_putc(out, c);
      skim_str_putc(out, src[i++]);
      continue;
    }
    skim_str_putc(out, c);
    if (quote == '`' && c == '$' && i < len && src[i] == '{') {
      skim_str_putc(out, src[i++]);
      size_t expr_start = i;
      size_t expr_end = skim_skip_balanced(src, len, i - 1, '{', '}');
      if (expr_end == 0 || expr_end <= expr_start) continue;
      skim_transform_range(src, len, expr_start, expr_end - 1, out);
      skim_str_putc(out, '}');
      i = expr_end;
      continue;
    }
    if (c == quote) break;
  }
  return i;
}

size_t skim_copy_line_comment(skim_str_t *out, const char *src, size_t len, size_t i) {
  if (i >= len) return i;
  const char *newline = memchr(src + i, '\n', len - i);
  size_t end = newline ? (size_t)(newline - src) + 1 : len;
  skim_str_putn(out, src + i, end - i);
  return end;
}

size_t skim_copy_block_comment(skim_str_t *out, const char *src, size_t len, size_t i) {
  skim_str_putn(out, src + i, 2);
  i += 2;
  if (i >= len) return i;
  size_t close = skim_find_block_comment_close(src, len, i);
  size_t end = close < len ? close + 2 : len;
  skim_str_putn(out, src + i, end - i);
  return end;
}
