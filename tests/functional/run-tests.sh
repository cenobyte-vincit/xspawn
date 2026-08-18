#!/usr/bin/env bash
#
# run-tests.sh - Run xspawn functional tests in order.
#
# Usage: ./tests/functional/run-tests.sh <path-to-xspawn>
# Requires macOS (Darwin). Uses launchctl as an oracle only. Exports
# SPAWN_BIN, FIXTURES, and HELLOWORLD_BIN (the helloworld test program).
# Stops only after a full summary; exits 1 if any scenario fails.
#
set -euo pipefail
IFS=$'\n\t'

readonly __script_path="${BASH_SOURCE[0]}"
# shellcheck disable=SC2155
readonly __progname="$(basename "${__script_path}")"
readonly PATH="/usr/sbin:/usr/bin:/sbin:/bin"

errx() {
	echo -e "${__progname}: $*" >&2

	exit 1
}

usage() {
	echo -e "usage: ${__progname} <path-to-xspawn>" >&2

	exit 1
}

# Resolve a path to an absolute path (must already exist).
abs_path() {
	local -r raw="${1}"

	if [[ "${raw}" = /* ]]; then
		echo "${raw}"
		return 0
	fi

	echo "$(cd "$(dirname "${raw}")" && pwd)/$(basename "${raw}")"
}

main() {
	[[ "$#" -ne 1 ]] && \
		usage

	[[ ! "$(uname -s)" =~ ^Darwin ]] && \
		errx "macOS (Darwin) required"

	for bin in launchctl id mktemp grep sleep; do
		! command -v "${bin}" >/dev/null 2>&1 && \
			errx "cannot find '${bin}' in 'PATH=${PATH}'"
	done

	local -r prog_arg="${1}"
	[[ ! -e "${prog_arg}" ]] && \
		errx "not found: ${prog_arg}"
	[[ ! -x "${prog_arg}" ]] && \
		errx "not executable: ${prog_arg}"

	local -r testdir="$(cd "$(dirname "${__script_path}")" && pwd)"
	local -r fixtures="${testdir}/fixtures"
	[[ ! -d "${fixtures}" ]] && \
		errx "fixtures directory missing: ${fixtures}"

	local -r helloworld="${fixtures}/helloworld"
	[[ ! -x "${helloworld}" ]] && \
		errx "helloworld fixture missing: ${helloworld}"

	export SPAWN_BIN
	export FIXTURES
	export HELLOWORLD_BIN
	SPAWN_BIN="$(abs_path "${prog_arg}")"
	FIXTURES="${fixtures}"
	HELLOWORLD_BIN="$(abs_path "${helloworld}")"

	local passed=0
	local failed=0
	local t=""
	local rc=0
	local base=""

	shopt -s nullglob
	for t in "${testdir}"/t[0-9][0-9][0-9]-*.sh; do
		base="$(basename "${t}")"
		[[ ! -x "${t}" ]] && \
			errx "scenario not executable: ${t}"

		rc=0
		"${t}" || rc=$?
		if [[ "${rc}" -eq 0 ]]; then
			passed=$((passed + 1))
		else
			echo "FAIL ${base}"
			failed=$((failed + 1))
		fi
	done
	shopt -u nullglob

	echo ""
	echo "summary: ${passed} passed, ${failed} failed"

	[[ "${failed}" -ne 0 ]] && \
		exit 1

	[[ "${passed}" -eq 0 ]] && \
		errx "no scenario scripts found under ${testdir}"

	exit 0
}

main "$@"
