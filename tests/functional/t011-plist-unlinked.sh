#!/usr/bin/env bash
#
# t011-plist-unlinked.sh - Temp dir must be gone after a successful bootstrap.
#
# Bootstraps a oneshot, reads the printed temp plist path, and
# asserts that file and its parent directory are gone.
# Cleans up the job with launchctl bootout.
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

	local -r label="com.xspawn.test.unlink.$$"
	local -r domain="gui/$(id -u)/${label}"
	local out=""
	local plist=""
	local parent=""

	_cleanup_domain="${domain}"
	cleanup() {
		[ -z "${_cleanup_domain:-}" ] && return 0
		launchctl bootout "${_cleanup_domain}" >/dev/null 2>&1 || true
		_cleanup_domain=""
	}
	trap cleanup EXIT

	out="$("${SPAWN_BIN}" oneshot -l "${label}" -- \
		"${HELLOWORLD_BIN}" 20)"
	plist="${out%%$'\n'*}"
	parent="${plist%/*}"

	[[ "${plist}" != /*.plist ]] && \
		errx "first line is not a .plist path: ${plist}"
	[ -e "${plist}" ] && \
		errx "temp plist still on disk: ${plist}"
	[ -e "${parent}" ] && \
		errx "temp dir still on disk: ${parent}"

	echo "PASS ${__progname}"
}

main "$@"
