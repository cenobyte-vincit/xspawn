#!/usr/bin/env bash
#
# t010-stdio-paths.sh - Oneshot with -o/-e must honour stdio paths.
#
# Bootstraps helloworld into a scratch dir via StandardOutPath, waits
# briefly, and asserts the file contains the HELLOWORLD line.
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

	local -r label="com.xspawn.test.stdio.$$"
	local tmpdir=""
	local print_out=""
	local rc=0
	local i=0

	_cleanup_domain="gui/$(id -u)/${label}"
	_cleanup_tmpdir=""
	cleanup() {
		[ -n "${_cleanup_domain:-}" ] && \
			launchctl bootout "${_cleanup_domain}" >/dev/null 2>&1 || \
			true
		[ -n "${_cleanup_tmpdir:-}" ] && \
			rm -rf -- "${_cleanup_tmpdir}"
		_cleanup_domain=""
		_cleanup_tmpdir=""
	}
	trap cleanup EXIT

	tmpdir="$(mktemp -d "/tmp/${__progname}.XXXXXX")"
	[ -d "${tmpdir}" ] || \
		errx "mktemp -d"
	_cleanup_tmpdir="${tmpdir}"

	local -r out="${tmpdir}/out.txt"
	local -r errf="${tmpdir}/err.txt"

	"${SPAWN_BIN}" oneshot -l "${label}" -o "${out}" -e "${errf}" -- \
		"${HELLOWORLD_BIN}" 0

	rc=0
	print_out="$(launchctl print "${_cleanup_domain}" 2>&1)" || rc=$?
	[[ "${rc}" -ne 0 ]] && \
		errx "launchctl print failed after oneshot for ${label}"
	[[ "${print_out}" != *"type = LaunchAgent"* ]] && \
		errx "expected type = LaunchAgent"

	# helloworld 0 prints and exits; wait for the stdout file.
	for ((i = 0; i < 20; i++)); do
		[ -s "${out}" ] && break
		sleep 0.1
	done
	[ ! -f "${out}" ] && \
		errx "stdout file missing: ${out}"
	[[ "$(<"${out}")" != *HELLOWORLD:* ]] && \
		errx "stdout missing HELLOWORLD: $(<"${out}")"

	echo "PASS ${__progname}"
}

main "$@"
