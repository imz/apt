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

. $TESTDIR/framework

# The "file" method doesn't compute and pass the cksums to apt.
case "$APT_TEST_METHOD" in
	file*) APT_TEST_XFAIL=yes
	       ;;
esac

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
fake_repo_noarch_pkglist_cksum "$CKSUM_TYPE"

# Cksum verification shouldn't pass.

testregexmatch '.*MD5Sum mismatch.*' aptget update
testfailure
testsuccess aptcache show simple-package
testfailure aptcache show simple-package-noarch
testfailure aptcache show nosuchpkg
