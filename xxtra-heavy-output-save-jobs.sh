#!/bin/bash -efuC
set -o pipefail

# Parse the output of xxtra-heavy-checkinstall package's script:
# save each job's output to a separate file

readonly MY_DIR="${0%/*}"

readonly LOG_TO_PARSE="$1"; shift
readonly DIR_FOR_WRITE="$LOG_TO_PARSE"-by-jobs

mkdir "$DIR_FOR_WRITE"

SED_CMDS=

append_cmd_to_write_a_line()
{
    local -r no="$1"
    local -r name="$2"
    local i="$no"
    until [ "${i#0}" = "$i" ]; do i="${i#0}"; done
    local -r i
    local fname
    printf -v fname '%03d:%s' "$i" "$name"
    local -r fname

    printf -v SED_CMDS \
	   '%s\ns,^\[%s\] (.*)$,%s,w %s' \
	   "$SED_CMDS" \
	   "[ 0-9]+/[^ ]+ $no/[0-9]+:$name" \
	   '\1' \
	   "$fname"
}

"$MY_DIR"/xxtra-heavy-output-ls-jobs.sh <"$LOG_TO_PARSE" | {
    while read -r NO NAME; do
	append_cmd_to_write_a_line "$NO" "$NAME"
    done

    #printf '%s\n' "$SED_CMDS"
    (cd "$DIR_FOR_WRITE" &&
	 sed -nEe "$SED_CMDS") <"$LOG_TO_PARSE"
}
