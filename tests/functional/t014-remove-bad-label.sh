#!/usr/bin/env bash
#
# t014-remove-bad-label.sh - remove with a slash in the label must fail.
#
# Invokes remove with an illegal label. Expects non-zero exit and
# stderr mentioning "invalid label".
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

	local rc=0
	local err=""

	err="$("${SPAWN_BIN}" remove -l 'com/example' 2>&1)" || rc=$?

	if [[ "${rc}" -eq 0 ]]; then
		errx "expected non-zero exit, got 0"
	fi
	[[ ! "${err}" =~ invalid\ label ]] && \
		errx "stderr missing 'invalid label': ${err}"

	echo "PASS ${__progname}"
}

main "$@"
