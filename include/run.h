#ifndef SKIM_RUN_H
#define SKIM_RUN_H

#include "decl.h"
#include "scan_cache.h"
#include "skim.h"
#include "typescript_state.h"

typedef struct {
  skim_options_t options;
  skim_source_mode_t source_mode;
  size_t transform_depth;
  skim_decl_table_t decls;
  skim_scan_cache_t scan_cache;
  skim_ts_state_t ts;
} skim_run_t;

extern thread_local skim_run_t *skim_run;

void skim_run_free(skim_run_t *run);

#endif
