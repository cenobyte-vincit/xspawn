#!/usr/bin/env bash
#
# t016-remove-submit.sh - submit then remove must unload the job.
#
# Submits a KeepAlive helloworld, asserts launchctl print finds it,
# then remove -l and asserts print no longer finds the service.
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

	local -r label="com.xspawn.test.remove.$$"
	local -r domain="gui/$(id -u)/${label}"
	local print_out=""
	local rc=0

	_cleanup_domain="${domain}"
	cleanup() {
		[ -z "${_cleanup_domain:-}" ] && return 0
		launchctl bootout "${_cleanup_domain}" >/dev/null 2>&1 || true
		_cleanup_domain=""
	}
	trap cleanup EXIT

	"${SPAWN_BIN}" submit -l "${label}" -- "${HELLOWORLD_BIN}" 20

	rc=0
	print_out="$(launchctl print "${domain}" 2>&1)" || rc=$?
	[[ "${rc}" -ne 0 ]] && \
		errx "launchctl print failed after submit for ${label}"
	[[ "${print_out}" != *"type = LaunchAgent"* ]] && \
		errx "expected type = LaunchAgent: ${print_out}"

	"${SPAWN_BIN}" remove -l "${label}"
	_cleanup_domain=""

	rc=0
	print_out="$(launchctl print "${domain}" 2>&1)" || rc=$?
	[[ "${rc}" -eq 0 ]] && \
		errx "print still succeeds after remove: ${print_out}"

	echo "PASS ${__progname}"
}

main "$@"
