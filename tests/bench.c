#include "skim.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(__APPLE__)
#  include <libproc.h>
#  include <sys/resource.h>
#  include <unistd.h>
#  define BENCH_HAS_COUNTERS 1
#else
#  define BENCH_HAS_COUNTERS 0
#endif

typedef struct {
  unsigned long long nanos;
  unsigned long long instructions;
  unsigned long long cycles;
} sample_t;

static sample_t sample_now(void) {
  sample_t sample = {0};
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  sample.nanos = (unsigned long long)now.tv_sec * 1000000000ull + (unsigned long long)now.tv_nsec;
#if BENCH_HAS_COUNTERS
  struct rusage_info_v4 usage;
  if (proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&usage) == 0) {
    sample.instructions = usage.ri_instructions;
    sample.cycles = usage.ri_cycles;
  }
#endif
  return sample;
}

static sample_t sample_delta(sample_t from, sample_t to) {
  return (sample_t){to.nanos - from.nanos, to.instructions - from.instructions, to.cycles - from.cycles};
}

static void keep_best(sample_t *best, sample_t sample) {
  if (best->nanos == 0 || sample.nanos < best->nanos) best->nanos = sample.nanos;
  if (best->instructions == 0 || sample.instructions < best->instructions) best->instructions = sample.instructions;
  if (best->cycles == 0 || sample.cycles < best->cycles) best->cycles = sample.cycles;
}

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
  if (!data) {
    fprintf(stderr, "out of memory\n");
    exit(1);
  }
  *out_len = fread(data, 1, (size_t)size, f);
  data[*out_len] = '\0';
  fclose(f);
  return data;
}

static void print_result(const char *label, size_t bytes, sample_t best) {
  double mb_per_s = best.nanos ? (double)bytes * 1000.0 / (double)best.nanos : 0;
  printf("%-32s %9zu B %10.3f ms %9.1f MB/s", label, bytes, (double)best.nanos / 1e6, mb_per_s);
  if (BENCH_HAS_COUNTERS && bytes) {
    printf(
      " %8.1f ins/B %7.1f cyc/B", (double)best.instructions / (double)bytes, (double)best.cycles / (double)bytes
    );
  }
  putchar('\n');
}

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: %s <passes> <file.ts>...\n", argv[0]);
    return 2;
  }
  int passes = atoi(argv[1]);
  int file_count = argc - 2;
  bool per_file = getenv("SKIM_BENCH_PER_FILE") != NULL;
  char **sources = malloc(sizeof(*sources) * (size_t)file_count);
  size_t *lengths = malloc(sizeof(*lengths) * (size_t)file_count);
  sample_t *best_per_file = calloc((size_t)file_count, sizeof(*best_per_file));
  if (!sources || !lengths || !best_per_file) return 1;
  size_t total_bytes = 0;
  for (int i = 0; i < file_count; i++) {
    sources[i] = read_file(argv[i + 2], &lengths[i]);
    total_bytes += lengths[i];
  }

  skim_context_t ctx = {0};
  if (skim_context_init(&ctx) != 0) return 1;
  sample_t best = {0};
  for (int pass = 0; pass < passes; pass++) {
    sample_t pass_start = sample_now();
    for (int i = 0; i < file_count; i++) {
      sample_t file_start = per_file ? sample_now() : (sample_t){0};
      size_t out_len = 0;
      skim_error_t error = SKIM_OK;
      skim_context_reset(&ctx);
      skim_strip_typescript_borrowed(
        &ctx, sources[i], lengths[i], argv[i + 2], SKIM_SOURCE_AUTO, NULL, &out_len, &error, NULL, 0
      );
      if (per_file) keep_best(&best_per_file[i], sample_delta(file_start, sample_now()));
    }
    keep_best(&best, sample_delta(pass_start, sample_now()));
  }

  if (per_file) {
    for (int i = 0; i < file_count; i++) {
      const char *slash = strrchr(argv[i + 2], '/');
      print_result(slash ? slash + 1 : argv[i + 2], lengths[i], best_per_file[i]);
    }
  }
  char label[64];
  snprintf(label, sizeof(label), "total (%d files, best of %d)", file_count, passes);
  print_result(label, total_bytes, best);

  skim_context_deinit(&ctx);
  for (int i = 0; i < file_count; i++)
    free(sources[i]);
  free(sources);
  free(lengths);
  free(best_per_file);
  return 0;
}
