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
    fprintf(stderr, "fseek failed\n");
    exit(1);
  }
  long size = ftell(f);
  if (size < 0) {
    fprintf(stderr, "ftell failed\n");
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

int main(int argc, char **argv) {
  const char *path = NULL;
  skim_options_t options = {0};
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--only-remove-type-imports") == 0) {
      options.only_remove_type_imports = true;
    } else if (strcmp(argv[i], "--optimize-enums") == 0) {
      options.optimize_enums = true;
    } else if (strcmp(argv[i], "--optimize-const-enums") == 0) {
      options.optimize_const_enums = true;
    } else if (strcmp(argv[i], "--remove-class-fields-without-initializer") == 0) {
      options.remove_class_fields_without_initializer = true;
    } else if (strcmp(argv[i], "--allow-declare-fields-false") == 0) {
      options.allow_declare_fields_false = true;
    } else if (strcmp(argv[i], "--transform-class-properties") == 0) {
      options.transform_class_properties = true;
    } else if (strcmp(argv[i], "--set-public-class-fields") == 0) {
      options.set_public_class_fields = true;
    } else if (argv[i][0] == '-') {
      fprintf(stderr, "unknown option: %s\n", argv[i]);
      return 2;
    } else {
      path = argv[i];
    }
  }
  if (!path) {
    fprintf(stderr, "usage: %s [strip-options] <file.ts>\n", argv[0]);
    return 2;
  }
  size_t len = 0;
  char *src = read_file(path, &len);
  size_t out_len = 0;
  skim_error_t error = SKIM_OK;
  char error_buf[256] = {0};
  char *out = skim_strip_typescript_owned(src, len, path, true, &options, &out_len, &error, error_buf, sizeof(error_buf));
  if (!out) {
    fprintf(stderr, "%s\n", error_buf[0] ? error_buf : "failed to strip TypeScript");
    free(src);
    return error == SKIM_OK ? 1 : -(int)error;
  }
  fwrite(out, 1, out_len, stdout);
  skim_free(out);
  free(src);
  return 0;
}
