#include "chars.h"
#include "skip.h"
#include "syntax.h"

#include <string.h>

static skim_syntax_keyword_t keyword_from_span(const char *src, size_t start, size_t end) {
  size_t n = end - start;
#define KW(lit, kind)                                                                                                  \
  if (n == sizeof(lit) - 1 && memcmp(src + start, lit, sizeof(lit) - 1) == 0) return kind
  KW("import", SKIM_KEYWORD_IMPORT);
  KW("export", SKIM_KEYWORD_EXPORT);
  KW("interface", SKIM_KEYWORD_INTERFACE);
  KW("type", SKIM_KEYWORD_TYPE);
  KW("declare", SKIM_KEYWORD_DECLARE);
  KW("enum", SKIM_KEYWORD_ENUM);
  KW("namespace", SKIM_KEYWORD_NAMESPACE);
  KW("module", SKIM_KEYWORD_MODULE);
  KW("class", SKIM_KEYWORD_CLASS);
  KW("const", SKIM_KEYWORD_CONST);
#undef KW
  return SKIM_KEYWORD_NONE;
}

static size_t skip_trivia(const char *src, size_t len, size_t i) {
  while (i < len) {
    if (skim_is_space(src[i])) {
      i++;
      continue;
    }
    if (i + 1 < len && src[i] == '/' && src[i + 1] == '/') {
      i += 2;
      while (i < len && src[i] != '\n' && src[i] != '\r')
        i++;
      continue;
    }
    if (i + 1 < len && src[i] == '/' && src[i + 1] == '*') {
      i = skim_skip_block_comment(src, len, i + 2);
      continue;
    }
    break;
  }
  return i;
}

skim_syntax_token_t skim_syntax_token_at(const char *src, size_t len, size_t pos) {
  size_t start = skip_trivia(src, len, pos);
  if (start >= len) return (skim_syntax_token_t){.kind = SKIM_TOKEN_EOF, .start = len, .end = len};
  if (!skim_is_id_start(src[start])) {
    return (skim_syntax_token_t){.kind = SKIM_TOKEN_OTHER, .start = start, .end = start + 1};
  }
  size_t end = start + 1;
  while (end < len && skim_is_id_part(src[end]))
    end++;
  skim_syntax_keyword_t keyword = keyword_from_span(src, start, end);
  return (skim_syntax_token_t){
    .kind = keyword == SKIM_KEYWORD_NONE ? SKIM_TOKEN_IDENTIFIER : SKIM_TOKEN_KEYWORD,
    .keyword = keyword,
    .start = start,
    .end = end,
  };
}
