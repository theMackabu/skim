#include "skim.h"
#include "skim_internal.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
  skim_str_t output;
  skim_str_t cleanup;
  skim_transform_state_t transform;
} skim_context_impl_t;

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

static bool filename_has_suffix(const char *filename, const char *suffix) {
  if (!filename || !suffix) return false;
  size_t filename_len = strlen(filename);
  size_t suffix_len = strlen(suffix);
  return filename_len >= suffix_len && strcmp(filename + filename_len - suffix_len, suffix) == 0;
}

static skim_source_mode_t infer_source_mode(const char *filename) {
  if (
    filename_has_suffix(filename, ".cts") || filename_has_suffix(filename, ".cjs") ||
    filename_has_suffix(filename, ".d.cts")
  )
    return SKIM_SOURCE_COMMONJS;
  if (
    filename_has_suffix(filename, ".mts") || filename_has_suffix(filename, ".mjs") ||
    filename_has_suffix(filename, ".d.mts")
  )
    return SKIM_SOURCE_MODULE;
  return SKIM_SOURCE_MODULE;
}

static skim_source_mode_t resolve_source_mode(const char *filename, skim_source_mode_t source_mode) {
  return source_mode == SKIM_SOURCE_AUTO ? infer_source_mode(filename) : source_mode;
}

static skim_context_impl_t *context_impl(skim_context_t *ctx) {
  return ctx ? (skim_context_impl_t *)ctx->impl : NULL;
}

int skim_context_init(skim_context_t *ctx) {
  if (!ctx) return -1;
  ctx->impl = calloc(1, sizeof(skim_context_impl_t));
  return ctx->impl ? 0 : -1;
}

void skim_context_reset(skim_context_t *ctx) {
  skim_context_impl_t *impl = context_impl(ctx);
  if (!impl) return;
  impl->output.len = 0;
  impl->cleanup.len = 0;
  if (impl->output.data) impl->output.data[0] = '\0';
  if (impl->cleanup.data) impl->cleanup.data[0] = '\0';
}

void skim_context_deinit(skim_context_t *ctx) {
  skim_context_impl_t *impl = context_impl(ctx);
  if (!impl) return;
  free(impl->output.data);
  free(impl->cleanup.data);
  skim_transform_state_free(&impl->transform);
  free(impl);
  ctx->impl = NULL;
}

const char *skim_strip_typescript_borrowed(
  skim_context_t *ctx,
  const char *input,
  size_t input_len,
  const char *filename,
  skim_source_mode_t source_mode,
  const skim_options_t *options,
  size_t *out_len,
  skim_error_t *out_error,
  char *error_output,
  size_t error_output_len
) {
  skim_context_impl_t *impl = context_impl(ctx);
  if (out_len) *out_len = 0;
  if (out_error) *out_error = SKIM_ERR_NULL_INPUT;

  if (!impl) {
    if (out_error) *out_error = SKIM_ERR_NULL_INPUT;
    write_error(error_output, error_output_len, "null context passed");
    return NULL;
  }
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
  skim_source_mode_t previous_source_mode = skim_source_mode;
  skim_transform_state_t *previous_state = skim_state;
  skim_options = to_internal_options(options);
  skim_source_mode = resolve_source_mode(filename, source_mode);
  skim_state = &impl->transform;
  skim_transform_typescript_into(input, input_len, &impl->output, &impl->cleanup);
  skim_options = previous;
  skim_source_mode = previous_source_mode;
  skim_state = previous_state;

  if (!impl->output.data) {
    if (out_error) *out_error = SKIM_ERR_TRANSFORM_FAILED;
    write_error(error_output, error_output_len, "failed to strip TypeScript");
    return NULL;
  }

  *out_len = impl->output.len;
  if (out_error) *out_error = SKIM_OK;
  return impl->output.data;
}

char *skim_strip_typescript_with_context(
  skim_context_t *ctx,
  const char *input,
  size_t input_len,
  const char *filename,
  skim_source_mode_t source_mode,
  const skim_options_t *options,
  size_t *out_len,
  skim_error_t *out_error,
  char *error_output,
  size_t error_output_len
) {
  const char *borrowed = skim_strip_typescript_borrowed(
    ctx, input, input_len, filename, source_mode, options, out_len, out_error, error_output, error_output_len
  );
  if (!borrowed) return NULL;

  char *result = malloc(*out_len + 1);
  if (!result) {
    if (out_error) *out_error = SKIM_ERR_TRANSFORM_FAILED;
    write_error(error_output, error_output_len, "out of memory");
    return NULL;
  }
  memcpy(result, borrowed, *out_len);
  result[*out_len] = '\0';
  return result;
}

char *skim_strip_typescript_owned(
  const char *input,
  size_t input_len,
  const char *filename,
  skim_source_mode_t source_mode,
  const skim_options_t *options,
  size_t *out_len,
  skim_error_t *out_error,
  char *error_output,
  size_t error_output_len
) {
  skim_context_t ctx = {0};
  if (skim_context_init(&ctx) != 0) {
    if (out_len) *out_len = 0;
    if (out_error) *out_error = SKIM_ERR_TRANSFORM_FAILED;
    write_error(error_output, error_output_len, "out of memory");
    return NULL;
  }
  char *result = skim_strip_typescript_with_context(
    &ctx, input, input_len, filename, source_mode, options, out_len, out_error, error_output, error_output_len
  );
  skim_context_deinit(&ctx);
  return result;
}

void skim_free(char *ptr) {
  free(ptr);
}
