#!/usr/bin/python

# apt-groupinstall v0.1
# groupinstall helper script for apt
# by pmatilai@welho.com

import rhpl.comps, sys, string

comps = rhpl.comps.Comps("/usr/share/comps/i386/comps.xml")


def usage():
	print "Usage: %s showgroups" % sys.argv[0]
	print "       %s showgroup <group>" % sys.argv[0]
	print "       %s grouppkgs <group>" % sys.argv[0]
	sys.exit(1)

def findgroup(grpname):
	if comps.groups.has_key(grpname):
		return comps.groups[grpname]
	for group in comps.groups.values():
		if group.id == grpname:
			return group

def grouppkgs(grpname):
	if not findgroup(grpname):
		print "No such group: %s" % grpname
		return
	group = findgroup(grpname)
	if group:
		for pkg in comps.groups[group.name].packages:
			print "%s" % pkg

def showgroups():
	for group in comps.groups.values():
		print "%s (%s)" % (group.id, group.name)

def showgroup(grpname):
	group = findgroup(grpname)
	if not group:
		print "No such group: %s" % grpname
		return
	print "Group: %s" % group.id
	print "Description: %s" % group.description
	print "Packages: "
	for pkg in comps.groups[group.name].packages:
		print "  %s" % pkg

		
if __name__ == "__main__":
	if len(sys.argv) < 2:
		usage()
	if sys.argv[1] == "showgroups":
		showgroups()
	elif len(sys.argv) < 3:
		usage()
	elif sys.argv[1] == "grouppkgs":
		grouppkgs(sys.argv[2])
	elif sys.argv[1] == "showgroup":
		showgroup(sys.argv[2])
	else:
		usage()
	
