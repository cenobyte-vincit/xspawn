#!/usr/bin/env bash
#
# t015-remove-unknown.sh - remove of a label that is not loaded must fail.
#
# Invokes remove with a valid but unknown label. Expects non-zero exit
# and a non-empty stderr diagnostic from launchd.
#
set -euo pipefail
IFS=$'\n\t'

# shellcheck disable=SC2155
readonly __progname="$(basename "${BASH_SOURCE[0]}")"
readonly PATH="/usr/sbin:/usr/bin:/sbin:/bin"

errx() {
	echo -e "${__progname}: $*" >&2

	exit 1
}

main() {
	[[ -z "${SPAWN_BIN:-}" ]] && \
		errx "SPAWN_BIN not set"
	[[ ! -x "${SPAWN_BIN}" ]] && \
		errx "not executable: ${SPAWN_BIN}"

	local -r label="com.xspawn.test.missing.$$"
	local rc=0
	local err=""

	err="$("${SPAWN_BIN}" remove -l "${label}" 2>&1)" || rc=$?

	if [[ "${rc}" -eq 0 ]]; then
		errx "expected non-zero exit, got 0"
	fi
	[[ -z "${err}" ]] && \
		errx "expected a diagnostic on stderr"

	echo "PASS ${__progname}"
}

main "$@"
