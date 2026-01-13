#!/bin/bash -efuC
set -o pipefail

# A helper to parse the output of xxtra-heavy-checkinstall package's script:
# list the jobs

readonly SLOT_RE='[ 0-9]+/[^ ]+'
#readonly JOBNO_RE='0*([1-9][0-9]*|0)'
readonly JOBNO_RE='[0-9]+'
readonly JOBNAME_RE='[^]]*'

sed -nEe 's,^'"\[$SLOT_RE ($JOBNO_RE)/([0-9]+):($JOBNAME_RE)\]"' (.*)$,\2 \1 \3,p' | sort -u -k 2 -n | {
    expected=0
    while read -r total no name; do
        until [ $expected -eq "$no" ]; do
	    printf >&2 'MISSING JOB NUMBER: %d!\n' \
		       $(( expected++ ))
	done
	[ "${TOTAL:=$total}" -eq "$total" ] ||
	    printf >&2 'INCONSISTENT TOTAL NUMBER: changed from %d to %d!\n' \
		       "$TOTAL" \
		       $(( TOTAL = total))
	# pass $no verbatim (with leading zeroes) for further matching
	printf '%s %s\n' "$no" "$name"
	(( ++expected ))
    done
    if [ "${TOTAL:-0}" -ne $expected ]; then
	printf >&2 'WRONG TOTAL NUMBER; actual jobs: %d!\n' \
		   "$expected"
    fi
}

# A note on efficiency:
#
# A way to do the saving to separate files with just one "sed -nE" program is like:
#
# s,^\[[^ ]+ ([0-9]+)/[0-9]+(:[^]]+)\] (.*)$,cat >>\1\2<<'PARSE_EOF'\n\3\nPARSE_EOF,ep
#
# but it's very inefficient because of the huge number of run processes;
# so I split this into ls-jobs.sh and save-jobs.sh to get a fixed list
# of job names and make sed open a fixed set of files once (see the 2nd script).
