#include "syntax.h"

#include <ctype.h>
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
  KW("var", SKIM_KEYWORD_VAR);
  KW("let", SKIM_KEYWORD_LET);
  KW("function", SKIM_KEYWORD_FUNCTION);
  KW("async", SKIM_KEYWORD_ASYNC);
  KW("new", SKIM_KEYWORD_NEW);
  KW("this", SKIM_KEYWORD_THIS);
  KW("super", SKIM_KEYWORD_SUPER);
  KW("await", SKIM_KEYWORD_AWAIT);
  KW("yield", SKIM_KEYWORD_YIELD);
#undef KW
  return SKIM_KEYWORD_NONE;
}

void skim_lexer_init(skim_lexer_t *lexer, const char *src, size_t len) {
  *lexer = (skim_lexer_t){.src = src, .len = len};
}

static size_t skip_trivia(const char *src, size_t len, size_t i, bool *had_newline) {
  while (i < len) {
    unsigned char c = (unsigned char)src[i];
    if (isspace(c)) {
      if (src[i] == '\n' || src[i] == '\r') *had_newline = true;
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
      i += 2;
      while (i + 1 < len && !(src[i] == '*' && src[i + 1] == '/')) {
        if (src[i] == '\n' || src[i] == '\r') *had_newline = true;
        i++;
      }
      if (i + 1 < len) i += 2;
      continue;
    }
    break;
  }
  return i;
}

static size_t scan_number(const char *src, size_t len, size_t i) {
  if (
    i + 1 < len && src[i] == '0' &&
    (src[i + 1] == 'x' || src[i + 1] == 'X' || src[i + 1] == 'b' || src[i + 1] == 'B' || src[i + 1] == 'o' ||
     src[i + 1] == 'O')
  ) {
    i += 2;
    while (i < len && (isalnum((unsigned char)src[i]) || src[i] == '_'))
      i++;
    return i;
  }
  while (i < len && (isdigit((unsigned char)src[i]) || src[i] == '_'))
    i++;
  if (i < len && src[i] == '.') {
    i++;
    while (i < len && (isdigit((unsigned char)src[i]) || src[i] == '_'))
      i++;
  }
  if (i < len && (src[i] == 'e' || src[i] == 'E')) {
    size_t exp = i + 1;
    if (exp < len && (src[exp] == '+' || src[exp] == '-')) exp++;
    if (exp < len && isdigit((unsigned char)src[exp])) {
      i = exp + 1;
      while (i < len && (isdigit((unsigned char)src[i]) || src[i] == '_'))
        i++;
    }
  }
  if (i < len && src[i] == 'n') i++;
  return i;
}

static size_t scan_regex(const char *src, size_t len, size_t i) {
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

static size_t scan_punct(const char *src, size_t len, size_t i) {
  if (i + 3 < len && src[i] == '>' && src[i + 1] == '>' && src[i + 2] == '>' && src[i + 3] == '=') return i + 4;
  if (i + 2 < len) {
    char a = src[i], b = src[i + 1], c = src[i + 2];
    if (
      (a == '=' && b == '=' && c == '=') || (a == '!' && b == '=' && c == '=') || (a == '>' && b == '>' && c == '>') ||
      (a == '<' && b == '<' && c == '=') || (a == '>' && b == '>' && c == '=') || (a == '*' && b == '*' && c == '=') ||
      (a == '?' && b == '?' && c == '=') || (a == '&' && b == '&' && c == '=') || (a == '|' && b == '|' && c == '=') ||
      (a == '.' && b == '.' && c == '.')
    )
      return i + 3;
  }
  if (i + 1 < len) {
    char a = src[i], b = src[i + 1];
    if (
      (a == '=' && b == '=') || (a == '!' && b == '=') || (a == '<' && b == '=') || (a == '>' && b == '=') ||
      (a == '+' && b == '+') || (a == '-' && b == '-') || (a == '+' && b == '=') || (a == '-' && b == '=') ||
      (a == '*' && b == '=') || (a == '/' && b == '=') || (a == '%' && b == '=') || (a == '&' && b == '&') ||
      (a == '|' && b == '|') || (a == '&' && b == '=') || (a == '|' && b == '=') || (a == '^' && b == '=') ||
      (a == '=' && b == '>') || (a == '?' && b == '?') || (a == '?' && b == '.') || (a == '*' && b == '*') ||
      (a == '<' && b == '<') || (a == '>' && b == '>')
    )
      return i + 2;
  }
  return i + 1;
}

skim_syntax_token_t skim_lexer_next(skim_lexer_t *lexer, bool regex_allowed) {
  const char *src = lexer->src;
  size_t len = lexer->len;
  bool had_newline = false;
  size_t leading_start = lexer->pos;
  size_t start = skip_trivia(src, len, lexer->pos, &had_newline);
  if (start >= len) {
    lexer->pos = len;
    return (skim_syntax_token_t){
      .kind = SKIM_TOKEN_EOF,
      .leading_start = leading_start,
      .start = len,
      .end = len,
      .leading_had_newline = had_newline
    };
  }
  size_t end = start + 1;
  skim_syntax_token_kind_t kind = SKIM_TOKEN_PUNCT;
  skim_syntax_keyword_t keyword = SKIM_KEYWORD_NONE;
  if (skim_is_id_start(src[start])) {
    end = start + 1;
    while (end < len && skim_is_id_part(src[end]))
      end++;
    keyword = keyword_from_span(src, start, end);
    kind = keyword == SKIM_KEYWORD_NONE ? SKIM_TOKEN_IDENTIFIER : SKIM_TOKEN_KEYWORD;
  } else if (
    isdigit((unsigned char)src[start]) ||
    (src[start] == '.' && start + 1 < len && isdigit((unsigned char)src[start + 1]))
  ) {
    end = scan_number(src, len, start);
    kind = SKIM_TOKEN_NUMBER;
  } else if (src[start] == '\'' || src[start] == '"') {
    end = skim_skip_string_raw(src, len, start);
    kind = SKIM_TOKEN_STRING;
  } else if (src[start] == '`') {
    end = skim_skip_string_raw(src, len, start);
    kind = SKIM_TOKEN_TEMPLATE;
  } else if (src[start] == '/' && regex_allowed) {
    end = scan_regex(src, len, start);
    kind = SKIM_TOKEN_REGEX;
  } else {
    end = scan_punct(src, len, start);
  }
  lexer->pos = end;
  return (skim_syntax_token_t){
    .kind = kind,
    .keyword = keyword,
    .leading_start = leading_start,
    .start = start,
    .end = end,
    .leading_had_newline = had_newline,
  };
}

skim_syntax_token_t skim_syntax_token_at(const char *src, size_t len, size_t pos, bool regex_allowed) {
  skim_lexer_t lexer;
  skim_lexer_init(&lexer, src, len);
  lexer.pos = pos;
  return skim_lexer_next(&lexer, regex_allowed);
}

bool skim_syntax_token_is_punct(const skim_syntax_token_t *token, const char *punct, const char *src) {
  size_t n = strlen(punct);
  return token->kind == SKIM_TOKEN_PUNCT && token->end - token->start == n && memcmp(src + token->start, punct, n) == 0;
}
