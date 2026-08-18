#!/usr/bin/env bash
#
# t020-print-plist.sh - oneshot must print path then XML to stdout.
#
# Bootstraps helloworld, captures stdout, asserts the first line is
# the temp XXXXXX.plist path and the remainder is XML for that label.
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

	local -r label="com.xspawn.test.print.$$"
	local -r domain="gui/$(id -u)/${label}"
	local out=""
	local first=""
	local parent=""
	local dirbase=""
	local filebase=""
	local rest=""

	_cleanup_domain="${domain}"
	cleanup() {
		[ -z "${_cleanup_domain:-}" ] && return 0
		launchctl bootout "${_cleanup_domain}" >/dev/null 2>&1 || true
		_cleanup_domain=""
	}
	trap cleanup EXIT

	out="$("${SPAWN_BIN}" oneshot -l "${label}" -- \
		"${HELLOWORLD_BIN}" 20)"
	first="${out%%$'\n'*}"
	rest="${out#*$'\n'}"

	parent="${first%/*}"
	dirbase="${parent##*/}"
	filebase="${first##*/}"
	[[ "${first}" != /* ]] && \
		errx "first line is not an absolute path: ${first}"
	[[ "${first}" != *.plist ]] && \
		errx "first line is not the temp plist path: ${first}"
	[[ "${#dirbase}" -ne 6 ]] && \
		errx "temp dir name is not 6 characters: ${first}"
	[[ "${#filebase}" -ne 12 ]] && \
		errx "temp file name is not XXXXXX.plist: ${first}"
	[[ "${rest}" != *"<?xml"* ]] && \
		errx "stdout missing XML header: ${rest}"
	[[ "${rest}" != *"${label}"* ]] && \
		errx "XML missing label ${label}"
	[[ "${rest}" != *"${HELLOWORLD_BIN}"* ]] && \
		errx "XML missing program ${HELLOWORLD_BIN}"
	[[ "${rest}" != *"/dev/null"* ]] && \
		errx "XML missing /dev/null stdio paths"

	echo "PASS ${__progname}"
}

main "$@"
