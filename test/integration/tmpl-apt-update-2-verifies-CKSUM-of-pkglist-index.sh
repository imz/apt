#!/bin/bash
set -eu

readonly CKSUM_TYPE="$1"; shift

TESTDIR=$(readlink -f $(dirname $0))

case "$APT_TEST_METHOD" in
	cdrom*)
		echo 'SKIP (not ready for apt-cdrom, because it fetches the lists earlier)' >&2
		exit 0
		;;
esac

# apt chooses only one type of hash to verify.
# apt will choose BLAKE2b if available in the repo;
# so to actually check the verification of other checksum types,
# we have to disable the generation of BLAKE2b hashes.
[ "$CKSUM_TYPE" = Size ] ||
	[ "$CKSUM_TYPE" = BLAKE2b ] ||
	APT_TEST_GENBASEDIR_OPTS="$APT_TEST_GENBASEDIR_OPTS --no-blake2b"

. $TESTDIR/framework-without-repo
prereq $TESTDIR/test-apt-update-simple

if ! [ "$CKSUM_TYPE" = Size ]; then
	# Actual checksums (other than the size, which is easily got from stat)
	# are not computed at all by some methods (like file) and not passed
	# to apt, and hence not checked.
	case "$APT_TEST_METHOD" in
		file*) APT_TEST_XFAIL=yes
		       ;;
	esac
else
	# FIXME: Unlike test-apt-update-verifies-size-of-pkglist-index,
	# the file method surprizingly ignores the changed size in the release
	# file on the second run of "apt-get update"!
	case "$APT_TEST_METHOD" in
		file*) APT_TEST_XFAIL=yes
		       ;;
	esac
fi

# Fake the cksum of pkglist (only noarch, since a noarch pkg is definitely present).
#
# (For faking the pkglist itself, see other tests,
# like test-apt-update-2-rejects-fake-pkglist-index.)
case "$CKSUM_TYPE" in
	Size)
		fake_repo_noarch_pkglist_size
		;;
	*)
		fake_repo_noarch_pkglist_cksum "$CKSUM_TYPE"
		;;
esac

# Cksum verification shouldn't pass.
# apt should discard the old fetched pkglist, shouldn't it?
# Because now it sees a new release file with a different cksum.

case "$CKSUM_TYPE" in
	Size)
		testregexmatch '.*Size mismatch.*' \
			       aptget update
		;;
	*)
		testregexmatch '.*Checksum mismatch.*' \
			       aptget update
		;;
esac
testfailure
testsuccess aptcache show simple-package
testfailure aptcache show simple-package-noarch
testfailure aptcache show nosuchpkg
