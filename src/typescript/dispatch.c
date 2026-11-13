#include "chars.h"
#include "run.h"
#include "skip.h"
#include "str.h"
#include "transform.h"
#include "typescript.h"

static bool next_non_ws_is_member_access(const char *src, size_t len, size_t i) {
  if (i >= len || !skim_is_id_start(src[i])) return false;
  i++;
  while (i < len && skim_is_id_part(src[i]))
    i++;
  i = skim_skip_ws(src, len, i);
  return i < len && (src[i] == '.' || src[i] == '[' || (i + 1 < len && src[i] == '?' && src[i + 1] == '.'));
}

bool skim_transform_try_at(skim_str_t *out, const char *src, size_t len, size_t *io) {
  size_t i = *io;
  if (i > 0 && src[i - 1] == '.' && src[i] != '<') return false;
  switch (src[i]) {
  case '@':
    if (skim_ts_class_try(out, src, len, io)) return true;
    return skim_ts_annotations_try(out, src, len, io);
  case ':':
  case '?':
  case '!':
  case '<':
    return skim_ts_annotations_try(out, src, len, io);
  case 'a':
    if (skim_word_any(src, len, i, "as\0abstract\0accessor\0")) { return skim_ts_annotations_try(out, src, len, io); }
    break;
  case 'c':
    if (skim_word_at(src, len, i, "class")) return skim_ts_class_try(out, src, len, io);
    if (skim_word_at(src, len, i, "const")) return skim_ts_enum_try(out, src, len, io);
    break;
  case 'd':
    if (skim_word_at(src, len, i, "declare")) return skim_ts_annotations_try(out, src, len, io);
    break;
  case 'e':
    if (skim_word_at(src, len, i, "enum")) return skim_ts_enum_try(out, src, len, io);
    if (skim_word_at(src, len, i, "export")) {
      size_t j = skim_skip_ws_comments(src, len, i + 6);
      if (skim_word_at(src, len, j, "declare")) return skim_ts_annotations_try(out, src, len, io);
      if (skim_ts_class_try(out, src, len, io)) return true;
      if (skim_ts_namespace_try(out, src, len, io)) return true;
      if (skim_ts_enum_try(out, src, len, io)) return true;
      return skim_ts_module_try(out, src, len, io);
    }
    break;
  case 'f':
    if (skim_word_at(src, len, i, "function")) return skim_ts_annotations_try(out, src, len, io);
    break;
  case 'i':
    if (skim_word_at(src, len, i, "import")) return skim_ts_module_try(out, src, len, io);
    if (skim_word_any(src, len, i, "interface\0implements\0")) { return skim_ts_annotations_try(out, src, len, io); }
    break;
  case 'm':
    if (skim_word_at(src, len, i, "module")) return skim_ts_namespace_try(out, src, len, io);
    break;
  case 'n':
    if (skim_word_at(src, len, i, "namespace")) return skim_ts_namespace_try(out, src, len, io);
    break;
  case 'p':
    if (skim_word_any(src, len, i, "public\0private\0protected\0")) {
      return skim_ts_annotations_try(out, src, len, io);
    }
    break;
  case 'r':
    if (skim_word_at(src, len, i, "readonly")) return skim_ts_annotations_try(out, src, len, io);
    break;
  case 's':
    if (skim_word_at(src, len, i, "satisfies")) return skim_ts_annotations_try(out, src, len, io);
    break;
  case 't':
    if (skim_word_any(src, len, i, "type\0this\0")) return skim_ts_annotations_try(out, src, len, io);
    break;
  }

  if (
    (skim_run->options.remove_class_fields_without_initializer || skim_run->options.allow_declare_fields_false) &&
    skim_is_id_start(src[i]) && skim_ts_annotations_try(out, src, len, io)
  ) {
    return true;
  }
  if (
    skim_run->ts.enums.inline_member_count && next_non_ws_is_member_access(src, len, i) &&
    skim_ts_enum_ref_try(out, src, len, io)
  )
    return true;
  return false;
}
