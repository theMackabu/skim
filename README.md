# meow meow meow

fast TypeScript -> JavaScript. library and tiny CLI.

```sh
meson setup build
meson compile -C build
meson test -C build
```

library users include `skim.h` and call `skim_strip_typescript_owned`. the
returned buffer is owned by the caller and must be released with `skim_free`.

```c
#include <skim.h>

size_t out_len = 0;
skim_error_t error = SKIM_OK;

char *js = skim_strip_typescript_owned(
  source, source_len, "input.ts",
  SKIM_SOURCE_AUTO, NULL,
  &out_len, &error, NULL, 0
);

if (js) {
  // use js[0..out_len]
  skim_free(js);
}
```
