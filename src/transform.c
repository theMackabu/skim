#include "ast.h"
#include "decl.h"
#include "run.h"
#include "scan_cache.h"
#include "skim.h"
#include "skip.h"
#include "str.h"
#include "transform.h"
#include "typescript.h"

#include <stdlib.h>

thread_local skim_run_t *skim_run;

static bool source_mode_needs_module_marker(void) {
  return skim_run->source_mode == SKIM_SOURCE_MODULE;
}

void skim_run_free(skim_run_t *run) {
  if (!run) return;
  free(run->decls.decl_buckets);
  free(run->ts.enums.known_member_buckets);
  run->decls.decl_buckets = NULL;
  run->ts.enums.known_member_buckets = NULL;
}

static void invalidate_position_caches(void) {
  skim_ts_enum_scope_invalidate();
  skim_ts_annotations_memo_invalidate();
  skim_ts_namespace_decl_index_invalidate();
  skim_ts_module_index_invalidate();
}

void skim_transform_typescript_into(const char *src, size_t len, skim_str_t *out, skim_str_t *scratch) {
  bool root_transform = skim_run->transform_depth++ == 0;
  skim_scan_cache_t saved_cache;
  skim_scan_cache_enter(&saved_cache, src, len);
  invalidate_position_caches();
  if (root_transform) {
    skim_decl_reset();
    skim_ts_enum_reset();
    skim_ts_namespace_reset();
  }
  out->len = 0;
  if (out->data) out->data[0] = '\0';
  skim_str_reserve(out, len + 128);
  skim_ast_program_t program = {0};
  skim_parse_program(&program, src, len);
  skim_ast_print_program(&program, out);
  skim_ast_free_program(&program);
  if (
    source_mode_needs_module_marker() && skim_source_has_module_word(src, len) &&
    !skim_has_module_syntax(out->data ? out->data : "", out->len) && skim_source_needs_module_marker(src, len)
  ) {
    if (skim_has_effective_code(out->data ? out->data : "", out->len)) skim_str_putc(out, '\n');
    else {
      out->len = 0;
      if (out->data) out->data[0] = '\0';
    }
    skim_str_puts(out, "export {};\n");
  }
  if (root_transform) skim_cleanup_output_spacing(out, scratch);
  skim_scan_cache_leave(&saved_cache);
  invalidate_position_caches();
  skim_run->transform_depth--;
  if (root_transform) {
    skim_ts_enum_reset();
    skim_ts_namespace_reset();
    skim_decl_reset();
  }
  if (!out->data) skim_str_reserve(out, 0);
}

char *skim_transform_typescript(const char *src, size_t len) {
  skim_str_t out = {0};
  skim_transform_typescript_into(src, len, &out, NULL);
  return out.data ? out.data : skim_slice_dup("", 0, 0);
}
