/*
 * test-label.c - Unit tests for label_is_valid.
 *
 * Covers reverse-DNS-ish acceptance and rejections for NULL, empty,
 * illegal characters, and overlong labels. No live launchd required.
 */

#include "label.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_ASSERT(cond) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		return (1); \
	} \
} while (0)

/*
 * Accept well-formed reverse-DNS-ish labels.
 */
static int
test_label_good(void)
{
	TEST_ASSERT(label_is_valid("com.example.job") == 1);
	TEST_ASSERT(label_is_valid("com.xspawn.test.1") == 1);
	TEST_ASSERT(label_is_valid("a") == 1);
	TEST_ASSERT(label_is_valid("A.B_c-9") == 1);
	TEST_ASSERT(label_is_valid("org.foo.bar_baz-1") == 1);
	return (0);
}

/*
 * Reject NULL and empty strings.
 */
static int
test_label_null_empty(void)
{
	TEST_ASSERT(label_is_valid(NULL) == 0);
	TEST_ASSERT(label_is_valid("") == 0);
	return (0);
}

/*
 * Reject characters outside the allowed charset.
 */
static int
test_label_illegal_chars(void)
{
	TEST_ASSERT(label_is_valid("com.example.job!") == 0);
	TEST_ASSERT(label_is_valid("com/example") == 0);
	TEST_ASSERT(label_is_valid("com example") == 0);
	TEST_ASSERT(label_is_valid("com.example.job\n") == 0);
	TEST_ASSERT(label_is_valid("com.example.job;") == 0);
	TEST_ASSERT(label_is_valid("café") == 0);
	return (0);
}

/*
 * Reject labels longer than 128 bytes (exclusive of NUL).
 */
static int
test_label_too_long(void)
{
	char buf[130];
	size_t i;

	/* 128 'a' — at the limit, must pass */
	for (i = 0; i < 128; i++)
		buf[i] = 'a';
	buf[128] = '\0';
	TEST_ASSERT(label_is_valid(buf) == 1);

	/* 129 'a' — one over the limit */
	buf[128] = 'a';
	buf[129] = '\0';
	TEST_ASSERT(label_is_valid(buf) == 0);
	return (0);
}

int
main(void)
{
	if (test_label_good() != 0)
		exit(1);
	if (test_label_null_empty() != 0)
		exit(1);
	if (test_label_illegal_chars() != 0)
		exit(1);
	if (test_label_too_long() != 0)
		exit(1);
	printf("PASS\n");
	return (0);
}
