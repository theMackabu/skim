#include "skim.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  THREAD_COUNT = 8,
  ROUNDS = 25,
};

typedef struct {
  char *source;
  size_t source_len;
  const char *path;
  char *expected;
  size_t expected_len;
} sample_t;

typedef struct {
  sample_t *samples;
  size_t sample_count;
  size_t offset;
  size_t mismatches;
} worker_t;

static const skim_options_t optimizing = {.optimize_enums = true, .optimize_const_enums = true};

static char *read_file(const char *path, size_t *out_len) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    perror(path);
    exit(1);
  }
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  rewind(f);
  char *data = malloc((size_t)size + 1);
  if (!data) exit(1);
  *out_len = fread(data, 1, (size_t)size, f);
  data[*out_len] = '\0';
  fclose(f);
  return data;
}

static void *run_worker(void *arg) {
  worker_t *worker = arg;
  skim_context_t ctx = {0};
  if (skim_context_init(&ctx) != 0) exit(1);
  for (size_t round = 0; round < ROUNDS; round++) {
    for (size_t n = 0; n < worker->sample_count; n++) {
      const sample_t *sample = &worker->samples[(n + worker->offset) % worker->sample_count];
      size_t out_len = 0;
      skim_error_t error = SKIM_OK;
      skim_context_reset(&ctx);
      const char *out = skim_strip_typescript_borrowed(
        &ctx, sample->source, sample->source_len, sample->path, SKIM_SOURCE_AUTO, &optimizing, &out_len, &error, NULL, 0
      );
      if (!out || out_len != sample->expected_len || memcmp(out, sample->expected, out_len) != 0) worker->mismatches++;
    }
  }
  skim_context_deinit(&ctx);
  return NULL;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s <fixture.ts>...\n", argv[0]);
    return 2;
  }
  size_t sample_count = (size_t)(argc - 1);
  sample_t *samples = calloc(sample_count, sizeof(*samples));
  if (!samples) return 1;
  for (size_t n = 0; n < sample_count; n++) {
    samples[n].path = argv[n + 1];
    samples[n].source = read_file(argv[n + 1], &samples[n].source_len);
    skim_error_t error = SKIM_OK;
    samples[n].expected = skim_strip_typescript_owned(
      samples[n].source,
      samples[n].source_len,
      samples[n].path,
      SKIM_SOURCE_AUTO,
      &optimizing,
      &samples[n].expected_len,
      &error,
      NULL,
      0
    );
    if (!samples[n].expected) {
      fprintf(stderr, "%s: strip failed\n", samples[n].path);
      return 1;
    }
  }

  pthread_t threads[THREAD_COUNT];
  worker_t workers[THREAD_COUNT];
  for (size_t t = 0; t < THREAD_COUNT; t++) {
    workers[t] = (worker_t){.samples = samples, .sample_count = sample_count, .offset = t};
    if (pthread_create(&threads[t], NULL, run_worker, &workers[t]) != 0) return 1;
  }
  size_t mismatches = 0;
  for (size_t t = 0; t < THREAD_COUNT; t++) {
    pthread_join(threads[t], NULL);
    mismatches += workers[t].mismatches;
  }

  for (size_t n = 0; n < sample_count; n++) {
    free(samples[n].source);
    skim_free(samples[n].expected);
  }
  free(samples);
  if (mismatches) {
    fprintf(stderr, "%zu outputs differed from the single-threaded result\n", mismatches);
    return 1;
  }
  puts("skim threads passed");
  return 0;
}
