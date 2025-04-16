#!/bin/sh -X # no runnable script, just for editors

# Some tests are "multiplied" by the method for accessing the repo (the one
# specified in sources.list).
#
# Here we actually have much more "pseudo-methods", where the base methods are
# further modified with various details about how the connection is set up
# to be made by apt.
#
# As for the _dbgconn modifier, tests with it are run first (to see more
# debug info if it takes much time waiting in tests and so the time limit
# is exceeded before all tests are complete).
APT_TEST_ALL_METHODS=(file http{,s{,_pinned}}{,_localhost6}{,_numeric}{,_proxy}{_dbgconn,} copy cdrom)

# filter out
filter_methods_to_skip() {
    local x
    for x; do

	case "$x" in
	    https_*_proxy*)
		# Those make almost no sense at all
		# because the proxy config is not honored for https now.
		# However, we leave just https_proxy* ones to demonstrate this.
		continue
		;;
	esac

	echo "$x"
    done
}
APT_TEST_ALL_METHODS=($(filter_methods_to_skip "${APT_TEST_ALL_METHODS[@]}"))

readonly -a APT_TEST_ALL_METHODS
