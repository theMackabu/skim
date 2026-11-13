#include "chars.h"
#include "print_internal.h"
#include "str.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

typedef enum : uint8_t {
  VALUE_UNKNOWN,
  VALUE_NUMBER,
  VALUE_STRING,
} value_kind_t;

typedef struct {
  value_kind_t kind;
  double number;
  uint32_t text;
  uint32_t len;
} value_t;

typedef struct skim_enum_member {
  uint32_t owner;
  uint32_t name;
  value_t value;
  bool inlined;
} member_t;

typedef struct {
  skim_printer_t *pr;
  uint32_t owner;
  uint32_t i;
  uint32_t end;
  bool failed;
} eval_t;

static const value_t unknown = {.kind = VALUE_UNKNOWN};

static value_t number_value(double number) {
  return (value_t){.kind = VALUE_NUMBER, .number = number};
}

static value_t string_value(skim_printer_t *pr, const char *text, size_t len) {
  value_t value = {.kind = VALUE_STRING, .text = (uint32_t)pr->enum_strings.len, .len = (uint32_t)len};
  skim_str_putn(&pr->enum_strings, text, len);
  return value;
}

static bool name_matches(const skim_printer_t *pr, uint32_t token, const char *text, uint32_t len) {
  uint32_t n = token_len(pr, token);
  const char *name = pr->t[token].kind == SKIM_T_STRING ? string_content(pr, token, &n) : token_text(pr, token);
  return n == len && memcmp(name, text, len) == 0;
}

static const member_t *find_member(const skim_printer_t *pr, uint32_t owner, const char *text, uint32_t len) {
  for (uint32_t i = pr->enum_member_count; i > 0; i--) {
    const member_t *member = &pr->enum_members[i - 1];
    if (token_equal(pr, member->owner, owner) && name_matches(pr, member->name, text, len)) return member;
  }
  return NULL;
}

static void remember_member(skim_printer_t *pr, uint32_t owner, uint32_t name, value_t value, bool inlined) {
  if (pr->enum_member_count == pr->enum_member_cap) {
    uint32_t cap = pr->enum_member_cap ? pr->enum_member_cap * 2 : 32;
    member_t *members = realloc(pr->enum_members, sizeof(*members) * cap);
    if (!members) skim_die("out of memory");
    pr->enum_members = members;
    pr->enum_member_cap = cap;
  }
  pr->enum_members[pr->enum_member_count++] =
    (member_t){.owner = owner, .name = name, .value = value, .inlined = inlined};
}

static void skip_stripped(eval_t *ev) {
  while (ev->i < ev->end && ev->pr->t[ev->i].flags & SKIM_TF_STRIP)
    ev->i++;
}

static uint8_t peek(eval_t *ev) {
  skip_stripped(ev);
  return ev->i < ev->end ? ev->pr->t[ev->i].kind : SKIM_T_EOF;
}

static int32_t to_int32(double number) {
  if (!isfinite(number)) return 0;
  double wrapped = fmod(trunc(number), 4294967296.0);
  if (wrapped < 0) wrapped += 4294967296.0;
  return (int32_t)(uint32_t)wrapped;
}

static void put_double(skim_str_t *out, double number) {
  char buf[64];
  if (isnan(number)) {
    skim_str_puts(out, "NaN");
    return;
  }
  if (isinf(number)) {
    skim_str_puts(out, number < 0 ? "-Infinity" : "Infinity");
    return;
  }
  if (floor(number) == number && fabs(number) < 1e21) {
    snprintf(buf, sizeof(buf), "%.0f", number);
  } else {
    for (int precision = 15; precision <= 17; precision++) {
      snprintf(buf, sizeof(buf), "%.*g", precision, number);
      if (strtod(buf, NULL) == number) break;
    }
  }
  const char *text = buf;
  if (*text == '-') skim_str_putc(out, *text++);
  if (text[0] == '0' && text[1] == '.') text++;
  skim_str_puts(out, text);
}

static value_t concat(skim_printer_t *pr, value_t left, value_t right) {
  skim_str_t joined = {0};
  for (int side = 0; side < 2; side++) {
    value_t value = side ? right : left;
    if (value.kind == VALUE_STRING) skim_str_putn(&joined, pr->enum_strings.data + value.text, value.len);
    else put_double(&joined, value.number);
  }
  value_t result = string_value(pr, joined.data ? joined.data : "", joined.len);
  free(joined.data);
  return result;
}

static value_t eval_binary(eval_t *ev, int min_precedence);

static value_t eval_number(eval_t *ev, uint32_t token) {
  const char *text = token_text(ev->pr, token);
  uint32_t len = token_len(ev->pr, token);
  char clean[64];
  uint32_t n = 0;
  if (text[len - 1] == 'n') return unknown;
  for (uint32_t i = 0; i < len && n + 1 < sizeof(clean); i++)
    if (text[i] != '_') clean[n++] = text[i];
  clean[n] = '\0';
  if (n > 1 && clean[0] == '0' && (clean[1] == 'b' || clean[1] == 'B' || clean[1] == 'o' || clean[1] == 'O')) {
    return number_value((double)strtoull(clean + 2, NULL, clean[1] == 'b' || clean[1] == 'B' ? 2 : 8));
  }
  if (n > 1 && clean[0] == '0' && (clean[1] == 'x' || clean[1] == 'X'))
    return number_value((double)strtoull(clean + 2, NULL, 16));
  if (n > 1 && clean[0] == '0' && skim_is_digit(clean[1]) && !strpbrk(clean, ".eE89"))
    return number_value((double)strtoull(clean, NULL, 8));
  return number_value(strtod(clean, NULL));
}

static value_t eval_string(eval_t *ev, const char *text, uint32_t len) {
  skim_str_t escaped = {0};
  for (uint32_t i = 0; i < len; i++) {
    char c = text[i];
    if (c == '\\' && i + 1 < len) {
      char next = text[++i];
      if (next != '\'' && next != '`' && next != '$') skim_str_putc(&escaped, '\\');
      skim_str_putc(&escaped, next);
    } else if (c == '"') {
      skim_str_puts(&escaped, "\\\"");
    } else if (c == '\n') {
      skim_str_puts(&escaped, "\\n");
    } else if (c != '\r') {
      skim_str_putc(&escaped, c);
    }
  }
  value_t value = string_value(ev->pr, escaped.data ? escaped.data : "", escaped.len);
  free(escaped.data);
  return value;
}

static value_t eval_template(eval_t *ev) {
  skim_printer_t *pr = ev->pr;
  uint32_t piece = ev->i;
  value_t result = string_value(pr, "", 0);
  for (;;) {
    const skim_token_t *token = &pr->t[piece];
    bool tail = token->flags & SKIM_TF_TAIL;
    uint32_t trim = tail ? 1 : 2;
    value_t chunk = eval_string(ev, pr->src + token->start + 1, token->end - token->start - 1 - trim);
    if (chunk.kind == VALUE_UNKNOWN) return unknown;
    result = concat(pr, result, chunk);
    if (tail) break;
    eval_t inner = {.pr = pr, .owner = ev->owner, .i = piece + 1, .end = token->pair};
    value_t value = eval_binary(&inner, 0);
    if (value.kind == VALUE_UNKNOWN || inner.failed || peek(&inner) != SKIM_T_EOF) return unknown;
    result = concat(pr, result, value);
    piece = token->pair;
  }
  ev->i = piece + 1;
  return result;
}

static value_t eval_reference(eval_t *ev) {
  skim_printer_t *pr = ev->pr;
  uint32_t token = ev->i++;
  const char *text = token_text(pr, token);
  uint32_t len = token_len(pr, token);
  uint8_t next = peek(ev);
  if (next == '.' && ev->i + 1 < ev->end && pr->t[ev->i + 1].kind == SKIM_T_WORD) {
    const member_t *member = find_member(pr, token, token_text(pr, ev->i + 1), token_len(pr, ev->i + 1));
    ev->i += 2;
    return member ? member->value : unknown;
  }
  if (next == '[' && ev->i + 2 < ev->end && pr->t[ev->i + 1].kind == SKIM_T_STRING && pr->t[ev->i + 2].kind == ']') {
    uint32_t key_len = 0;
    const char *key = string_content(pr, ev->i + 1, &key_len);
    const member_t *member = find_member(pr, token, key, key_len);
    ev->i += 3;
    return member ? member->value : unknown;
  }
  if (len == 8 && memcmp(text, "Infinity", 8) == 0) return number_value(INFINITY);
  if (len == 3 && memcmp(text, "NaN", 3) == 0) return number_value(NAN);
  const member_t *member = find_member(pr, ev->owner, text, len);
  return member ? member->value : unknown;
}

static value_t eval_unary(eval_t *ev) {
  skim_printer_t *pr = ev->pr;
  uint8_t kind = peek(ev);
  uint32_t token = ev->i;
  switch (kind) {
  case '+':
  case '-':
  case '~': {
    ev->i++;
    value_t value = eval_unary(ev);
    if (value.kind == VALUE_UNKNOWN) return unknown;
    if (value.kind == VALUE_STRING) return kind == '+' ? value : number_value(kind == '-' ? NAN : -1);
    if (kind == '+') return value;
    return number_value(kind == '-' ? -value.number : (double)~to_int32(value.number));
  }
  case '(': {
    uint32_t close = token_close(pr, token);
    eval_t inner = {.pr = pr, .owner = ev->owner, .i = token + 1, .end = close};
    value_t value = eval_binary(&inner, 0);
    if (inner.failed || peek(&inner) != SKIM_T_EOF) value = unknown;
    ev->i = close + 1;
    return value;
  }
  case SKIM_T_NUMBER:
    ev->i++;
    return eval_number(ev, token);
  case SKIM_T_STRING: {
    uint32_t len = 0;
    const char *text = string_content(pr, token, &len);
    ev->i++;
    return eval_string(ev, text, len);
  }
  case SKIM_T_TEMPLATE:
    return eval_template(ev);
  case SKIM_T_WORD:
    return eval_reference(ev);
  default:
    ev->failed = true;
    return unknown;
  }
}

static int binary_operator(eval_t *ev, uint32_t *width, char *op) {
  const skim_printer_t *pr = ev->pr;
  uint8_t kind = peek(ev);
  const skim_token_t *token = &pr->t[ev->i];
  *width = 1;
  *op = (char)kind;
  switch (kind) {
  case '|':
    return 1;
  case '&':
    return 3;
  case '+':
  case '-':
    return 5;
  case '*':
  case '/':
    return 6;
  case '<':
  case '>':
    while (ev->i + *width < ev->end && pr->t[ev->i + *width].kind == kind &&
           pr->t[ev->i + *width].start == pr->t[ev->i + *width - 1].end)
      (*width)++;
    if (*width == 1 || (kind == '<' && *width != 2)) return -1;
    *op = kind == '<' ? 'l' : *width == 2 ? 'r' : 'u';
    return 4;
  case SKIM_T_OP:
    *op = pr->src[token->start];
    if (token->end - token->start == 1 && *op == '^') return 2;
    if (token->end - token->start == 1 && *op == '%') return 6;
    if (token->end - token->start == 2 && *op == '*' && pr->src[token->start + 1] == '*') {
      *op = 'p';
      return 7;
    }
    return -1;
  default:
    return -1;
  }
}

static value_t apply_binary(skim_printer_t *pr, char op, value_t left, value_t right) {
  if (left.kind == VALUE_UNKNOWN || right.kind == VALUE_UNKNOWN) return unknown;
  if (op == '+' && (left.kind == VALUE_STRING || right.kind == VALUE_STRING)) return concat(pr, left, right);
  if (left.kind != VALUE_NUMBER || right.kind != VALUE_NUMBER) return unknown;
  double a = left.number, b = right.number;
  int32_t ia = to_int32(a), ib = to_int32(b);
  switch (op) {
  case '+':
    return number_value(a + b);
  case '-':
    return number_value(a - b);
  case '*':
    return number_value(a * b);
  case '/':
    return number_value(a / b);
  case '%':
    return number_value(fmod(a, b));
  case 'p':
    return number_value(pow(a, b));
  case '|':
    return number_value((double)(ia | ib));
  case '&':
    return number_value((double)(ia & ib));
  case '^':
    return number_value((double)(ia ^ ib));
  case 'l':
    return number_value((double)(int32_t)((uint32_t)ia << ((uint32_t)ib & 31u)));
  case 'r':
    return number_value((double)(ia >> ((uint32_t)ib & 31u)));
  case 'u':
    return number_value((double)((uint32_t)ia >> ((uint32_t)ib & 31u)));
  default:
    return unknown;
  }
}

static value_t eval_binary(eval_t *ev, int min_precedence) {
  value_t left = eval_unary(ev);
  for (;;) {
    uint32_t width = 1;
    char op = 0;
    int precedence = binary_operator(ev, &width, &op);
    if (precedence < 0 || precedence < min_precedence) return left;
    ev->i += width;
    value_t right = eval_binary(ev, op == 'p' ? precedence : precedence + 1);
    left = apply_binary(ev->pr, op, left, right);
  }
}

static value_t evaluate(skim_printer_t *pr, uint32_t owner, uint32_t from, uint32_t to) {
  eval_t ev = {.pr = pr, .owner = owner, .i = from, .end = to};
  value_t value = eval_binary(&ev, 0);
  return ev.failed || peek(&ev) != SKIM_T_EOF ? unknown : value;
}

static void put_value(skim_printer_t *pr, value_t value) {
  if (value.kind == VALUE_NUMBER) {
    put_double(pr->out, value.number);
    return;
  }
  skim_str_putc(pr->out, '"');
  skim_str_putn(pr->out, pr->enum_strings.data + value.text, value.len);
  skim_str_putc(pr->out, '"');
}

static void put_member_name(skim_printer_t *pr, uint32_t name) {
  uint32_t len = token_len(pr, name);
  const char *text = pr->t[name].kind == SKIM_T_STRING ? string_content(pr, name, &len) : token_text(pr, name);
  skim_str_putn(pr->out, text, len);
}

static void put_initializer(skim_printer_t *pr, uint32_t owner, uint32_t from, uint32_t to) {
  pr->copied = pr->kept = pr->t[from].start;
  uint32_t segment = from;
  for (uint32_t i = from; i < to; i++) {
    const skim_token_t *token = &pr->t[i];
    const skim_node_t *nested = token->flags & SKIM_TF_NODE ? skim_print_node(pr, i) : NULL;
    if (nested && nested->kind == SKIM_N_ENUM && nested->end > i) {
      i = nested->end - 1;
      continue;
    }
    if (token->kind != SKIM_T_WORD || !(token->flags & SKIM_TF_REF) || token->flags & SKIM_TF_STRIP) continue;
    if (!find_member(pr, owner, token_text(pr, i), token_len(pr, i))) continue;
    skim_print_range(pr, segment, i);
    skim_print_begin(pr, i);
    put_token(pr, owner);
    put(pr, ".");
    segment = i;
  }
  skim_print_range(pr, segment, to);
  skim_print_flush(pr);
}

static uint32_t member_end(const skim_printer_t *pr, uint32_t from, uint32_t close) {
  while (from < close && pr->t[from].kind != ',') {
    uint8_t kind = pr->t[from].kind;
    bool open = kind == '(' || kind == '[' || kind == '{' ||
                (kind == SKIM_T_TEMPLATE && !(pr->t[from].flags & SKIM_TF_TAIL) && pr->t[from].pair > from);
    from = open ? token_close(pr, from) + (kind != SKIM_T_TEMPLATE) : from + 1;
  }
  return from < close ? from : close;
}

static bool drop_empty_enum(skim_printer_t *pr, const skim_node_t *node) {
  for (uint32_t i = node->end; i < pr->count; i++)
    if (pr->t[i].kind == SKIM_T_WORD && token_equal(pr, i, node->decl.name)) return false;
  uint32_t before = node->first ? node->first - 1 : 0;
  skim_str_t *out = pr->out;
  if (token_word(pr, before) == SKIM_KW_ELSE) {
    while (out->len > 0 && skim_is_space(out->data[out->len - 1]))
      out->len--;
    if (out->len >= 4 && memcmp(out->data + out->len - 4, "else", 4) == 0) out->len -= 4;
  } else if (pr->t[before].kind == ')') {
    put(pr, "{}");
  }
  skim_print_remove(pr, node->first, node->end);
  return true;
}

static bool mark_references(skim_printer_t *pr, uint32_t name, uint32_t from) {
  bool removable = true;
  for (uint32_t i = from; i < pr->count; i++) {
    skim_token_t *token = &pr->t[i];
    if (token->kind != SKIM_T_WORD || (token->flags & (SKIM_TF_REF | SKIM_TF_STRIP)) != SKIM_TF_REF) continue;
    if (!token_equal(pr, i, name)) continue;
    uint8_t next = pr->t[i + 1].kind;
    token->flags |= SKIM_TF_REWRITE;
    removable = removable && (next == '.' || next == SKIM_T_CHAIN || next == '[');
  }
  return removable;
}

uint32_t skim_print_enum(skim_printer_t *pr, const skim_node_t *node) {
  bool exported = pr->exported;
  uint32_t name = node->decl.name;
  uint32_t open = node->decl.body;
  uint32_t close = token_close(pr, open);
  bool merge = skim_print_declared(pr, name, name, false);
  bool scoped = exported || skim_print_scope_of(pr, name) != 0;
  bool optimized = node->flags & SKIM_NF_CONST && pr->options->optimize_const_enums;
  bool inlined = optimized || pr->options->optimize_enums;
  bool known = open + 1 < close;
  pr->exported = false;

  skim_print_begin(pr, node->first);
  size_t mark = pr->out->len;
  if (node->flags & SKIM_NF_CONST && open + 1 == close && drop_empty_enum(pr, node)) return node->first;
  if (!merge) {
    if (exported && !pr->param) put(pr, "export ");
    put(pr, scoped ? "let " : "var ");
  }
  put_token(pr, name);
  put(pr, " = function (");
  put_token(pr, name);
  put(pr, ") {\n");

  value_t previous = number_value(-1);
  uint32_t previous_name = 0;
  for (uint32_t member = open + 1; member < close;) {
    if (pr->t[member].kind == ',') {
      member++;
      continue;
    }
    uint32_t end = member_end(pr, member, close);
    bool initialized = member + 1 < end && pr->t[member + 1].kind == '=';
    value_t value = unknown;
    if (initialized && !token_equal(pr, member, name)) value = evaluate(pr, name, member + 2, end);
    else if (!initialized && previous.kind == VALUE_NUMBER) value = number_value(previous.number + 1);

    put(pr, "  ");
    put_token(pr, name);
    if (value.kind != VALUE_STRING) {
      put(pr, "[");
      put_token(pr, name);
    }
    put(pr, "[\"");
    put_member_name(pr, member);
    put(pr, "\"] = ");
    if (value.kind != VALUE_UNKNOWN) {
      put_value(pr, value);
    } else if (initialized) {
      put_initializer(pr, name, member + 2, end);
    } else {
      put(pr, "1 + ");
      put_token(pr, name);
      put(pr, "[\"");
      put_member_name(pr, previous_name);
      put(pr, "\"]");
    }
    if (value.kind != VALUE_STRING) {
      put(pr, "] = \"");
      put_member_name(pr, member);
      put(pr, "\"");
    }
    put(pr, ";\n");
    remember_member(pr, name, member, value, inlined);
    known = known && value.kind != VALUE_UNKNOWN;
    previous = value;
    previous_name = member;
    member = end;
  }

  put(pr, "  return ");
  put_token(pr, name);
  put(pr, ";\n}(");
  if (merge || !scoped) {
    put_token(pr, name);
    put(pr, " || {}");
  } else {
    put(pr, "{}");
  }
  put(pr, ");");
  if (exported && pr->param) {
    skim_print_line(pr, node->first);
    skim_print_alias(pr, name);
    put(pr, ";");
  }
  uint32_t end = node->end;
  if (end < pr->count && pr->t[end].kind == ';') end++;
  bool unused = inlined && mark_references(pr, name, end);
  if (optimized || (inlined && unused && known && !exported)) {
    pr->out->len = mark;
    skim_print_remove(pr, node->first, end);
    return node->first;
  }
  skim_print_resume(pr, end);
  return end;
}

uint32_t skim_print_enum_reference(skim_printer_t *pr, uint32_t token) {
  uint32_t member = token + 1;
  uint32_t end = 0;
  const char *text = NULL;
  uint32_t len = 0;
  if (pr->t[member].kind == SKIM_T_CHAIN || pr->t[member].kind == '.') member++;
  else if (pr->t[member].kind != '[') return token;
  if (pr->t[member].kind == SKIM_T_WORD && pr->t[member - 1].kind != '[') {
    text = token_text(pr, member);
    len = token_len(pr, member);
    end = member + 1;
  } else {
    if (pr->t[member].kind == '[') member++;
    if (pr->t[member - 1].kind != '[' || pr->t[member].kind != SKIM_T_STRING || pr->t[member + 1].kind != ']')
      return token;
    text = string_content(pr, member, &len);
    end = member + 2;
  }
  const member_t *found = find_member(pr, token, text, len);
  if (!found || !found->inlined || found->value.kind == VALUE_UNKNOWN) return token;
  bool wrap = found->value.kind == VALUE_NUMBER && pr->t[end].kind == '.';
  skim_print_begin(pr, token);
  if (wrap) put(pr, "(");
  put_value(pr, found->value);
  if (wrap) put(pr, ")");
  skim_print_resume(pr, end);
  return end;
}
