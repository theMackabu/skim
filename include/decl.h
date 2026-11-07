#ifndef SKIM_DECL_H
#define SKIM_DECL_H

#include <stdbool.h>
#include <stddef.h>

#define SKIM_BUCKET_COUNT 4096u
#define SKIM_DECL_VALUE 1u
#define SKIM_DECL_ENUM 2u
#define SKIM_DECL_NAMESPACE 4u

void skim_decl_reset(void);
void skim_decl_remember(const char *src, const char *name_start, const char *name_end, size_t pos, unsigned flags);
bool skim_decl_seen_before(const char *src, const char *name, size_t pos, unsigned flags);

#endif
