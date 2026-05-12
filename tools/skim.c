#include "skim.h"

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef SKIM_VERSION
#  define SKIM_VERSION "unknown"
#endif

typedef enum {
  CLI_OPTION_BOOL,
  CLI_OPTION_HELP,
  CLI_OPTION_VERSION,
} cli_option_kind_t;

typedef struct {
  const char *name;
  cli_option_kind_t kind;
  size_t option_offset;
  const char *help;
} cli_option_t;

typedef struct {
  skim_options_t options;
  const char *path;
  bool done;
  int status;
} cli_args_t;

static const cli_option_t cli_options[] = {
  {"--only-remove-type-imports",
   CLI_OPTION_BOOL,
   offsetof(skim_options_t, only_remove_type_imports),
   "remove type-only imports and keep value imports"},
  {"--optimize-enums", CLI_OPTION_BOOL, offsetof(skim_options_t, optimize_enums), "inline and remove safe enum values"},
  {"--optimize-const-enums",
   CLI_OPTION_BOOL,
   offsetof(skim_options_t, optimize_const_enums),
   "inline and remove safe const enum values"},
  {"--remove-class-fields-without-initializer",
   CLI_OPTION_BOOL,
   offsetof(skim_options_t, remove_class_fields_without_initializer),
   "drop class fields without initializers"},
  {"--allow-declare-fields-false",
   CLI_OPTION_BOOL,
   offsetof(skim_options_t, allow_declare_fields_false),
   "match allowDeclareFields:false behavior"},
  {"--transform-class-properties",
   CLI_OPTION_BOOL,
   offsetof(skim_options_t, transform_class_properties),
   "lower class properties when supported"},
  {"--set-public-class-fields",
   CLI_OPTION_BOOL,
   offsetof(skim_options_t, set_public_class_fields),
   "use setPublicClassFields behavior"},
  {"--help", CLI_OPTION_HELP, 0, "show this help"},
  {"-h", CLI_OPTION_HELP, 0, "show this help"},
  {"--version", CLI_OPTION_VERSION, 0, "print version"},
};

static void print_usage(FILE *f, const char *argv0) {
  fprintf(f, "usage: %s [strip-options] <file.ts>\n\n", argv0);
  fprintf(f, "options:\n");
  for (size_t i = 0; i < sizeof(cli_options) / sizeof(cli_options[0]); i++)
    fprintf(f, "  %-43s %s\n", cli_options[i].name, cli_options[i].help);
}

static const cli_option_t *find_option(const char *arg) {
  for (size_t i = 0; i < sizeof(cli_options) / sizeof(cli_options[0]); i++) 
    if (strcmp(arg, cli_options[i].name) == 0) return &cli_options[i];
  return NULL;
}

static int apply_option(cli_args_t *args, const cli_option_t *option, const char *argv0) {
  switch (option->kind) {
  case CLI_OPTION_BOOL: {
    bool *field = (bool *)((char *)&args->options + option->option_offset);
    *field = true;
    return 0;
  }
  case CLI_OPTION_HELP:
    print_usage(stdout, argv0);
    args->done = true;
    args->status = 0;
    return 0;
  case CLI_OPTION_VERSION:
    printf("skim %s\n", SKIM_VERSION);
    args->done = true;
    args->status = 0;
    return 0;
  }
  return 2;
}

static int parse_args(cli_args_t *args, int argc, char **argv) {
  for (int i = 1; i < argc; i++) {
    const char *arg = argv[i];
    const cli_option_t *option = find_option(arg);
    if (option) {
      int status = apply_option(args, option, argv[0]);
      if (status != 0 || args->done) return status;
      continue;
    }
    if (arg[0] == '-') {
      fprintf(stderr, "unknown option: %s\n", arg);
      return 2;
    }
    if (args->path) {
      fprintf(stderr, "unexpected extra input: %s\n", arg);
      return 2;
    }
    args->path = arg;
  }
  if (!args->path) {
    print_usage(stderr, argv[0]);
    return 2;
  }
  return 0;
}

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
  fclose(f); data[n] = '\0';
  *out_len = n;
  
  return data;
}

int main(int argc, char **argv) {
  cli_args_t args = {0};
  int arg_status = parse_args(&args, argc, argv);
  if (args.done) return args.status;
  if (arg_status != 0) return arg_status;

  size_t len = 0;
  char *src = read_file(args.path, &len);
  
  size_t out_len = 0;
  skim_error_t error = SKIM_OK;
  char error_buf[256] = {0};
  
  char *out = skim_strip_typescript_owned(
    src, len, args.path, true, &args.options, 
    &out_len, &error, error_buf, sizeof(error_buf)
  );
  
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
