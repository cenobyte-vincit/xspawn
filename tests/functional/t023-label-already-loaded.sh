#!/usr/bin/env bash
#
# t023-label-already-loaded.sh - Taken label must fail before the dump.
#
# Submits a oneshot, then a second oneshot with the same label.
# The second must exit non-zero, print "already loaded" on stderr,
# and write nothing to stdout (no temp path, no XML copy of the
# job dictionary).
# That check exists so a doomed 800 does not spill the plist to
# disk (a DFIR artefact).
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

	local -r label="com.xspawn.test.loaded.$$"
	local -r domain="gui/$(id -u)/${label}"
	local out=""
	local err=""
	local errf=""
	local rc=0

	_cleanup_domain="${domain}"
	_cleanup_errf=""
	cleanup() {
		[ -n "${_cleanup_domain:-}" ] && \
			launchctl bootout "${_cleanup_domain}" >/dev/null 2>&1 || \
			true
		[ -n "${_cleanup_errf:-}" ] && rm -f -- "${_cleanup_errf}"
		_cleanup_domain=""
		_cleanup_errf=""
	}
	trap cleanup EXIT

	"${SPAWN_BIN}" oneshot -l "${label}" -- "${HELLOWORLD_BIN}" 20 \
		>/dev/null

	errf="$(mktemp "/tmp/${__progname}.XXXXXX")"
	[ -f "${errf}" ] || \
		errx "mktemp"
	_cleanup_errf="${errf}"

	rc=0
	out="$("${SPAWN_BIN}" oneshot -l "${label}" -- \
		"${HELLOWORLD_BIN}" 20 2>"${errf}")" || rc=$?
	err="$(<"${errf}")"

	[[ "${rc}" -eq 0 ]] && \
		errx "second oneshot succeeded for taken label"
	[[ -n "${out}" ]] && \
		errx "second oneshot spilled stdout: ${out}"
	[[ "${err}" != *"already loaded"* ]] && \
		errx "stderr missing already loaded: ${err}"

	echo "PASS ${__progname}"
}

main "$@"
