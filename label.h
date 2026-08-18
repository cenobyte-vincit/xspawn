/*
 * label.h - Public API for launchd job label validation.
 *
 * Used to reject empty, overlong, or non reverse-DNS-ish labels
 * before writing a plist or sending the bootstrap XPC message.
 */

#ifndef LABEL_H
#define LABEL_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Return 1 if label is acceptable, 0 otherwise.
 * Accepts non-NULL, non-empty strings of at most 128 bytes (excluding NUL)
 * using ASCII letters, digits, '.', '-', and '_' only.
 */
int label_is_valid(const char *);

#ifdef __cplusplus
}
#endif

#endif /* LABEL_H */
