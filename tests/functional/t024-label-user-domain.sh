#!/usr/bin/env bash
#
# t024-label-user-domain.sh - user-domain label must fail before dump.
#
# com.apple.contactsd is a LaunchAgent in user/<uid>, not gui.
# oneshot of that label must exit non-zero with "already loaded"
# and empty stdout, so the temp plist is not written to disk.
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
	[[ -z "${HELLOWORLD_BIN:-}" ]] && \
		errx "HELLOWORLD_BIN not set"
	[[ ! -x "${HELLOWORLD_BIN}" ]] && \
		errx "not executable: ${HELLOWORLD_BIN}"

	local -r label="com.apple.contactsd"
	local out=""
	local err=""
	local errf=""
	local rc=0

	rc=0
	launchctl print "user/$(id -u)/${label}" >/dev/null 2>&1 || rc=$?
	[[ "${rc}" -ne 0 ]] && \
		errx "oracle: ${label} not in user/$(id -u)"

	errf="$(mktemp "/tmp/${__progname}.XXXXXX")"
	[ -f "${errf}" ] || \
		errx "mktemp"
	_cleanup_errf="${errf}"
	cleanup() {
		[ -n "${_cleanup_errf:-}" ] && rm -f -- "${_cleanup_errf}"
		_cleanup_errf=""
	}
	trap cleanup EXIT

	rc=0
	out="$("${SPAWN_BIN}" oneshot -l "${label}" -- \
		"${HELLOWORLD_BIN}" 0 2>"${errf}")" || rc=$?
	err="$(<"${errf}")"

	[[ "${rc}" -eq 0 ]] && \
		errx "oneshot succeeded for user-domain label"
	[[ -n "${out}" ]] && \
		errx "oneshot spilled stdout: ${out}"
	[[ "${err}" != *"already loaded: ${label}"* ]] && \
		errx "stderr missing already loaded: ${err}"

	echo "PASS ${__progname}"
}

main "$@"
