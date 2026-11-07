#include "chars.h"

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
