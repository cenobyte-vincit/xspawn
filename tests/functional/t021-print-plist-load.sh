#!/usr/bin/env bash
#
# t021-print-plist-load.sh - load -p must print path then XML.
#
# Writes a caller-owned plist, loads it, and asserts stdout starts
# with that path and continues with XML containing the label.
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

	local -r label="com.xspawn.test.printload.$$"
	local -r domain="gui/$(id -u)/${label}"
	local tmpdir=""
	local plist=""
	local out=""
	local first=""
	local rest=""

	_cleanup_domain="${domain}"
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
	plist="${tmpdir}/job.plist"

	cat >"${plist}" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
 "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>Label</key><string>${label}</string>
<key>ProgramArguments</key><array>
<string>${HELLOWORLD_BIN}</string>
<string>20</string>
</array>
<key>RunAtLoad</key><true/>
<key>LaunchOnlyOnce</key><true/>
<key>StandardOutPath</key><string>/dev/null</string>
<key>StandardErrorPath</key><string>/dev/null</string>
</dict></plist>
EOF
	chmod 644 "${plist}"

	out="$("${SPAWN_BIN}" load -p "${plist}")"
	first="${out%%$'\n'*}"
	rest="${out#*$'\n'}"

	[[ "${first}" != "${plist}" ]] && \
		errx "first line is not ${plist}: ${first}"
	[[ "${rest}" != *"<?xml"* ]] && \
		errx "stdout missing XML header: ${rest}"
	[[ "${rest}" != *"${label}"* ]] && \
		errx "XML missing label ${label}"

	echo "PASS ${__progname}"
}

main "$@"
