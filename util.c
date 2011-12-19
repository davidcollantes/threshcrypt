/* threshcrypt util.c
 * Copyright 2012 Ryan Castellucci <code@ryanc.org>
 * This software is published under the terms of the Simplified BSD License.
 * Please see the 'COPYING' file for details.
 */

#include <unistd.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <termios.h>
#include <time.h>

#include "common.h"
#include "util.h"

void * safe_malloc(size_t size) {
  void *ptr = malloc(size);
  if (ptr == NULL) {
    perror("malloc");
    fprintf(stderr, "malloc(%d) returned NULL\n", (unsigned int)size);
    exit(EXIT_FAILURE);
  }
  memset(ptr, 0, size);
  return ptr;
}

void _safe_free(void **ptr, const char *file, int line) {
  if (*ptr != NULL) {
    free(*ptr);
    *ptr = NULL;
  } else {
    fprintf(stderr, "Warning: [file %s, line %d]: called safe_free on a null pointer\n", file, line);
  }
}

void _wipe_free(void **ptr, size_t size, const char *file, int line) {
  if (*ptr != NULL) {
    memset(*ptr, 0, size);
    free(*ptr);
    *ptr = NULL;
  } else {
    fprintf(stderr, "Warning: [file %s, line %d]: called wipe_free on a null pointer\n", file, line);
  }
}

void memxor(unsigned char *p1, const unsigned char *p2, size_t size) {
  size_t i = 0;
  for (i = 0;i < size;i++) {
    p1[i] ^= p2[i];
  }
}

void fill_rand(unsigned char *buffer,
               unsigned int count) {
  size_t n;
  FILE *devrandom;

  devrandom = fopen("/dev/urandom", "rb");
  if (!devrandom) {
    perror("Unable to read /dev/urandom");
    abort();
  }
  n = fread(buffer, 1, count, devrandom);
  if (n < count) {
      perror("Short read from /dev/urandom");
      abort();
  }
  fclose(devrandom);
}

/* vim: set ts=2 sw=2 et ai si: */