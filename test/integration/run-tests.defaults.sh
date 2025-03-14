#!/bin/sh -X # no runnable script, just for editors

# Some tests are "multiplied" by the method for accessing the repo (the one
# specified in sources.list).
#
# Here we actually have much more "pseudo-methods", where the base methods are
# further modified with various details about how the connection is set up
# to be made by apt.
readonly -a APT_TEST_ALL_METHODS=(file http{,s{,_pinned}}{,_proxy} copy cdrom)
