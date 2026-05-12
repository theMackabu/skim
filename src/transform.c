#include "ast.h"

#include <stdlib.h>

skim_options_t skim_options;
static size_t transform_depth;

char *skim_transform_typescript_len(const char *src, size_t len, size_t *out_len) {
  bool root_transform = transform_depth++ == 0;
  if (root_transform) {
    skim_decl_reset();
    skim_ts_enum_reset();
    skim_ts_namespace_reset();
  }
  skim_str_t out = {0};
  skim_str_reserve(&out, len + 128);
  skim_ast_program_t program = {0};
  skim_parse_program(&program, src, len);
  skim_ast_print_program(&program, &out);
  skim_ast_free_program(&program);
  if (
    !skim_has_module_syntax(out.data ? out.data : "", out.len) &&
    (skim_has_erasable_import_syntax(src, len) || skim_has_export_declare(src, len) ||
     skim_has_type_only_module_syntax(src, len))
  ) {
    if (skim_has_effective_code(out.data ? out.data : "", out.len)) skim_str_putc(&out, '\n');
    else {
      free(out.data);
      out.data = NULL;
      out.len = 0;
      out.cap = 0;
    }
    skim_str_puts(&out, "export {};\n");
  }
  transform_depth--;
  if (root_transform) {
    skim_ts_enum_reset();
    skim_ts_namespace_reset();
    skim_decl_reset();
  }
  if (out_len) *out_len = out.len;
  return out.data ? out.data : skim_slice_dup("", 0, 0);
}

char *skim_transform_typescript(const char *src, size_t len) {
  return skim_transform_typescript_len(src, len, NULL);
}
