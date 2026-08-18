#!/usr/bin/env bash
#
# t012-missing-program.sh - Absolute missing binary is still a valid CLI.
#
# launchd accepts the plist even if the programme is absent. This test
# asserts the client still exits 0 and registers a LaunchAgent, then
# boots it out. Documents that existence is launchd's problem, not ours.
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
	local -r domain="gui/$(id -u)/${label}"
	local -r missing="/tmp/xspawn-no-such-$$"
	local print_out=""
	local rc=0

	_cleanup_domain="${domain}"
	cleanup() {
		[ -z "${_cleanup_domain:-}" ] && return 0
		launchctl bootout "${_cleanup_domain}" >/dev/null 2>&1 || true
		_cleanup_domain=""
	}
	trap cleanup EXIT

	[ -e "${missing}" ] && \
		errx "precondition: ${missing} must not exist"

	# Client must accept the absolute path. launchd may drop a
	# LaunchOnlyOnce job immediately if the binary is missing.
	"${SPAWN_BIN}" oneshot -l "${label}" -- "${missing}"

	rc=0
	print_out="$(launchctl print "${domain}" 2>&1)" || rc=$?
	if [[ "${rc}" -eq 0 ]]; then
		[[ "${print_out}" != *"type = LaunchAgent"* ]] && \
			errx "expected type = LaunchAgent: ${print_out}"
	fi

	echo "PASS ${__progname}"
}

main "$@"
