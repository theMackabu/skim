#include "skim.h"
#include "skim_internal.h"

#include <stdlib.h>
#include <string.h>

static void write_error(char *out, size_t out_len, const char *msg) {
  if (!out || out_len == 0) return;
  size_t n = strlen(msg);
  if (n >= out_len) n = out_len - 1;
  memcpy(out, msg, n);
  out[n] = '\0';
}

static bool valid_utf8(const char *src, size_t len) {
  const unsigned char *p = (const unsigned char *)src;
  size_t i = 0;
  while (i < len) {
    unsigned char c = p[i++];
    if (c < 0x80) continue;

    unsigned need = 0;
    unsigned min = 0;
    unsigned code = 0;
    if ((c & 0xe0) == 0xc0) {
      need = 1;
      min = 0x80;
      code = c & 0x1f;
    } else if ((c & 0xf0) == 0xe0) {
      need = 2;
      min = 0x800;
      code = c & 0x0f;
    } else if ((c & 0xf8) == 0xf0) {
      need = 3;
      min = 0x10000;
      code = c & 0x07;
    } else {
      return false;
    }

    if (i + need > len) return false;
    for (unsigned j = 0; j < need; j++) {
      unsigned char cc = p[i++];
      if ((cc & 0xc0) != 0x80) return false;
      code = (code << 6) | (cc & 0x3f);
    }
    if (code < min || code > 0x10ffff) return false;
    if (code >= 0xd800 && code <= 0xdfff) return false;
  }
  return true;
}

static skim_options_t to_internal_options(const skim_options_t *options) {
  if (!options) return (skim_options_t){0};
  return (skim_options_t){
    .only_remove_type_imports = options->only_remove_type_imports,
    .optimize_enums = options->optimize_enums,
    .optimize_const_enums = options->optimize_const_enums,
    .remove_class_fields_without_initializer = options->remove_class_fields_without_initializer,
    .allow_declare_fields_false = options->allow_declare_fields_false,
    .transform_class_properties = options->transform_class_properties,
    .set_public_class_fields = options->set_public_class_fields,
  };
}

char *skim_strip_typescript_owned(
  const char *input,
  size_t input_len,
  const char *filename,
  bool is_module,
  const skim_options_t *options,
  size_t *out_len,
  skim_error_t *out_error,
  char *error_output,
  size_t error_output_len
) {
  (void)filename;
  (void)is_module;

  if (out_len) *out_len = 0;
  if (out_error) *out_error = SKIM_ERR_NULL_INPUT;

  if (!input || !out_len) {
    write_error(error_output, error_output_len, "null input/output passed");
    return NULL;
  }
  if (!valid_utf8(input, input_len)) {
    if (out_error) *out_error = SKIM_ERR_INVALID_UTF8;
    write_error(error_output, error_output_len, "source input is not valid UTF-8");
    return NULL;
  }

  skim_options_t previous = skim_options;
  skim_options = to_internal_options(options);
  char *result = skim_transform_typescript_len(input, input_len, out_len);
  skim_options = previous;

  if (!result) {
    if (out_error) *out_error = SKIM_ERR_TRANSFORM_FAILED;
    write_error(error_output, error_output_len, "failed to strip TypeScript");
    return NULL;
  }

  if (out_error) *out_error = SKIM_OK;
  return result;
}

void skim_free(char *ptr) {
  free(ptr);
}
