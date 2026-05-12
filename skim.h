#ifndef SKIM_H
#define SKIM_H

#include <stdbool.h>
#include <stddef.h>

#if defined(_WIN32)
#  if defined(SKIM_BUILDING_DLL)
#    define SKIM_API __declspec(dllexport)
#  else
#    define SKIM_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define SKIM_API __attribute__((visibility("default")))
#else
#  define SKIM_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  SKIM_OK = 0,
  SKIM_ERR_NULL_INPUT = -1,
  SKIM_ERR_INVALID_UTF8 = -2,
  SKIM_ERR_PARSE_FAILED = -3,
  SKIM_ERR_TRANSFORM_FAILED = -4,
  SKIM_ERR_OUTPUT_TOO_LARGE = -5,
} skim_error_t;

typedef struct {
  bool only_remove_type_imports;
  bool optimize_enums;
  bool optimize_const_enums;
  bool remove_class_fields_without_initializer;
  bool allow_declare_fields_false;
  bool transform_class_properties;
  bool set_public_class_fields;
} skim_options_t;

SKIM_API char *skim_strip_typescript_owned(
  const char *input,
  size_t input_len,
  const char *filename,
  bool is_module,
  const skim_options_t *options,
  size_t *out_len,
  skim_error_t *out_error,
  char *error_output,
  size_t error_output_len
);

SKIM_API void skim_free(char *ptr);

#ifdef __cplusplus
}
#endif

#endif
