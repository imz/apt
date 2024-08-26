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

. $TESTDIR/framework

setupenvironment

buildpackage 'simple-package'
buildpackage 'simple-package-noarch'
buildpackage 'conflicting-package-one'
buildpackage 'conflicting-package-two'

generaterepository_and_switch_sources "$TMPWORKINGDIRECTORY/usr/src/RPM/RPMS"

# Fake the cksum of pkglist (only noarch, since a noarch pkg is definitely present).
#
# (For faking the pkglist itself, see other tests,
# like test-apt-update-rejects-fake-pkglist-index.)
case "$CKSUM_TYPE" in
	Size)
		fake_repo_noarch_pkglist_size
		;;
	*)
		fake_repo_noarch_pkglist_cksum "$CKSUM_TYPE"
		;;
esac

# Cksum verification shouldn't pass.

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
