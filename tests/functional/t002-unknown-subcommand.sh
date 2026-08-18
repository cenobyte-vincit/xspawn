#!/usr/bin/env bash
#
# t002-unknown-subcommand.sh - Unknown subcommand must print usage.
#
# Invokes the binary with a bogus subcommand. Expects non-zero exit
# and stderr containing "usage:".
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

	err="$("${SPAWN_BIN}" nosuch 2>&1)" || rc=$?

	if [[ "${rc}" -eq 0 ]]; then
		errx "expected non-zero exit, got 0"
	fi
	[[ ! "${err}" =~ usage: ]] && \
		errx "stderr missing 'usage:': ${err}"

	echo "PASS ${__progname}"
}

main "$@"
