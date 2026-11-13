#include "chars.h"
#include "str.h"
#include "token.h"

#include <stdlib.h>
#include <string.h>

constexpr uint32_t KEYWORD_SLOTS = 256;

static const char *const keyword_text[SKIM_KW_COUNT] = {
#define SKIM_KEYWORD(name, text, attrs) [SKIM_KW_##name] = text,
#include "keyword_list.h"
};

static const uint8_t keyword_len[SKIM_KW_COUNT] = {
#define SKIM_KEYWORD(name, text, attrs) [SKIM_KW_##name] = sizeof(text) - 1,
#include "keyword_list.h"
};

const uint8_t skim_keyword_attrs[SKIM_KW_COUNT] = {
#define SKIM_KEYWORD(name, text, attrs) [SKIM_KW_##name] = attrs,
#include "keyword_list.h"
};

static thread_local uint8_t keyword_slots[KEYWORD_SLOTS];
static thread_local bool keyword_slots_ready;

static inline unsigned keyword_hash(const char *text, size_t n) {
  return ((unsigned char)text[0] * 7u + (unsigned char)text[n - 1] * 13u + (unsigned)n * 3u) & (KEYWORD_SLOTS - 1);
}

static void keyword_slots_build(void) {
  for (unsigned id = 1; id < SKIM_KW_COUNT; id++) {
    unsigned slot = keyword_hash(keyword_text[id], keyword_len[id]);
    while (keyword_slots[slot])
      slot = (slot + 1) & (KEYWORD_SLOTS - 1);
    keyword_slots[slot] = (uint8_t)id;
  }
  keyword_slots_ready = true;
}

static inline uint8_t keyword_lookup(const char *text, size_t n) {
  if (n < 2 || n > 11 || text[0] < 'a') return SKIM_KW_NONE;
  for (unsigned slot = keyword_hash(text, n);; slot = (slot + 1) & (KEYWORD_SLOTS - 1)) {
    uint8_t id = keyword_slots[slot];
    if (!id) return SKIM_KW_NONE;
    if (keyword_len[id] == n && memcmp(keyword_text[id], text, n) == 0) return id;
  }
}

static inline bool is_newline(char c) {
  return c == '\n' || c == '\r';
}

typedef struct {
  skim_tokens_t *tokens;
  const char *src;
  size_t len;
  uint32_t depth;
} lexer_t;

static inline skim_token_t *push_token(lexer_t *lx, size_t start, size_t end, uint8_t kind, uint16_t flags) {
  skim_tokens_t *tokens = lx->tokens;
  if (tokens->count + 3 > tokens->cap) {
    uint32_t cap = tokens->cap ? tokens->cap * 2 : 1024;
    skim_token_t *items = realloc(tokens->items, sizeof(*items) * cap);
    if (!items) skim_die("out of memory");
    tokens->items = items;
    tokens->cap = cap;
  }
  skim_token_t *token = &tokens->items[tokens->count++];
  *token = (skim_token_t){.start = (uint32_t)start, .end = (uint32_t)end, .kind = kind, .flags = flags};
  return token;
}

static void push_open(lexer_t *lx, uint32_t index) {
  skim_tokens_t *tokens = lx->tokens;
  if (lx->depth == tokens->stack_cap) {
    uint32_t cap = tokens->stack_cap ? tokens->stack_cap * 2 : 256;
    uint32_t *stack = realloc(tokens->stack, sizeof(*stack) * cap);
    if (!stack) skim_die("out of memory");
    tokens->stack = stack;
    tokens->stack_cap = cap;
  }
  tokens->stack[lx->depth++] = index;
}

static bool regex_allowed(const lexer_t *lx) {
  const skim_tokens_t *tokens = lx->tokens;
  if (tokens->count == 0) return true;
  const skim_token_t *prev = &tokens->items[tokens->count - 1];
  switch (prev->kind) {
  case SKIM_T_WORD: {
    if (!skim_keyword_attrs[prev->word]) return false;
    if (tokens->count < 2) return true;
    uint8_t before = tokens->items[tokens->count - 2].kind;
    return before != '.' && before != SKIM_T_CHAIN;
  }
  case SKIM_T_PRIVATE:
  case SKIM_T_NUMBER:
  case SKIM_T_STRING:
  case SKIM_T_TEMPLATE:
  case SKIM_T_REGEX:
  case SKIM_T_INCDEC:
  case ')':
  case ']':
  case '}':
    return false;
  default:
    return true;
  }
}

static size_t scan_string(const char *src, size_t len, size_t i, uint16_t *flags) {
  char quote = src[i++];
  while (i < len) {
    char c = src[i];
    if (c == quote) return i + 1;
    if (c == '\\') {
      *flags |= SKIM_TF_REWRITE;
      i += i + 2 < len && src[i + 1] == '\r' && src[i + 2] == '\n' ? 3 : 2;
      continue;
    }
    if (c == '\0') *flags |= SKIM_TF_REWRITE;
    if (is_newline(c)) return i;
    i++;
  }
  return len;
}

static size_t scan_template_chunk(const char *src, size_t len, size_t i, bool *open) {
  *open = false;
  while (i < len) {
    char c = src[i];
    if (c == '`') return i + 1;
    if (c == '\\') {
      i += 2;
      continue;
    }
    if (c == '$' && i + 1 < len && src[i + 1] == '{') {
      *open = true;
      return i + 2;
    }
    i++;
  }
  return len;
}

static size_t scan_regex(const char *src, size_t len, size_t i) {
  bool in_class = false;
  i++;
  while (i < len) {
    char c = src[i++];
    if (c == '\\' && i < len) i++;
    else if (c == '[') in_class = true;
    else if (c == ']') in_class = false;
    else if ((c == '/' && !in_class) || is_newline(c)) break;
  }
  while (i < len && skim_is_id_part(src[i]))
    i++;
  return i;
}

static size_t scan_number(const char *src, size_t len, size_t i, uint16_t *flags) {
  size_t start = i;
  if (src[i] == '0' && i + 1 < len && skim_is_id_part(src[i + 1])) {
    *flags |= SKIM_TF_REWRITE;
    i += 2;
    while (i < len && skim_is_id_part(src[i]))
      i++;
    if (!skim_is_digit(src[start + 1])) return i;
    i = start;
  }
  while (i < len && (skim_is_digit(src[i]) || src[i] == '_'))
    i++;
  if (i < len && src[i] == '.') {
    i++;
    while (i < len && (skim_is_digit(src[i]) || src[i] == '_'))
      i++;
  }
  if (i < len && (src[i] == 'e' || src[i] == 'E')) {
    size_t exp = i + 1;
    if (exp < len && (src[exp] == '+' || src[exp] == '-')) exp++;
    if (exp < len && skim_is_digit(src[exp])) {
      i = exp;
      while (i < len && (skim_is_digit(src[i]) || src[i] == '_'))
        i++;
    }
  }
  if (i < len && src[i] == 'n') i++;
  for (size_t k = start; k < i && !(*flags & SKIM_TF_REWRITE); k++)
    if (!skim_is_digit(src[k])) *flags |= SKIM_TF_REWRITE;
  if (i - start >= 12 || (i - start >= 4 && memcmp(src + i - 3, "000", 3) == 0)) *flags |= SKIM_TF_REWRITE;
  return i;
}

static void template_piece(lexer_t *lx, size_t start, uint16_t flags, bool continues) {
  skim_tokens_t *tokens = lx->tokens;
  bool open = false;
  size_t end = scan_template_chunk(lx->src, lx->len, start + 1, &open);
  uint32_t index = tokens->count;
  skim_token_t *token = push_token(lx, start, end, SKIM_T_TEMPLATE, flags);
  if (continues) {
    uint32_t prev = tokens->stack[--lx->depth];
    tokens->items[prev].pair = index;
    token->pair = prev;
  }
  if (open) push_open(lx, index);
  else token->flags |= SKIM_TF_TAIL;
}

static size_t scan_operator(const char *src, size_t len, size_t i, uint8_t *kind) {
  char c = src[i];
  char n1 = i + 1 < len ? src[i + 1] : '\0';
  char n2 = i + 2 < len ? src[i + 2] : '\0';
  *kind = (uint8_t)c;
  switch (c) {
  case '.':
    if (n1 == '.' && n2 == '.') {
      *kind = SKIM_T_SPREAD;
      return i + 3;
    }
    return i + 1;
  case '?':
    if (n1 == '.' && !skim_is_digit(n2)) {
      *kind = SKIM_T_CHAIN;
      return i + 2;
    }
    if (n1 == '?') {
      *kind = SKIM_T_OP;
      return i + (n2 == '=' ? 3 : 2);
    }
    return i + 1;
  case '=':
    if (n1 == '>') {
      *kind = SKIM_T_ARROW;
      return i + 2;
    }
    if (n1 == '=') {
      *kind = SKIM_T_OP;
      return i + (n2 == '=' ? 3 : 2);
    }
    return i + 1;
  case '!':
    if (n1 == '=') {
      *kind = SKIM_T_OP;
      return i + (n2 == '=' ? 3 : 2);
    }
    return i + 1;
  case '+':
  case '-':
    if (n1 == c) {
      *kind = SKIM_T_INCDEC;
      return i + 2;
    }
    if (n1 == '=') {
      *kind = SKIM_T_OP;
      return i + 2;
    }
    return i + 1;
  case '*':
    if (n1 == '*') {
      *kind = SKIM_T_OP;
      return i + (n2 == '=' ? 3 : 2);
    }
    if (n1 == '=') {
      *kind = SKIM_T_OP;
      return i + 2;
    }
    return i + 1;
  case '&':
  case '|':
    if (n1 == c) {
      *kind = SKIM_T_OP;
      return i + (n2 == '=' ? 3 : 2);
    }
    if (n1 == '=') {
      *kind = SKIM_T_OP;
      return i + 2;
    }
    return i + 1;
  case '/':
  case '%':
  case '^':
    if (n1 == '=') {
      *kind = SKIM_T_OP;
      return i + 2;
    }
    if (c != '/') *kind = SKIM_T_OP;
    return i + 1;
  default:
    return i + 1;
  }
}

void skim_lex(skim_tokens_t *tokens, const char *src, size_t len) {
  if (!keyword_slots_ready) keyword_slots_build();
  lexer_t lx = {.tokens = tokens, .src = src, .len = len};
  tokens->count = 0;
  uint16_t flags = 0;
  size_t i = 0;
  if (len >= 3 && memcmp(src, "\xef\xbb\xbf", 3) == 0) i = 3;
  if (i + 1 < len && src[i] == '#' && src[i + 1] == '!') {
    while (i < len && !is_newline(src[i]))
      i++;
  }

  while (i < len) {
    char c = src[i];
    if (skim_is_space(c)) {
      if (is_newline(c)) flags |= SKIM_TF_NEWLINE;
      i++;
      continue;
    }
    if (skim_is_id_start(c)) {
      size_t start = i++;
      while (i < len && skim_is_id_part(src[i]))
        i++;
      push_token(&lx, start, i, SKIM_T_WORD, flags)->word = keyword_lookup(src + start, i - start);
      flags = 0;
      continue;
    }
    if (skim_is_digit(c) || (c == '.' && i + 1 < len && skim_is_digit(src[i + 1]))) {
      size_t start = i;
      if (c == '.') flags |= SKIM_TF_REWRITE;
      i = scan_number(src, len, i, &flags);
      push_token(&lx, start, i, SKIM_T_NUMBER, flags);
      flags = 0;
      continue;
    }

    size_t start = i;
    uint32_t index = tokens->count;
    switch (c) {
    case '\'':
    case '"':
      i = scan_string(src, len, i, &flags);
      push_token(&lx, start, i, SKIM_T_STRING, flags);
      break;
    case '`':
      template_piece(&lx, i, flags, false);
      i = tokens->items[index].end;
      break;
    case '(':
    case '[':
    case '{':
      push_token(&lx, i, i + 1, (uint8_t)c, flags);
      push_open(&lx, index);
      i++;
      break;
    case ')':
    case ']':
    case '}':
      if (lx.depth > 0 && tokens->items[tokens->stack[lx.depth - 1]].kind == SKIM_T_TEMPLATE && c == '}') {
        template_piece(&lx, i, flags, true);
        i = tokens->items[index].end;
        break;
      }
      push_token(&lx, i, i + 1, (uint8_t)c, flags)->pair = index;
      if (lx.depth > 0) {
        uint32_t open = tokens->stack[lx.depth - 1];
        char want = c == ')' ? '(' : c == ']' ? '[' : '{';
        if (tokens->items[open].kind == (uint8_t)want) {
          lx.depth--;
          tokens->items[open].pair = index;
          tokens->items[index].pair = open;
        }
      }
      i++;
      break;
    case '#':
      i++;
      while (i < len && skim_is_id_part(src[i]))
        i++;
      push_token(&lx, start, i, i - start > 1 ? SKIM_T_PRIVATE : (uint8_t)'#', flags);
      break;
    case '/':
      if (i + 1 < len && src[i + 1] == '/') {
        i += 2;
        while (i < len && !is_newline(src[i]))
          i++;
        continue;
      }
      if (i + 1 < len && src[i + 1] == '*') {
        i += 2;
        while (i < len && !(src[i] == '/' && src[i - 1] == '*' && i > start + 2)) {
          if (is_newline(src[i])) flags |= SKIM_TF_NEWLINE;
          i++;
        }
        if (i < len) i++;
        continue;
      }
      if (regex_allowed(&lx)) {
        i = scan_regex(src, len, i);
        push_token(&lx, start, i, SKIM_T_REGEX, flags);
        break;
      }
      [[fallthrough]];
    default: {
      uint8_t kind = 0;
      i = scan_operator(src, len, i, &kind);
      push_token(&lx, start, i, kind, flags);
      break;
    }
    }
    flags = 0;
  }

  uint32_t eof = tokens->count;
  push_token(&lx, len, len, SKIM_T_EOF, flags | SKIM_TF_NEWLINE)->pair = eof;
  push_token(&lx, len, len, SKIM_T_EOF, SKIM_TF_NEWLINE)->pair = eof;
  tokens->count = eof;
  while (lx.depth > 0)
    tokens->items[tokens->stack[--lx.depth]].pair = eof;
}

void skim_tokens_free(skim_tokens_t *tokens) {
  free(tokens->items);
  free(tokens->stack);
  *tokens = (skim_tokens_t){0};
}
