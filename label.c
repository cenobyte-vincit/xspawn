/*
 * label.c - Validate reverse-DNS-ish launchd job labels.
 *
 * Implements label_is_valid: length and charset checks only.
 * Leading or trailing dots are allowed; keep the rules simple and strict
 * on characters rather than DNS grammar.
 */

#include "label.h"

#include <stddef.h>

/* Maximum label length excluding the terminating NUL. */
#define LABEL_MAX_LEN	128

/*
 * Return 1 if c is an allowed label character (ASCII only).
 */
static int
label_char_ok(unsigned char c)
{
	if (c >= 'A' && c <= 'Z')
		return (1);
	if (c >= 'a' && c <= 'z')
		return (1);
	if (c >= '0' && c <= '9')
		return (1);
	if (c == '.' || c == '-' || c == '_')
		return (1);
	return (0);
}

/*
 * Return 1 if label is non-NULL, non-empty, at most LABEL_MAX_LEN bytes,
 * and uses only the reverse-DNS-ish charset; otherwise 0.
 */
int
label_is_valid(const char *label)
{
	size_t n;

	if (label == NULL)
		return (0);
	if (*label == '\0')
		return (0);

	n = 0;
	while (label[n] != '\0') {
		if (n >= LABEL_MAX_LEN)
			return (0);
		if (!label_char_ok((unsigned char)label[n]))
			return (0);
		n++;
	}
	return (1);
}
