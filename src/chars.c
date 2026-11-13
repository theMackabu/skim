#include "chars.h"

enum {
#define SKIM_CHAR_GROUP(name, flags) CC_##name = flags,
#include "char_group_list.h"
};

const uint8_t skim_char_class[256] = {
#define SKIM_CHAR_CLASS(lo, hi, cls) [lo... hi] = cls,
#include "char_class_list.h"
};
