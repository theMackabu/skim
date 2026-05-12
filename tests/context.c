#include "skim.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_file(const char *path, size_t *out_len) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    fprintf(stderr, "open %s: %s\n", path, strerror(errno));
    exit(1);
  }
  if (fseek(f, 0, SEEK_END) != 0) {
    fprintf(stderr, "fseek %s failed\n", path);
    exit(1);
  }
  long size = ftell(f);
  if (size < 0) {
    fprintf(stderr, "ftell %s failed\n", path);
    exit(1);
  }
  rewind(f);
  char *data = malloc((size_t)size + 1);
  if (!data) {
    fprintf(stderr, "out of memory\n");
    exit(1);
  }
  size_t n = fread(data, 1, (size_t)size, f);
  fclose(f);
  data[n] = '\0';
  *out_len = n;
  return data;
}

static void
assert_strip_ok(const char *path, const char *out, size_t out_len, skim_error_t error, const char *error_buf) {
  if (!out || error != SKIM_OK) {
    fprintf(stderr, "%s: %s\n", path, error_buf[0] ? error_buf : "strip failed");
    exit(1);
  }
  if (out_len == 0) {
    fprintf(stderr, "%s: empty transform output\n", path);
    exit(1);
  }
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s <fixture.ts>...\n", argv[0]);
    return 2;
  }

  skim_context_t ctx = {0};
  if (skim_context_init(&ctx) != 0) {
    fprintf(stderr, "skim_context_init failed\n");
    return 1;
  }

  for (int i = 1; i < argc; i++) {
    size_t src_len = 0;
    char *src = read_file(argv[i], &src_len);
    size_t out_len = 0;
    skim_error_t error = SKIM_OK;
    char error_buf[256] = {0};

    skim_context_reset(&ctx);
    const char *out = skim_strip_typescript_borrowed(
      &ctx, src, src_len, argv[i], SKIM_SOURCE_AUTO, NULL, &out_len, &error, error_buf, sizeof(error_buf)
    );
    assert_strip_ok(argv[i], out, out_len, error, error_buf);

    free(src);
  }

  const char module_marker_source[] = "import type { Thing } from './thing';\nconst value: number = 1;\n";
  size_t out_len = 0;
  skim_error_t error = SKIM_OK;
  char error_buf[256] = {0};

  skim_context_reset(&ctx);
  const char *out = skim_strip_typescript_borrowed(
    &ctx,
    module_marker_source,
    strlen(module_marker_source),
    "sample.ts",
    SKIM_SOURCE_AUTO,
    NULL,
    &out_len,
    &error,
    error_buf,
    sizeof(error_buf)
  );
  assert_strip_ok("sample.ts", out, out_len, error, error_buf);
  if (!strstr(out, "export {};")) {
    fprintf(stderr, "sample.ts: expected AUTO to preserve module mode with export marker\n");
    skim_context_deinit(&ctx);
    return 1;
  }

  skim_context_reset(&ctx);
  out = skim_strip_typescript_borrowed(
    &ctx,
    module_marker_source,
    strlen(module_marker_source),
    "sample.cts",
    SKIM_SOURCE_AUTO,
    NULL,
    &out_len,
    &error,
    error_buf,
    sizeof(error_buf)
  );
  assert_strip_ok("sample.cts", out, out_len, error, error_buf);
  if (strstr(out, "export {};")) {
    fprintf(stderr, "sample.cts: expected AUTO CommonJS mode to omit export marker\n");
    skim_context_deinit(&ctx);
    return 1;
  }

  skim_context_deinit(&ctx);
  puts("skim context reuse passed");
  return 0;
}
